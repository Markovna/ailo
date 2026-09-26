#pragma once
#include "ecs/Scene.h"

namespace ailo {

// Marks a mesh entity as skinned. Its bone matrices come from the AnimatorComponent on `animator`;
// if that entity is gone (or was never created), the renderer binds identity bones instead.
struct Skin {
    Entity animator = entt::null;
};

}
