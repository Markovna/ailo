#pragma once
#include <glm/glm.hpp>
#include <type_traits>

#include "Scene.h"
#include "Transform.h"
#include "assets/Assets.h"
#include "physics/PhysicsComponents.h"
#include "render/Material.h"

namespace ailo {

class RenderAPI;
struct Model;

namespace scene {

Entity spawnPrefab(Scene& scene, const asset_ptr<Model>& prefab, const glm::mat4& transform = glm::mat4(1.0f));
Entity spawnCube(Scene& scene, AssetManager& assets, RenderAPI& api, const Transform& transform = {}, MotionType motionType = MotionType::Dynamic);

template<typename ...TComponents>
Entity spawn(Scene& scene, TComponents&& ...components) {
    auto entity = scene.addEntity();
    (scene.addComponent<std::remove_cvref_t<TComponents>>(entity, std::forward<TComponents>(components)), ...);
    return entity;
}

}

}
