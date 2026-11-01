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
    std::vector<NodeTransform> pose;
    std::vector<NodeTransform> fromPose;
    std::vector<glm::mat4> local;
};

float advance(float time, float delta, const AnimationClip& clip, bool looping) {
    time += delta;
    return looping ? std::fmod(time, clip.duration) : time;
}

void samplePose(float time, const Skeleton& skeleton, const AnimationClip& clip, std::vector<NodeTransform>& pose) {
    pose = skeleton.bindPoseTransforms;

    for (auto& channel : clip.channels) {
        auto it = skeleton.nodeNameToIndex.find(channel.boneName);
        if (it == skeleton.nodeNameToIndex.end()) { continue; }

        assert(it->second < skeleton.nodes.size());
        auto& node = pose[it->second];
        node.position = BoneChannel::interpolate(time, channel.positionKeys);
        node.rotation = BoneChannel::interpolate(time, channel.rotationKeys);
        node.scale    = BoneChannel::interpolate(time, channel.scaleKeys);
    }
}

void blendPose(std::vector<NodeTransform>& pose, const std::vector<NodeTransform>& from, float weight) {
    for (size_t i = 0; i < pose.size(); ++i) {
        auto& to = pose[i];
        to.position = glm::mix(from[i].position, to.position, weight);
        to.rotation = glm::slerp(from[i].rotation, to.rotation, weight);
        to.scale    = glm::mix(from[i].scale, to.scale, weight);
    }
}

void composeLocal(const std::vector<NodeTransform>& pose, std::vector<glm::mat4>& local) {
    local.resize(pose.size());
    for (size_t i = 0; i < pose.size(); ++i) {
        local[i] = glm::translate(glm::mat4(1.0f), pose[i].position)
                 * glm::mat4_cast(pose[i].rotation)
                 * glm::scale(glm::mat4(1.0f), pose[i].scale);
    }
}

void startFadeOnClipChange(AnimatorComponent& animator) {
    auto& fade = animator.crossFade;
    if (fade.appliedClip == animator.currentClip) {
        return;
    }

    // Only two clips are blended, so switching mid-fade keeps whichever of the two weighs more as the outgoing one.
    const bool fading = fade.fromClip != AnimatorComponent::kNoClip;
    if (!fading || fade.elapsed >= 0.5f * fade.duration) {
        fade.fromClip = fade.appliedClip;
        fade.fromTime = fade.appliedTime;
        fade.fromLooping = fade.appliedLooping;
    }
    if (animator.fadeInTime <= 0.0f) {
        fade.fromClip = AnimatorComponent::kNoClip;
    }

    fade.elapsed = 0.0f;
    fade.duration = animator.fadeInTime;
    fade.appliedClip = animator.currentClip;
}

void writeBoneTransforms(const Skeleton& skeleton, std::vector<glm::mat4>& pose, BonesUniform& out) {
    for (uint32_t i = 0; i < skeleton.nodes.size(); ++i) {
        const auto& node = skeleton.nodes[i];
        auto& model = pose[i];

        assert(node.parentIndex < static_cast<int>(i));
        if (node.parentIndex >= 0) {
            model = pose[node.parentIndex] * model;
        }

        if (node.boneOutputIndex >= 0 &&
            static_cast<uint32_t>(node.boneOutputIndex) < BonesUniform::kMaxBones) {
            out.bones[node.boneOutputIndex].transform = model * skeleton.inverseBindPose[i];
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

        startFadeOnClipChange(animator);

        const auto& skeleton = *animator.skeleton;
        const auto& clip = *animator.clips[animator.currentClip];
        animator.currentTime = advance(animator.currentTime, time.delta, clip, animator.looping);
        samplePose(animator.currentTime, skeleton, clip, cache->pose);

        auto& fade = animator.crossFade;
        fade.appliedTime = animator.currentTime;
        fade.appliedLooping = animator.looping;

        if (fade.fromClip < animator.clips.size()) {
            fade.elapsed += time.delta;
            if (fade.elapsed < fade.duration) {
                const auto& from = *animator.clips[fade.fromClip];
                fade.fromTime = advance(fade.fromTime, time.delta, from, fade.fromLooping);
                samplePose(fade.fromTime, skeleton, from, cache->fromPose);
                blendPose(cache->pose, cache->fromPose, fade.elapsed / fade.duration);
            } else {
                fade.fromClip = AnimatorComponent::kNoClip;
            }
        }

        composeLocal(cache->pose, cache->local);
        writeBoneTransforms(skeleton, cache->local, bonesData);
        animator.boneBuffer.updateBuffer(&api, &bonesData, sizeof(bonesData));
    }
}

}

void AnimationPlugin::build(App& app) {
    app.addSystem(Stage::PostUpdate, animate, "AnimationPlugin::animate");
}

}
