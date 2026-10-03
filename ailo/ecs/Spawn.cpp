#include "Spawn.h"

#include "AnimatorComponent.h"
#include "Hierarchy.h"
#include "Transform.h"
#include "render/Model.h"
#include "render/Renderable.h"
#include "render/Skin.h"

namespace ailo::scene {

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
