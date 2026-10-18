#include "MemoryAssetReader.h"

#include <algorithm>
#include <stdexcept>

namespace ailo {

MemoryAssetReader::MemoryAssetReader(std::initializer_list<Asset> assets)
    : MemoryAssetReader(std::span(assets.begin(), assets.size())) {}

MemoryAssetReader::MemoryAssetReader(std::span<const Asset> assets) {
    m_assets.reserve(assets.size());
    for (const auto& [key, data] : assets) {
        m_assets.push_back({ std::string(key), data });
    }
    std::ranges::sort(m_assets, {}, &Entry::key);
    const auto duplicate = std::ranges::adjacent_find(m_assets, {}, &Entry::key);
    if (duplicate != m_assets.end()) {
        throw std::invalid_argument("asset '" + duplicate->key + "' is listed twice");
    }
}

std::vector<std::uint8_t> MemoryAssetReader::read(const std::string& key) {
    const auto data = find(key);
    if (!data) {
        throw std::runtime_error("no asset '" + key + "'");
    }
    return { data->begin(), data->end() };
}

std::optional<std::span<const std::uint8_t>> MemoryAssetReader::find(std::string_view key) const {
    const auto it = std::ranges::lower_bound(m_assets, key, {}, &Entry::key);
    if (it == m_assets.end() || it->key != key) return std::nullopt;
    return it->data;
}

}
