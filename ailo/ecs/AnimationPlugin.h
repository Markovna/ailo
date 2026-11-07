#pragma once

namespace ailo {

class App;

// Requires: PlatformPlugin (Time), AssetPlugin.
// Resources: AssetStorage<AnimationStateMachine>.
// PostUpdate: advances the state machine of every playing AnimatorComponent and writes the pose of the Skeleton on its
//            entity. A transition fades the new state's clip in over its fadeDuration on top of every clip still
//            fading out. Animators without a state machine are left alone.
struct AnimationPlugin {
    void build(App& app);
};

}
