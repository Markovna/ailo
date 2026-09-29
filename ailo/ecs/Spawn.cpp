#include "Spawn.h"

#include "AnimatorComponent.h"
#include "Transform.h"
#include "render/Model.h"
#include "render/Renderable.h"
#include "render/Skin.h"

namespace ailo::scene {

std::vector<Entity> spawn(Scene& scene, const asset_ptr<Model>& prefab, const glm::mat4& transform) {
    std::vector<Entity> entities;
    if (!prefab) return entities;

    entities.reserve(prefab->instances.size() + 1);

    // The animator owns the bone buffer that every skinned mesh of this instance reads.
    Entity animatorEntity = entt::null;
    if (prefab->skeleton && !prefab->clips.empty()) {
        animatorEntity = scene.addEntity();
        auto& animator = scene.addComponent<AnimatorComponent>(animatorEntity);
        animator.skeleton = prefab->skeleton;
        animator.clips = prefab->clips;
    }

    for (const auto& instance : prefab->instances) {
        auto entity = scene.addEntity();
        entities.push_back(entity);

        Renderable& renderable = scene.addComponent<Renderable>(entity);
        renderable.mesh = instance.mesh;
        renderable.materials.push_back(instance.material);

        Transform& tr = scene.addComponent<Transform>(entity);
        tr.transform = transform * instance.transform;

        if (instance.skinned)
            scene.addComponent<Skin>(entity, animatorEntity);
    }

    if (animatorEntity != entt::null)
        entities.push_back(animatorEntity);

    return entities;
}

}
