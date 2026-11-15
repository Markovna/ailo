#include "FootMarkers.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace modelc {

using namespace ailo::model;

namespace {

glm::vec3 interpolate(const glm::vec3& a, const glm::vec3& b, float t) { return glm::mix(a, b, t); }
glm::quat interpolate(const glm::quat& a, const glm::quat& b, float t) { return glm::slerp(a, b, t); }

// Clamped at both ends, like the runtime's BoneChannel::interpolate.
template<typename T>
T sample(const std::vector<Key<T>>& keys, float time, const T& fallback) {
    if (keys.empty()) return fallback;
    if (time <= keys.front().time) return keys.front().value;
    if (time >= keys.back().time) return keys.back().value;
    const auto next = std::ranges::upper_bound(keys, time, {}, &Key<T>::time);
    const auto& k1 = *next;
    const auto& k0 = *(next - 1);
    const float range = k1.time - k0.time;
    return interpolate(k0.value, k1.value, range > 0.0f ? (time - k0.time) / range : 0.0f);
}

struct Bind {
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 scale;
};

// For a channel that animates only some of a node's components.
Bind decompose(const glm::mat4& m) {
    const glm::vec3 scale { glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2])) };
    const glm::mat3 rotation { glm::vec3(m[0]) / scale.x, glm::vec3(m[1]) / scale.y, glm::vec3(m[2]) / scale.z };
    return { glm::vec3(m[3]), glm::quat_cast(rotation), scale };
}

struct Foot {
    const std::string* marker;
    std::vector<uint32_t> chain;    // skeleton nodes from the root down to the foot
};

}

std::vector<SyncMarker> findFootMarkers(std::span<const SkeletonNode> skeleton, const Clip& clip,
                                        const FootMarkerSettings& settings) {
    if (clip.duration <= 0.0f || settings.feet.empty()) return {};

    std::unordered_map<std::string, uint32_t> nodeIndices;
    for (uint32_t i = 0; i < skeleton.size(); i++) nodeIndices.emplace(skeleton[i].name, i);

    std::vector<const Channel*> channels(skeleton.size(), nullptr);
    for (const Channel& channel : clip.channels) {
        if (auto it = nodeIndices.find(channel.node); it != nodeIndices.end()) channels[it->second] = &channel;
    }

    std::vector<Foot> feet;
    for (const auto& [marker, bone] : settings.feet) {
        auto it = nodeIndices.find(bone);
        if (it == nodeIndices.end()) throw std::runtime_error("foot bone \"" + bone + "\" is not in the skeleton");
        Foot& foot = feet.emplace_back(&marker);
        for (int32_t node = int32_t(it->second); node >= 0; node = skeleton[node].parent) {
            foot.chain.push_back(uint32_t(node));
        }
        std::ranges::reverse(foot.chain);
    }

    std::vector<Bind> binds;
    binds.reserve(skeleton.size());
    for (const SkeletonNode& node : skeleton) binds.push_back(decompose(node.localTransform));

    const auto localTransform = [&](uint32_t node, float time) {
        const Channel* channel = channels[node];
        if (!channel) return skeleton[node].localTransform;
        const Bind& bind = binds[node];
        const glm::mat4 translation = glm::translate(glm::mat4(1.0f), sample(channel->positions, time, bind.position));
        const glm::mat4 rotation = glm::mat4_cast(sample(channel->rotations, time, bind.rotation));
        return glm::scale(translation * rotation, sample(channel->scales, time, bind.scale));
    };

    const uint32_t count = std::max(2u, uint32_t(std::ceil(clip.duration * settings.sampleRate)));
    const float step = clip.duration / float(count);

    // (sample, foot) for every foot that comes down, in foot order within a sample.
    std::vector<std::pair<uint32_t, const std::string*>> landings;
    std::vector<glm::vec3> positions(count);
    std::vector<uint8_t> planted(count);
    for (const Foot& foot : feet) {
        float lowest = std::numeric_limits<float>::max();
        for (uint32_t i = 0; i < count; i++) {
            glm::mat4 transform(1.0f);
            for (uint32_t node : foot.chain) transform *= localTransform(node, float(i) * step);
            positions[i] = glm::vec3(transform[3]);
            lowest = std::min(lowest, glm::dot(positions[i], settings.up));
        }

        uint32_t plantedCount = 0;
        for (uint32_t i = 0; i < count; i++) {
            bool down = glm::dot(positions[i], settings.up) - lowest <= settings.groundThreshold;
            // One-sided at the ends: with root motion the clip doesn't end where it starts.
            if (down && settings.speedThreshold > 0.0f) {
                const uint32_t previous = i > 0 ? i - 1 : i;
                const uint32_t next = i + 1 < count ? i + 1 : i;
                const float speed = glm::length(positions[next] - positions[previous]) / (float(next - previous) * step);
                down = speed <= settings.speedThreshold;
            }
            planted[i] = down;
            plantedCount += down;
        }
        if (plantedCount == 0 || plantedCount == count) continue;

        for (uint32_t i = 0; i < count; i++) {
            if (planted[i] && !planted[(i + count - 1) % count]) landings.emplace_back(i, foot.marker);
        }
    }

    std::ranges::stable_sort(landings, {}, &std::pair<uint32_t, const std::string*>::first);
    std::vector<SyncMarker> markers;
    for (size_t i = 0; i < landings.size(); i++) {
        if (i > 0 && landings[i].first == landings[i - 1].first) continue;
        markers.push_back({ *landings[i].second, float(landings[i].first) * step });
    }
    if (markers.size() > ailo::SyncTrack::kMaxEvents) {
        throw std::runtime_error(std::to_string(markers.size()) + " foot markers, the limit is " +
                                 std::to_string(ailo::SyncTrack::kMaxEvents));
    }
    return markers;
}

}
