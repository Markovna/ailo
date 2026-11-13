#include "AnimationPlugin.h"

#include "AnimatorComponent.h"
#include "Transform.h"
#include "app/App.h"
#include "assets/AssetPlugin.h"
#include "platform/PlatformPlugin.h"
#include "render/Skeleton.h"

namespace ailo {

namespace {

void animate(Query<AnimatorComponent, Skeleton, Exclude<Disabled>> animators, Local<PosePool> pool, const Time& time) {
    for (auto [entity, animator, skeleton] : animators.each()) {
        if (!animator.playing || !animator.graph) {
            continue;
        }

        animator.graph->update(time.delta);
        animator.graph->evaluate(skeleton, *pool, skeleton.pose);
        skeleton.updateModelPose();
    }
}

}

void AnimationPlugin::build(App& app) {
    addAssetType<AnimGraph>(app);
    app.addSystem(Stage::PostUpdate, animate, "AnimationPlugin::animate");
}

}
