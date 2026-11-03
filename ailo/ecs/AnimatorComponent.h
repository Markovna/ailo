#pragma once

#include <algorithm>
#include <vector>

#include "render/Animation.h"
#include "render/Skeleton.h"
#include "render/RenderPrimitive.h"

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

    AssetPtr<Skeleton> skeleton;
    std::vector<AssetPtr<AnimationClip>> clips;
    uint32_t currentClip = 0;
    float fadeInTime     = 0.0f;
    float currentTime    = 0.0f;
    bool  playing        = true;
    bool  looping        = true;
    BufferObject boneBuffer;
    std::vector<AnimationSampler> samplers;
};

} // namespace ailo
