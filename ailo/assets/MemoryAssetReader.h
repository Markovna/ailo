#pragma once

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "AssetReader.h"

namespace ailo {

class MemoryAssetReader final : public AssetReader {
public:
    using Asset = std::pair<std::string_view, std::span<const std::uint8_t>>;

    MemoryAssetReader(std::initializer_list<Asset> assets);
    explicit MemoryAssetReader(std::span<const Asset> assets);

    std::vector<std::uint8_t> read(const std::string& key) override;

    std::optional<std::span<const std::uint8_t>> find(std::string_view key) const;

private:
    struct Entry {
        std::string key;
        std::span<const std::uint8_t> data;
    };

    // Sorted by key.
    std::vector<Entry> m_assets;
};

}
