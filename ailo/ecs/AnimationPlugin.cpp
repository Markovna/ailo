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

struct PosePool {
    std::vector<std::vector<NodeTransform>> free;

    std::vector<NodeTransform> acquire() {
        if (free.empty()) {
            return {};
        }
        auto pose = std::move(free.back());
        free.pop_back();
        return pose;
    }

    void release(std::vector<NodeTransform>&& pose) { free.push_back(std::move(pose)); }
};

struct AnimationCache {
    BonesUniform bonesData {};
    PosePool pool;
    std::vector<NodeTransform> pose;
    std::vector<glm::mat4> local;
};

float advance(float time, float delta, const AnimationClip& clip, bool looping) {
    time += delta;
    return looping ? std::fmod(time, clip.duration) : time;
}

void samplePose(float time, const Skeleton& skeleton, const AnimationClip& clip, std::vector<NodeTransform>& pose) {
    pose.resize(skeleton.nodes.size());
    for (size_t i = 0; i < skeleton.nodes.size(); ++i) {
        auto& node = pose[i];
        auto it = clip.nameToIndex.find(skeleton.nodes[i].name);
        if (it == clip.nameToIndex.end()) {
            node = skeleton.bindPoseTransforms[i];
            continue;
        }
        auto& channel = clip.channels[it->second];
        node.position = BoneChannel::interpolate(time, channel.positionKeys);
        node.rotation = BoneChannel::interpolate(time, channel.rotationKeys);
        node.scale    = BoneChannel::interpolate(time, channel.scaleKeys);
    }
}

void blendPose(const std::span<NodeTransform>& out, const std::span<const NodeTransform>& pose, float weight) {
    for (size_t i = 0; i < out.size(); ++i) {
        auto& node = out[i];
        node.position = glm::mix(node.position, pose[i].position, weight);
        node.rotation = glm::slerp(node.rotation, pose[i].rotation, weight);
        node.scale    = glm::mix(node.scale, pose[i].scale, weight);
    }
}

// Each sampler fades in over the ones below it, so sampler i weighs fade_i * prod_{j>i}(1 - fade_j).
std::span<NodeTransform> blendStack(std::vector<AnimationSampler>& samplers, std::vector<NodeTransform>& out) {
    if (samplers.size() == 1) {
        return samplers[0].pose;
    }
    out = samplers[0].pose;
    for (size_t i = 1; i < samplers.size(); ++i) {
        blendPose(out, samplers[i].pose, samplers[i].fade());
    }
    return out;
}

void composeLocal(const std::span<NodeTransform>& pose, std::vector<glm::mat4>& local) {
    local.resize(pose.size());
    for (size_t i = 0; i < pose.size(); ++i) {
        local[i] = glm::translate(glm::mat4(1.0f), pose[i].position)
                 * glm::mat4_cast(pose[i].rotation)
                 * glm::scale(glm::mat4(1.0f), pose[i].scale);
    }
}

void releaseOldest(std::vector<AnimationSampler>& samplers, PosePool& pool, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        pool.release(std::move(samplers[i].pose));
    }
    samplers.erase(samplers.begin(), samplers.begin() + count);
}

void startSamplerOnClipChange(AnimatorComponent& animator, PosePool& pool) {
    auto& samplers = animator.samplers;
    if (!samplers.empty() && samplers.back().clip == animator.currentClip) {
        return;
    }

    if (animator.fadeInTime <= 0.0f) {
        releaseOldest(samplers, pool, samplers.size());
    } else if (samplers.size() >= AnimatorComponent::kMaxSamplers) {
        releaseOldest(samplers, pool, samplers.size() - AnimatorComponent::kMaxSamplers + 1);
    }

    samplers.reserve(AnimatorComponent::kMaxSamplers);
    auto& sampler = samplers.emplace_back();
    sampler.clip = animator.currentClip;
    sampler.looping = animator.looping;
    sampler.fadeDuration = animator.fadeInTime;
    sampler.pose = pool.acquire();
}

void retireHidden(std::vector<AnimationSampler>& samplers, PosePool& pool) {
    // TODO: use ring buffer?
    for (size_t i = samplers.size(); i-- > 1;) {
        if (samplers[i].fade() >= 1.0f) {
            releaseOldest(samplers, pool, i);
            return;
        }
    }
}

void writeBoneTransforms(const Skeleton& skeleton, const std::span<glm::mat4>& pose, BonesUniform& out) {
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

        startSamplerOnClipChange(animator, cache->pool);

        const auto& skeleton = *animator.skeleton;
        auto& samplers = animator.samplers;
        samplers.back().looping = animator.looping;

        for (auto& sampler : samplers) {
            sampler.time = advance(sampler.time, time.delta, *animator.clips[sampler.clip], sampler.looping);
            sampler.fadeElapsed += time.delta;
        }

        retireHidden(samplers, cache->pool);

        for (auto& sampler : samplers) {
            samplePose(sampler.time, skeleton, *animator.clips[sampler.clip], sampler.pose);
        }

        composeLocal(blendStack(samplers, cache->pose), cache->local);
        writeBoneTransforms(skeleton, cache->local, cache->bonesData);
        animator.boneBuffer.updateBuffer(&api, &cache->bonesData, sizeof(cache->bonesData));
    }
}

}

void AnimationPlugin::build(App& app) {
    app.addSystem(Stage::PostUpdate, animate, "AnimationPlugin::animate");
}

}
