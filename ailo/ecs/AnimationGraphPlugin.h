#pragma once

namespace ailo {

class App;

// Requires: PlatformPlugin (Time), AssetPlugin.
// Resources: AssetStorage<AnimGraph>.
// PostUpdate: updates the graph of every playing AnimationGraphComponent and writes its evaluated pose to the
//            Skeleton on its entity. Components without a graph are left alone.
struct AnimationGraphPlugin {
    void build(App& app);
};

}
