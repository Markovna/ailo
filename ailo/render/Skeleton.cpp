#include "Skeleton.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace ailo {

glm::vec3 Skeleton::interpolatePosition(float time, const BoneChannel& channel) const {
    if (channel.positionKeys.empty()) return glm::vec3(0.0f);
    if (channel.positionKeys.size() == 1) return channel.positionKeys[0].value;

    uint32_t idx = static_cast<uint32_t>(channel.positionKeys.size()) - 2;
    for (uint32_t i = 0; i < channel.positionKeys.size() - 1; i++) {
        if (time < channel.positionKeys[i + 1].time) {
            idx = i;
            break;
        }
    }
    const auto& k0 = channel.positionKeys[idx];
    const auto& k1 = channel.positionKeys[idx + 1];
    float range = k1.time - k0.time;
    float t = range > 0.0f ? (time - k0.time) / range : 0.0f;
    return glm::mix(k0.value, k1.value, glm::clamp(t, 0.0f, 1.0f));
}

glm::quat Skeleton::interpolateRotation(float time, const BoneChannel& channel) const {
    if (channel.rotationKeys.empty()) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (channel.rotationKeys.size() == 1) return channel.rotationKeys[0].value;

    uint32_t idx = static_cast<uint32_t>(channel.rotationKeys.size()) - 2;
    for (uint32_t i = 0; i < channel.rotationKeys.size() - 1; i++) {
        if (time < channel.rotationKeys[i + 1].time) {
            idx = i;
            break;
        }
    }
    const auto& k0 = channel.rotationKeys[idx];
    const auto& k1 = channel.rotationKeys[idx + 1];
    float range = k1.time - k0.time;
    float t = range > 0.0f ? (time - k0.time) / range : 0.0f;
    return glm::normalize(glm::slerp(k0.value, k1.value, glm::clamp(t, 0.0f, 1.0f)));
}

glm::vec3 Skeleton::interpolateScale(float time, const BoneChannel& channel) const {
    if (channel.scaleKeys.empty()) return glm::vec3(1.0f);
    if (channel.scaleKeys.size() == 1) return channel.scaleKeys[0].value;

    uint32_t idx = static_cast<uint32_t>(channel.scaleKeys.size()) - 2;
    for (uint32_t i = 0; i < channel.scaleKeys.size() - 1; i++) {
        if (time < channel.scaleKeys[i + 1].time) {
            idx = i;
            break;
        }
    }
    const auto& k0 = channel.scaleKeys[idx];
    const auto& k1 = channel.scaleKeys[idx + 1];
    float range = k1.time - k0.time;
    float t = range > 0.0f ? (time - k0.time) / range : 0.0f;
    return glm::mix(k0.value, k1.value, glm::clamp(t, 0.0f, 1.0f));
}

void Skeleton::computeBoneTransforms(float time, const AnimationClip& clip, BonesUniform& out) const {
    std::unordered_map<std::string, const BoneChannel*> channelMap;
    channelMap.reserve(clip.channels.size());
    for (const auto& channel : clip.channels) {
        channelMap[channel.boneName] = &channel;
    }

    // nodes[] is in parent-first (DFS) order, so worldTransforms[parentIndex] is
    // always computed before worldTransforms[i].
    std::vector<glm::mat4> worldTransforms(nodes.size());

    for (uint32_t i = 0; i < static_cast<uint32_t>(nodes.size()); i++) {
        const NodeInfo& node = nodes[i];
        glm::mat4 localTransform = node.localTransform;

        // If an animation channel exists for this node, override the bind-pose transform.
        auto it = channelMap.find(node.name);
        if (it != channelMap.end()) {
            const BoneChannel& channel = *it->second;
            glm::vec3 pos   = interpolatePosition(time, channel);
            glm::quat rot   = interpolateRotation(time, channel);
            glm::vec3 scale = interpolateScale(time, channel);
            localTransform = glm::translate(glm::mat4(1.0f), pos)
                           * glm::mat4_cast(rot)
                           * glm::scale(glm::mat4(1.0f), scale);
        }

        if (node.parentIndex == -1) {
            worldTransforms[i] = localTransform;
        } else {
            worldTransforms[i] = worldTransforms[static_cast<uint32_t>(node.parentIndex)] * localTransform;
        }

        // Only actual bone nodes write to the output uniform.
        if (node.boneOutputIndex >= 0 &&
            static_cast<uint32_t>(node.boneOutputIndex) < BonesUniform::kMaxBones) {
            out.bones[node.boneOutputIndex].transform = worldTransforms[i] * node.inverseBindPose;
        }
    }
}

} // namespace ailo
