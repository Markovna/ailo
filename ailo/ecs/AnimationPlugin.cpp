#include "AnimationPlugin.h"

#include <cmath>
#include <vector>

#include "AnimatorComponent.h"
#include "Transform.h"
#include "app/App.h"
#include "platform/PlatformPlugin.h"
#include "render/Skeleton.h"

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
            node = skeleton.restPose[i];
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
void blendStack(const std::vector<AnimationSampler>& samplers, std::vector<NodeTransform>& out) {
    out = samplers[0].pose;
    for (size_t i = 1; i < samplers.size(); ++i) {
        blendPose(out, samplers[i].pose, samplers[i].fade());
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

void animate(Query<AnimatorComponent, Skeleton, Exclude<Disabled>> animators, Local<PosePool> pool, const Time& time) {
    for (auto [entity, animator, skeleton] : animators.each()) {
        if (!animator.playing || animator.clips.empty()) {
            continue;
        }

        startSamplerOnClipChange(animator, *pool);

        auto& samplers = animator.samplers;
        samplers.back().looping = animator.looping;

        for (auto& sampler : samplers) {
            sampler.time = advance(sampler.time, time.delta, *animator.clips[sampler.clip], sampler.looping);
            sampler.fadeElapsed += time.delta;
        }

        retireHidden(samplers, *pool);

        for (auto& sampler : samplers) {
            samplePose(sampler.time, skeleton, *animator.clips[sampler.clip], sampler.pose);
        }

        blendStack(samplers, skeleton.pose);
        skeleton.updateModelPose();
    }
}

}

void AnimationPlugin::build(App& app) {
    app.addSystem(Stage::PostUpdate, animate, "AnimationPlugin::animate");
}

}
