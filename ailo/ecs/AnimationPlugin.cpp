#include "AnimationPlugin.h"

#include <cmath>

#include "AnimatorComponent.h"
#include "app/App.h"
#include "platform/PlatformPlugin.h"
#include "render/RenderAPI.h"
#include "render/Renderer.h"

namespace ailo {

namespace {

void animate(Query<AnimatorComponent> animators, const Time& time, RenderAPI& api) {
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

        animator.skeleton->updateBoneTransforms(animator.currentTime, clip, bonesData);
        animator.boneBuffer.updateBuffer(&api, &bonesData, sizeof(bonesData));
    }
}

}

void AnimationPlugin::build(App& app) {
    app.addSystem(Stage::PostUpdate, animate, "AnimationPlugin::animate");
}

}
