#include "AssetReader.h"

#include <stdexcept>

#include "FileIO.h"

namespace ailo {

std::vector<std::uint8_t> FileAssetReader::read(const std::string& key) {
    const auto path = m_root / std::filesystem::path(std::u8string(key.begin(), key.end()));
    std::vector<std::uint8_t> data;
    if (!fileio::readFile(path, data)) {
        const auto name = path.generic_u8string();
        throw std::runtime_error("cannot read file '" + std::string(name.begin(), name.end()) + "'");
    }
    return data;
}

}
