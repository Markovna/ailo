#include "AnimationPlugin.h"

#include <cmath>
#include <vector>

#include "AnimatorComponent.h"
#include "Transform.h"
#include "AnimationStateMachine.h"
#include "PoseOps.h"
#include "app/App.h"
#include "assets/AssetPlugin.h"
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

void pushSampler(AnimatorComponent& animator, PosePool& pool, uint32_t clip, bool looping, float fade) {
    auto& samplers = animator.samplers;
    if (fade <= 0.0f) {
        releaseOldest(samplers, pool, samplers.size());
    } else if (samplers.size() >= AnimatorComponent::kMaxSamplers) {
        releaseOldest(samplers, pool, samplers.size() - AnimatorComponent::kMaxSamplers + 1);
    }

    samplers.reserve(AnimatorComponent::kMaxSamplers);
    auto& sampler = samplers.emplace_back();
    sampler.clip = clip;
    sampler.looping = looping;
    sampler.fadeDuration = fade;
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
        if (!animator.playing || !animator.stateMachine || animator.stateMachine->definition().stateCount() == 0) {
            continue;
        }

        auto& stateMachine = *animator.stateMachine;
        auto& samplers = animator.samplers;
        if (samplers.empty()) {
            pushSampler(animator, *pool, stateMachine.currentClip(), stateMachine.currentState().looping, 0.0f);
        }
        // A transition always starts a new sampler, even into the clip already playing, so re-entering restarts it.
        if (const auto* transition = stateMachine.update(time.delta)) {
            pushSampler(animator, *pool, stateMachine.currentClip(), stateMachine.currentState().looping,
                        transition->fadeDuration);
        }

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
    addAssetType<AnimationStateMachine>(app);
    app.addSystem(Stage::PostUpdate, animate, "AnimationPlugin::animate");
}

}
