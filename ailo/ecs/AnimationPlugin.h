#pragma once

namespace ailo {

class App;

// Requires: PlatformPlugin (Time), AssetPlugin.
// Resources: AssetStorage<AnimGraph>.
// PostUpdate: updates the animation graph of every playing AnimatorComponent and writes its evaluated pose to the
//            Skeleton on its entity. A transition fades the new state in over its fadeDuration on top of every state
//            still fading out. Animators without a graph are left alone.
struct AnimationPlugin {
    void build(App& app);
};

}
