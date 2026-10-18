#include "AssetServer.h"

#include <algorithm>
#include <cctype>

#include "entt/core/fwd.hpp"

namespace ailo {

AssetServer::AssetServer(std::unique_ptr<AssetReader> defaultReader) : m_defaultReader(std::move(defaultReader)) {
    assert(m_defaultReader && "AssetServer needs an AssetReader");
}

AssetServer::~AssetServer() = default;

void AssetServer::registerReader(std::string source, std::unique_ptr<AssetReader> reader) {
    if (!reader) {
        throw std::invalid_argument("asset reader for source '" + source + "' is null");
    }
    if (source.empty() || source.find_first_of("/:") != std::string::npos) {
        throw std::invalid_argument("invalid asset source name '" + source + "'");
    }
    if (m_readers.contains(source)) {
        throw std::invalid_argument("asset reader already registered for source '" + source + "'");
    }
    m_readers.emplace(std::move(source), std::move(reader));
}

AssetReader& AssetServer::getReader(std::string_view source, const std::string& key) {
    if (source.empty()) {
        return *m_defaultReader;
    }
    auto it = m_readers.find(std::string(source));
    if (it == m_readers.end()) {
        throw std::runtime_error(std::format("no asset reader registered for source '{}' (asset '{}')", source, key));
    }
    return *it->second;
}

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
