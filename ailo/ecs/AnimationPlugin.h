#pragma once

namespace ailo {

class App;

// Requires: PlatformPlugin (Time), RenderPlugin (RenderAPI).
// PostUpdate: creates the bone buffer of new AnimatorComponents (identity bones),
//            then advances every playing AnimatorComponent and uploads its bone matrices.
struct AnimationPlugin {
    void build(App& app);
};

}
