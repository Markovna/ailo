#include "AssetServer.h"

#include "entt/core/fwd.hpp"

namespace ailo {

AssetServer::~AssetServer() = default;

detail::AssetStorageBase* AssetServer::findStorage(entt::id_type type) const {
    auto it = m_storages.find(type);
    return it == m_storages.end() ? nullptr : it->second;
}

detail::AssetLoaderBase* AssetServer::findLoader(entt::id_type type) const {
    auto it = m_loaders.find(type);
    return it == m_loaders.end() ? nullptr : it->second.get();
}

std::size_t AssetServer::reportLeaks(std::ostream& out) const {
    std::size_t leaked = 0;
    for (auto& [type, storage] : m_storages) {
        storage->reportLeaks(out);
        leaked += storage->size();
    }
    return leaked;
}

void AssetServer::setLoader(entt::id_type type, std::unique_ptr<detail::AssetLoaderBase> loader) {
    m_loaders.emplace(type, std::move(loader));
}

void AssetServer::setStorage(entt::id_type type, detail::AssetStorageBase* storage) {
    if (storage) {
        m_storages[type] = storage;
    } else {
        m_storages.erase(type);
    }
}

}
