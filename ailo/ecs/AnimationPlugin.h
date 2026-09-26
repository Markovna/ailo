#pragma once

namespace ailo {

class App;

// Requires: PlatformPlugin (Time), RenderPlugin (RenderAPI).
// PostUpdate: advances every playing AnimatorComponent and uploads its bone matrices.
struct AnimationPlugin {
    void build(App& app);
};

}
