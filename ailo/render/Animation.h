#pragma once
#include <ctime>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace ailo {

template <typename T>
struct AnimationKey {
    float time;
    T value;
};

template <typename T>
T interpolate(const T& l, const T& r, float t) {
    return l + (r - l) * t;
}

template<>
inline glm::vec3 interpolate<glm::vec3>(const glm::vec3& l, const glm::vec3& r, float t) {
    return glm::mix(l, r, t);
}

template<>
inline glm::quat interpolate<glm::quat>(const glm::quat& l, const glm::quat& r, float t) {
    return glm::slerp(l, r, t);
}

struct BoneChannel {
    std::string boneName;
    std::vector<AnimationKey<glm::vec3>> positionKeys;
    std::vector<AnimationKey<glm::quat>> rotationKeys;
    std::vector<AnimationKey<glm::vec3>> scaleKeys;

    template<typename T>
    static T interpolate(float time, const std::vector<AnimationKey<T>>& channel);
};

template <typename T>
T BoneChannel::interpolate(float time, const std::vector<AnimationKey<T>>& channel) {
    if (channel.empty()) return T();
    if (channel.size() == 1) return channel[0].value;
    uint32_t idx = channel.size() - 2;
    for (uint32_t i = 0; i < channel.size() - 1; i++) {
        if (time < channel[i + 1].time) {
            idx = i;
            break;
        }
    }

    const auto& k0 = channel[idx];
    const auto& k1 = channel[idx + 1];
    float range = k1.time - k0.time;
    float t = range > 0.0f ? (time - k0.time) / range : 0.0f;
    return ailo::interpolate(k0.value, k1.value, glm::clamp(t, 0.0f, 1.0f));
}

struct AnimationClip {
    std::string name;
    float duration;        // seconds
    float ticksPerSecond;
    std::vector<BoneChannel> channels;
};

} // namespace ailo
