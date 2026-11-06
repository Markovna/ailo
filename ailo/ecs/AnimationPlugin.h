#pragma once

namespace ailo {

class App;

// Requires: PlatformPlugin (Time).
// PostUpdate: advances every playing AnimatorComponent and writes the pose of the Skeleton on its entity.
//            Changing currentClip fades the new clip in over fadeInTime seconds on top of every clip still fading out.
struct AnimationPlugin {
    void build(App& app);
};

}
