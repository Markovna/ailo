#pragma once
#include <memory>
#include <vector>
#include "render/Animation.h"
#include "render/Skeleton.h"
#include "render/RenderPrimitive.h"

namespace ailo {

struct AnimatorComponent {
    std::shared_ptr<Skeleton> skeleton;
    std::vector<AnimationClip> clips;
    uint32_t currentClip = 0;
    float currentTime    = 0.0f;
    bool  playing        = true;
    bool  looping        = true;
    std::shared_ptr<BufferObject> boneBuffer; // shared with all mesh entities' Skin
};

} // namespace ailo
