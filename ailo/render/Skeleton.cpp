#include "Skeleton.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace ailo {

void Skeleton::updateBoneTransforms(float time, const AnimationClip& clip, BonesUniform& out) {
    for (auto& node : nodes) {
        glm::mat4 localTransform = node.localTransform;

        auto it = clip.nameToIndex.find(node.name);
        if (it != clip.nameToIndex.end()) {
            uint32_t channelIndex = it->second;
            const BoneChannel& channel = clip.channels[channelIndex];
            glm::vec3 pos   = BoneChannel::interpolate(time, channel.positionKeys);
            glm::quat rot   = BoneChannel::interpolate(time, channel.rotationKeys);
            glm::vec3 scale = BoneChannel::interpolate(time, channel.scaleKeys);
            localTransform = glm::translate(glm::mat4(1.0f), pos)
                           * glm::mat4_cast(rot)
                           * glm::scale(glm::mat4(1.0f), scale);
        }

        if (node.parentIndex == -1) {
            node.worldTransform = localTransform;
        } else {
            node.worldTransform = nodes[node.parentIndex].worldTransform * localTransform;
        }

        if (node.boneOutputIndex >= 0 &&
            static_cast<uint32_t>(node.boneOutputIndex) < BonesUniform::kMaxBones) {
            out.bones[node.boneOutputIndex].transform = node.worldTransform * node.inverseBindPose;
        }
    }
}

} // namespace ailo
