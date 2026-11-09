#pragma once

#include <span>
#include <vector>

#include "render/Animation.h"
#include "render/Skeleton.h"

namespace ailo {

inline void samplePose(float time, const Skeleton& skeleton, const AnimationClip& clip,
                       std::vector<NodeTransform>& pose) {
    pose.resize(skeleton.nodes.size());
    for (size_t i = 0; i < skeleton.nodes.size(); ++i) {
        auto& node = pose[i];
        auto it = clip.nameToIndex.find(skeleton.nodes[i].name);
        if (it == clip.nameToIndex.end()) {
            node = skeleton.restPose[i];
            continue;
        }
        auto& channel = clip.channels[it->second];
        node.position = BoneChannel::interpolate(time, channel.positionKeys);
        node.rotation = BoneChannel::interpolate(time, channel.rotationKeys);
        node.scale    = BoneChannel::interpolate(time, channel.scaleKeys);
    }
}

inline void blendPose(std::span<NodeTransform> out, std::span<const NodeTransform> pose, float weight) {
    for (size_t i = 0; i < out.size(); ++i) {
        auto& node = out[i];
        node.position = glm::mix(node.position, pose[i].position, weight);
        node.rotation = glm::slerp(node.rotation, pose[i].rotation, weight);
        node.scale    = glm::mix(node.scale, pose[i].scale, weight);
    }
}

} // namespace ailo
