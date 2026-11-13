#include "AnimationGraphPlugin.h"

#include "AnimationGraphComponent.h"
#include "Transform.h"
#include "app/App.h"
#include "assets/AssetPlugin.h"
#include "platform/PlatformPlugin.h"
#include "render/Skeleton.h"

namespace ailo {

namespace {

void animate(Query<AnimationGraphComponent, Skeleton, Exclude<Disabled>> animators, const Time& time,
             PosePool& posePool) {
    for (auto [entity, animator, skeleton] : animators.each()) {
        if (!animator.playing || !animator.graph) {
            continue;
        }

        animator.graph->update(time.delta);
        animator.graph->evaluate(skeleton, posePool, skeleton.pose);
        skeleton.updateModelPose();
    }
}

}

void AnimationGraphPlugin::build(App& app) {
    addAssetType<AnimGraph>(app);
    app.insertResource<PosePool>();
    app.addSystem(Stage::PostUpdate, animate, "AnimationGraphPlugin::animate");
}

}
