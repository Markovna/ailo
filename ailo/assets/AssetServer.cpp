#include "AssetServer.h"

#include <algorithm>
#include <cctype>

#include "entt/core/fwd.hpp"

namespace ailo {

AssetServer::AssetServer(std::unique_ptr<AssetReader> reader) : m_reader(std::move(reader)) {
    assert(m_reader && "AssetServer needs an AssetReader");
}

AssetServer::~AssetServer() = default;

detail::AssetStorageBase* AssetServer::findStorage(entt::id_type type) const {
    auto it = m_storages.find(type);
    return it == m_storages.end() ? nullptr : it->second;
}

namespace {

std::string toLower(std::string_view s) {
    std::string result(s);
    std::ranges::transform(result, result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

}

std::string AssetServer::extensionOf(std::string_view key) {
    const auto slash = key.find_last_of("/\\");
    const auto name = slash == std::string_view::npos ? key : key.substr(slash + 1);
    const auto dot = name.rfind('.');
    return dot == std::string_view::npos ? std::string() : toLower(name.substr(dot));
}

detail::AssetLoaderBase* AssetServer::findLoader(entt::id_type type, std::string_view extension) const {
    auto it = m_loadersByExtension.find(type);
    if (it == m_loadersByExtension.end()) {
        return nullptr;
    }
    auto loader = it->second.find(toLower(extension));
    return loader == it->second.end() ? nullptr : loader->second;
}

void AssetServer::addLoader(entt::id_type type, std::span<const std::string_view> extensions,
                            std::unique_ptr<detail::AssetLoaderBase> loader) {
    auto& byExtension = m_loadersByExtension[type];
    for (std::string_view extension : extensions) {
        byExtension.emplace(toLower(extension), loader.get());
    }
    m_loaders.push_back(std::move(loader));
}

void AssetServer::setStorage(entt::id_type type, detail::AssetStorageBase* storage) {
    if (storage) {
        m_storages[type] = storage;
    } else {
        m_storages.erase(type);
    }
}

}
