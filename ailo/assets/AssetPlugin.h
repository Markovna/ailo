#pragma once

#include "AssetServer.h"
#include "app/App.h"

namespace ailo {

// Resources: AssetServer.
// Asset types are added by the plugins that own them through addAssetType<T>, which inserts an
// AssetStorage<T> resource; they also register the loaders (e.g. RenderPlugin registers the Texture loader).
// Assets are destroyed as soon as their last AssetPtr drops. A storage reports the assets still alive and asserts
// that it is empty when destroyed, so insert storages after the resources their assets depend on (e.g. the
// RenderAPI) and before the resources that hold AssetPtrs.
struct AssetPlugin {
    void build(App& app) {
        app.insertResource<AssetServer>();
    }
};

template<class T>
AssetStorage<T>& addAssetType(App& app) {
    auto& storage = app.insertResource<AssetStorage<T>>();
    app.resource<AssetServer>().registerStorage(&storage);
    return storage;
}

}
