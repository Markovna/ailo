#include "Spawn.h"

#include "AnimatorComponent.h"
#include "Hierarchy.h"
#include "Transform.h"
#include "render/Model.h"
#include "render/Renderable.h"
#include "render/Shader.h"
#include "render/Skin.h"
#include "render/Texture.h"

namespace ailo::scene {

namespace {

asset_ptr<Material> whiteMaterial(AssetManager& assets, RenderAPI& api) {
    constexpr auto kPath = "builtin://materials/white";
    if (auto material = assets.get<Material>(kPath)) return material;

    auto shader = Shader::load(&assets, &api, Shader::getDefaultShaderDescription());
    auto material = assets.emplaceWithPath<Material>(kPath, &api, shader);
    material->setTexture(1, assets.load<Texture>("builtin://textures/white"));
    material->setTexture(2, assets.load<Texture>("builtin://textures/normal@norm"));
    material->setTexture(3, assets.load<Texture>("builtin://textures/default_metallic_roughness"));
    return material;
}

}

Entity spawnCube(Scene& scene, AssetManager& assets, RenderAPI& api, const Transform& transform, MotionType motionType) {
    const Entity entity = scene.addEntity();
    scene.addComponent<TransformComponent>(entity, transform);

    Renderable& renderable = scene.addComponent<Renderable>(entity);
    renderable.mesh = Mesh::unitCube(&assets, &api);
    renderable.materials.push_back(whiteMaterial(assets, api));

    auto& rigidBody = scene.addComponent<RigidBody>(entity);
    rigidBody.motionType = motionType;

    scene.addComponent<Collider>(entity, Collider { .shape = BoxShape { .halfExtents = glm::vec3(0.5f) } });
    return entity;
}

Entity spawn(Scene& scene, const asset_ptr<Model>& prefab, const glm::mat4& transform) {
    if (!prefab) return entt::null;

    const Entity root = scene.addEntity();
    scene.addComponent<TransformComponent>(root, Transform::fromMatrix(transform));

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

        scene.addComponent<TransformComponent>(entity, Transform::fromMatrix(instance.transform));
        hierarchy::setParent(scene.registry(), entity, root, hierarchy::Keep::Local);

        if (instance.skinned)
            scene.addComponent<Skin>(entity, root);
    }

    return root;
}

}
