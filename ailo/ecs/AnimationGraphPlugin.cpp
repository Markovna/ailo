#include "AnimationGraphPlugin.h"

#include "AnimationGraphComponent.h"
#include "Transform.h"
#include "app/App.h"
#include "assets/AssetPlugin.h"
#include "platform/PlatformPlugin.h"
#include "render/Skeleton.h"

namespace ailo {

namespace {

void animate(Query<AnimationGraphComponent, Skeleton, Exclude<Disabled>> animators, const Time& time) {
    for (auto [entity, animator, skeleton] : animators.each()) {
        if (!animator.playing || !animator.graph) {
            continue;
        }

        animator.graph->update(time.delta);
        if (animator.graph->evaluate(skeleton, animator.clips, skeleton.pose)) {
            skeleton.updateModelPose();
        }
    }
}

}

void AnimationGraphPlugin::build(App& app) {
    addAssetType<AnimGraph>(app);
    app.addSystem(Stage::PostUpdate, animate, "AnimationGraphPlugin::animate");
}

}
