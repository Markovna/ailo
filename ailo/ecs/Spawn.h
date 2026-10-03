#pragma once
#include <glm/glm.hpp>

#include "Scene.h"
#include "assets/Assets.h"

namespace ailo {

struct Model;

namespace scene {

Entity spawn(Scene& scene, const asset_ptr<Model>& prefab, const glm::mat4& transform = glm::mat4(1.0f));

}

}
