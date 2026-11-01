#pragma once

namespace ailo {

class App;

// Requires: PlatformPlugin (Time), RenderPlugin (RenderAPI).
// PostUpdate: creates the bone buffer of new AnimatorComponents (identity bones),
//            then advances every playing AnimatorComponent and uploads its bone matrices.
//            Changing currentClip crossfades from the previous clip over fadeInTime seconds.
struct AnimationPlugin {
    void build(App& app);
};

}
