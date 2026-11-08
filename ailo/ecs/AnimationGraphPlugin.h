#pragma once

namespace ailo {

class App;

// Requires: PlatformPlugin (Time), AssetPlugin.
// Resources: AssetStorage<AnimGraph>.
// PostUpdate: updates the graph of every playing AnimationGraphComponent, steps its blend and writes the blended pose
//            of the Skeleton on its entity. Components without a graph are left alone.
struct AnimationGraphPlugin {
    void build(App& app);
};

}
