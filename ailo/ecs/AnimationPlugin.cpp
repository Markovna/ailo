#include "AnimationPlugin.h"

#include <cmath>
#include <vector>
#include <cassert>
#include <glm/gtc/matrix_transform.hpp>

#include "AnimatorComponent.h"
#include "app/App.h"
#include "platform/PlatformPlugin.h"
#include "render/RenderAPI.h"
#include "render/Renderer.h"

#include "ecs/Transform.h"

namespace ailo {

namespace {

struct AnimationCache {
    std::vector<glm::mat4> localPose;
    std::vector<glm::mat4> modelPose;
};

void updateLocalPose(float time, const Skeleton& skeleton, const std::vector<BoneChannel>& channels, std::vector<glm::mat4>& local) {
    local = skeleton.bindPose;

    for (auto& channel : channels) {
        auto it = skeleton.nodeNameToIndex.find(channel.boneName);
        if (it == skeleton.nodeNameToIndex.end()) { continue; }

        assert(it->second < skeleton.nodes.size());
        glm::vec3 pos   = BoneChannel::interpolate(time, channel.positionKeys);
        glm::quat rot   = BoneChannel::interpolate(time, channel.rotationKeys);
        glm::vec3 scale = BoneChannel::interpolate(time, channel.scaleKeys);
        local[it->second] = glm::translate(glm::mat4(1.0f), pos)
                       * glm::mat4_cast(rot)
                       * glm::scale(glm::mat4(1.0f), scale);
    }
}

void writeBoneTransforms(const Skeleton& skeleton, const std::vector<glm::mat4>& local, std::vector<glm::mat4>& model, BonesUniform& out) {
    model.resize(skeleton.nodes.size());

    for (uint32_t i = 0; i < skeleton.nodes.size(); ++i) {
        const auto& node = skeleton.nodes[i];
        const auto& localTransform = local[i];

        model[i] = node.parentIndex == -1 ? localTransform : model[node.parentIndex] * localTransform;
        if (node.boneOutputIndex >= 0 &&
            static_cast<uint32_t>(node.boneOutputIndex) < BonesUniform::kMaxBones) {
            out.bones[node.boneOutputIndex].transform = model[i] * skeleton.inverseBindPose[i];
        }
    }
}

void animate(Query<AnimatorComponent, Exclude<Disabled>> animators, Local<AnimationCache> cache, const Time& time, RenderAPI& api) {
    BonesUniform bonesData {};
    for (auto [entity, animator] : animators.each()) {
        if (!animator.boneBuffer.getHandle()) {
            animator.boneBuffer = BufferObject(&api, BufferBinding::UNIFORM, sizeof(BonesUniform));
            BonesUniform identity {};
            for (auto& bone : identity.bones) {
                bone.transform = glm::mat4(1.0f);
            }
            animator.boneBuffer.updateBuffer(&api, &identity, sizeof(identity));
        }

        if (!animator.playing || animator.clips.empty()) {
            continue;
        }

        const auto& clip = *animator.clips[animator.currentClip];
        animator.currentTime += time.delta;
        if (animator.looping) {
            animator.currentTime = std::fmod(animator.currentTime, clip.duration);
        }

        updateLocalPose(animator.currentTime, *animator.skeleton, clip.channels, cache->localPose);
        writeBoneTransforms(*animator.skeleton, cache->localPose, cache->modelPose, bonesData);
        animator.boneBuffer.updateBuffer(&api, &bonesData, sizeof(bonesData));
    }
}

}

void AnimationPlugin::build(App& app) {
    app.addSystem(Stage::PostUpdate, animate, "AnimationPlugin::animate");
}

}
