#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ailo::texture {

// Bump whenever the binary layout or the meaning of any field changes; packages with another version are rejected.
static constexpr uint32_t kTexturePackageVersion = 1;

enum class TextureType : uint8_t {
    Texture2D,
    Cubemap,
};

enum class TextureFormat : uint8_t {
    RGBA8,
    RGBA8_SRGB,
    RGBA32F,
};

std::string_view toString(TextureType type);
std::string_view toString(TextureFormat format);
uint32_t bytesPerPixel(TextureFormat format);

uint32_t mipLevelCount(uint32_t width, uint32_t height);

struct TexturePackage {
    TextureType type = TextureType::Texture2D;
    TextureFormat format = TextureFormat::RGBA8_SRGB;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t levels = 1;                // levels stored in `data`
    bool generateMipmaps = false;       // requires levels == 1

    std::vector<uint8_t> data;

    uint32_t layers() const { return type == TextureType::Cubemap ? 6 : 1; }

    uint32_t allocatedLevels() const { return generateMipmaps ? mipLevelCount(width, height) : levels; }

    uint32_t levelWidth(uint32_t level) const;
    uint32_t levelHeight(uint32_t level) const;
    size_t imageSize(uint32_t level) const;
    size_t expectedDataSize() const;

    std::span<const uint8_t> image(uint32_t level, uint32_t layer) const;

    std::string validate() const;

    std::vector<uint8_t> serialize() const;

    static std::optional<TexturePackage> deserialize(std::span<const uint8_t> data, std::string& error);
};

}
