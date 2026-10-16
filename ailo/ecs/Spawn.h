#pragma once
#include <glm/glm.hpp>
#include <type_traits>

#include "Scene.h"
#include "Transform.h"
#include "assets/AssetServer.h"
#include "physics/PhysicsComponents.h"
#include "render/Material.h"

namespace ailo {

class RenderAPI;
class MaterialInstance;
struct Mesh;
struct Model;

namespace scene {

Entity spawnPrefab(Scene& scene, const AssetPtr<Model>& prefab, const Transform& transform = {});
Entity spawnCube(Scene& scene, RenderAPI& api, AssetStorage<MaterialInstance>& materialInstances,
                 AssetServer& server, const Transform& transform = {}, MotionType motionType = MotionType::Dynamic);

template<typename ...TComponents>
Entity spawn(Scene& scene, TComponents&& ...components) {
    auto entity = scene.addEntity();
    (scene.addComponent<std::remove_cvref_t<TComponents>>(entity, std::forward<TComponents>(components)), ...);
    return entity;
}

}

}
