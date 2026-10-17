#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ailo::texture {

// Bump whenever the binary layout or the meaning of any field changes; packages with another version are rejected.
static constexpr uint32_t kTexturePackageVersion = 2;

enum class TextureType : uint8_t {
    Texture2D,
    Cubemap,
};

enum class TextureFormat : uint8_t {
    RGBA8,
    RGBA8_SRGB,
    RGBA32F,
    RGBA16F,
    // Block-compressed, 4x4 texel blocks.
    BC1,        // rgb, opaque
    BC1_SRGB,
    BC3,        // rgba
    BC3_SRGB,
    BC4,        // r
    BC5,        // rg
    BC6H,       // rgb half float, unsigned
    BC7,        // rgba
    BC7_SRGB,
};

std::string_view toString(TextureType type);
std::string_view toString(TextureFormat format);
bool isCompressed(TextureFormat format);
bool isSrgb(TextureFormat format);
// Texels per block side: 1 for uncompressed formats, 4 for block-compressed ones.
uint32_t blockExtent(TextureFormat format);
// Bytes per texel for uncompressed formats, per block for compressed ones.
uint32_t blockBytes(TextureFormat format);

uint32_t mipLevelCount(uint32_t width, uint32_t height);

struct TexturePackage {
    TextureType type = TextureType::Texture2D;
    TextureFormat format = TextureFormat::RGBA8_SRGB;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t levels = 1;                // levels stored in `data`
    bool generateMipmaps = false;       // requires levels == 1 and an uncompressed format

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
