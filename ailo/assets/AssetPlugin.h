#pragma once

#include "AssetServer.h"
#include "app/App.h"

namespace ailo {

// Resources: AssetServer.
// Shutdown: reports every asset still alive (it runs after the Shutdown systems of plugins added later).
// Asset types are added by the plugins that own them through addAssetType<T>, which inserts an
// AssetStorage<T> resource; they also register the loaders (e.g. RenderPlugin registers the Texture loader).
// Assets are destroyed as soon as their last AssetPtr drops. A storage asserts that it is empty when destroyed,
// so insert storages after the resources their assets depend on (e.g. the RenderAPI).
struct AssetPlugin {
    void build(App& app) {
        app.insertResource<AssetServer>();
        app.addSystem(Stage::Shutdown, [](const AssetServer& server) { server.reportLeaks(); }, "AssetPlugin::reportLeaks");
    }
};

template<class T>
AssetStorage<T>& addAssetType(App& app) {
    auto& storage = app.insertResource<AssetStorage<T>>();
    app.resource<AssetServer>().registerStorage(&storage);
    return storage;
}

}
