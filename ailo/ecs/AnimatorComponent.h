#pragma once

#include <algorithm>
#include <optional>
#include <vector>

#include "AnimationStateMachine.h"
#include "assets/AssetServer.h"
#include "render/Animation.h"

namespace ailo {

struct AnimationSampler {
    uint32_t clip         = 0;
    float    time         = 0.0f;
    bool     looping      = true;
    float    fadeElapsed  = 0.0f;
    float    fadeDuration = 0.0f;
    std::vector<NodeTransform> pose;

    float fade() const { return fadeDuration > 0.0f ? std::min(fadeElapsed / fadeDuration, 1.0f) : 1.0f; }
};

struct AnimatorComponent {
    static constexpr uint32_t kMaxSamplers = 8;

    std::vector<AssetPtr<AnimationClip>> clips;
    bool playing = true;
    std::optional<AnimationStateMachineInstance> stateMachine;
    std::vector<AnimationSampler> samplers;

    // Starts in the definition's first state, without a fade. Throws std::invalid_argument if a state's clip isn't
    // in clips.
    void setStateMachine(AssetPtr<AnimationStateMachine> definition) {
        stateMachine.emplace(std::move(definition), clips);
        samplers.clear();
    }
};

} // namespace ailo
