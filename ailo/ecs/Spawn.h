#pragma once
#include <glm/glm.hpp>

#include "Scene.h"
#include "Transform.h"
#include "assets/Assets.h"
#include "physics/PhysicsComponents.h"
#include "render/Material.h"

namespace ailo {

class RenderAPI;
struct Model;

namespace scene {

Entity spawn(Scene& scene, const asset_ptr<Model>& prefab, const glm::mat4& transform = glm::mat4(1.0f));
Entity spawnCube(Scene& scene, AssetManager& assets, RenderAPI& api, const Transform& transform = {}, MotionType motionType = MotionType::Dynamic);

}

}
