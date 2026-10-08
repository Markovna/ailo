#pragma once
#include <vector>
#include "render/Animation.h"
#include "render/Skeleton.h"
#include "render/RenderPrimitive.h"

namespace ailo {

struct AnimatorComponent {
    AssetPtr<Skeleton> skeleton;
    std::vector<AssetPtr<AnimationClip>> clips;
    uint32_t currentClip = 0;
    float currentTime    = 0.0f;
    bool  playing        = true;
    bool  looping        = true;
    BufferObject boneBuffer; // bone matrices read by every Skin that points at this entity; created by AnimationPlugin
};

} // namespace ailo
