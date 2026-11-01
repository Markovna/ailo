#pragma once

#include <limits>
#include <vector>

#include "render/Animation.h"
#include "render/Skeleton.h"
#include "render/RenderPrimitive.h"

namespace ailo {

struct AnimatorComponent {
    static constexpr uint32_t kNoClip = std::numeric_limits<uint32_t>::max();

    // Kept by AnimationPlugin to crossfade from the previous clip when currentClip changes.
    struct CrossFade {
        uint32_t appliedClip = kNoClip; // the clip the plugin last played; currentClip differing from it starts a fade
        float    appliedTime = 0.0f;    // its time then, since the game resets currentTime when it switches clips
        bool     appliedLooping = true;
        uint32_t fromClip = kNoClip;    // kNoClip when not fading
        float    fromTime = 0.0f;
        bool     fromLooping = true;
        float    elapsed = 0.0f;
        float    duration = 0.0f;       // fadeInTime when the fade started
    };

    AssetPtr<Skeleton> skeleton;
    std::vector<AssetPtr<AnimationClip>> clips;
    uint32_t currentClip = 0;
    float fadeInTime     = 0.0f;   // seconds to crossfade from the previous clip when currentClip changes
    float currentTime    = 0.0f;
    bool  playing        = true;
    bool  looping        = true;
    BufferObject boneBuffer; // bone matrices read by every Skin that points at this entity; created by AnimationPlugin
    CrossFade crossFade;
};

} // namespace ailo
