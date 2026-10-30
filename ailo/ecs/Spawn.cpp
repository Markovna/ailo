#include "Spawn.h"

#include "AnimatorComponent.h"
#include "Hierarchy.h"
#include "Transform.h"
#include "render/DefaultAssetFactory.h"
#include "render/Model.h"
#include "render/Renderable.h"
#include "render/Skin.h"
#include "render/Texture.h"

namespace ailo::scene {

namespace {

AssetPtr<MaterialInstance> whiteMaterial(RenderAPI& api, AssetStorage<MaterialInstance>& materialInstances, AssetServer& server) {
    constexpr auto kPath = "builtin://materials/white";
    if (auto instance = materialInstances.get(kPath)) return *instance;

    auto material = server.load<Material>(materials::kLit);
    if (!material) return {};

    auto instance = materialInstances.emplace(kPath, &api, server, std::move(material));
    instance->setParameter("metallicRoughnessMap", server.load<Texture>(textures::kDefaultMetallicRoughness));
    return instance;
}

}

Entity spawnCube(Scene& scene, RenderAPI& api, AssetStorage<MaterialInstance>& materialInstances,
                 AssetServer& server, const Transform& transform, MotionType motionType) {
    const Entity entity = scene.addEntity();
    scene.addComponent<TransformComponent>(entity, transform);

    Renderable& renderable = scene.addComponent<Renderable>(entity);
    renderable.mesh = server.load<Mesh>(meshes::kUnitCube);
    renderable.materials.push_back(whiteMaterial(api, materialInstances, server));

    auto& rigidBody = scene.addComponent<RigidBody>(entity);
    rigidBody.motionType = motionType;

    scene.addComponent<Collider>(entity, Collider { .shape = BoxShape { .halfExtents = glm::vec3(0.5f) } });
    return entity;
}

Entity spawnPrefab(Scene& scene, const AssetPtr<Model>& prefab, const Transform& transform) {
    if (!prefab) return entt::null;

    const Entity root = scene.addEntity();
    scene.addComponent<TransformComponent>(root, transform);

    // The animator owns the bone buffer that every skinned mesh of this instance reads.
    if (prefab->skeleton && !prefab->clips.empty()) {
        auto& animator = scene.addComponent<AnimatorComponent>(root);
        animator.skeleton = prefab->skeleton;
        animator.clips = prefab->clips;
    }

    for (const auto& instance : prefab->instances) {
        auto entity = scene.addEntity();

        Renderable& renderable = scene.addComponent<Renderable>(entity);
        renderable.mesh = instance.mesh;
        renderable.materials.push_back(instance.material);

        // The bone matrices already place skinned vertices in model space, through the whole node hierarchy.
        const glm::mat4 local = instance.skinned ? glm::mat4(1.0f) : instance.transform;
        scene.addComponent<TransformComponent>(entity, Transform::fromMatrix(local));
        hierarchy::setParent(scene.registry(), entity, root, hierarchy::Keep::Local);

        if (instance.skinned)
            scene.addComponent<Skin>(entity, root);
    }

    return root;
}

}
