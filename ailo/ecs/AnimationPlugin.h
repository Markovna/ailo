#pragma once

namespace ailo {

class App;

// Requires: PlatformPlugin (Time), RenderPlugin (RenderAPI).
// PostUpdate: creates the bone buffer of new AnimatorComponents (identity bones),
//            then advances every playing AnimatorComponent and uploads its bone matrices.
//            Changing currentClip fades the new clip in over fadeInTime seconds on top of every clip still fading out.
struct AnimationPlugin {
    void build(App& app);
};

}
