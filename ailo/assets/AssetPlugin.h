#pragma once

#include "Assets.h"
#include "app/App.h"

namespace ailo {

// Resources: AssetManager.
// Last: collects assets whose last reference was dropped this frame.
// Loaders are registered by the plugins that own the asset types (e.g. RenderPlugin registers the Texture loader).
struct AssetPlugin {
    void build(App& app) {
        app.insertResource<AssetManager>();
        app.addSystem(Stage::Last, [](AssetManager& assets) { assets.gc(); }, "AssetPlugin::gc");
    }
};

}
