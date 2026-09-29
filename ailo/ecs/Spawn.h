#pragma once
#include <vector>
#include <glm/glm.hpp>

#include "Scene.h"
#include "assets/Assets.h"

namespace ailo {

struct Model;

namespace scene {

// Instantiates `prefab` into `scene`: one entity per mesh instance (Renderable + Transform, plus Skin if skinned),
// and, if the model is skinned and has clips, an AnimatorComponent entity that its Skins point at.
std::vector<Entity> spawn(Scene& scene, const asset_ptr<Model>& prefab, const glm::mat4& transform = glm::mat4(1.0f));

}

}
