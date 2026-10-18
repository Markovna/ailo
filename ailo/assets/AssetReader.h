#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ailo {

class AssetReader {
public:
    virtual ~AssetReader() = default;
    virtual std::vector<std::uint8_t> read(const std::string& key) = 0;
};

class FileAssetReader final : public AssetReader {
public:
    explicit FileAssetReader(std::filesystem::path root = {}) : m_root(std::move(root)) {}

    std::vector<std::uint8_t> read(const std::string& key) override;

    const std::filesystem::path& root() const { return m_root; }

private:
    std::filesystem::path m_root;
};

}
