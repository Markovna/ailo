#include "TexturePackage.h"

#include <algorithm>
#include <bit>
#include <limits>

#include "common/BinaryStream.h"

namespace ailo::texture {

namespace {

using binary::makeTag;
using binary::Reader;
using binary::Writer;

// Package layout: "ATEX" | u32 version | chunks... (see common/BinaryStream.h)
constexpr uint32_t kMagic     = makeTag("ATEX");
constexpr uint32_t kChunkInfo = makeTag("INFO");
constexpr uint32_t kChunkData = makeTag("DATA");

constexpr uint32_t kMaxDimension = 65536;

}

std::string_view toString(TextureType type) {
    switch (type) {
        case TextureType::Texture2D: return "2d";
        case TextureType::Cubemap:   return "cubemap";
    }
    return "?";
}

std::string_view toString(TextureFormat format) {
    switch (format) {
        case TextureFormat::RGBA8:      return "rgba8";
        case TextureFormat::RGBA8_SRGB: return "rgba8_srgb";
        case TextureFormat::RGBA32F:    return "rgba32f";
    }
    return "?";
}

uint32_t bytesPerPixel(TextureFormat format) {
    switch (format) {
        case TextureFormat::RGBA8:
        case TextureFormat::RGBA8_SRGB: return 4;
        case TextureFormat::RGBA32F:    return 16;
    }
    return 0;
}

uint32_t mipLevelCount(uint32_t width, uint32_t height) {
    return std::bit_width(std::max({ width, height, 1u }));
}

uint32_t TexturePackage::levelWidth(uint32_t level) const {
    return std::max(width >> level, 1u);
}

uint32_t TexturePackage::levelHeight(uint32_t level) const {
    return std::max(height >> level, 1u);
}

size_t TexturePackage::imageSize(uint32_t level) const {
    return size_t(levelWidth(level)) * levelHeight(level) * bytesPerPixel(format);
}

size_t TexturePackage::expectedDataSize() const {
    size_t size = 0;
    for (uint32_t level = 0; level < levels; level++) {
        size += imageSize(level) * layers();
    }
    return size;
}

std::span<const uint8_t> TexturePackage::image(uint32_t level, uint32_t layer) const {
    size_t offset = 0;
    for (uint32_t l = 0; l < level; l++) {
        offset += imageSize(l) * layers();
    }
    offset += imageSize(level) * layer;
    return std::span(data).subspan(offset, imageSize(level));
}

std::string TexturePackage::validate() const {
    if (width == 0 || height == 0) return "texture has no pixels";
    // Beyond any GPU's limit; also keeps the size arithmetic below from overflowing on a corrupt header.
    if (width > kMaxDimension || height > kMaxDimension) {
        return "texture is " + std::to_string(width) + "x" + std::to_string(height) + ", the limit is " +
               std::to_string(kMaxDimension);
    }
    if (type == TextureType::Cubemap && width != height) {
        return "cubemap faces must be square, got " + std::to_string(width) + "x" + std::to_string(height);
    }
    if (levels == 0 || levels > mipLevelCount(width, height)) {
        return std::to_string(levels) + " levels stored, a " + std::to_string(width) + "x" + std::to_string(height) +
               " texture has at most " + std::to_string(mipLevelCount(width, height));
    }
    if (generateMipmaps && levels != 1) {
        return "generateMipmaps needs exactly one stored level, got " + std::to_string(levels);
    }
    if (data.size() != expectedDataSize()) {
        return "texture data is " + std::to_string(data.size()) + " bytes, expected " +
               std::to_string(expectedDataSize());
    }
    if (data.size() > std::numeric_limits<uint32_t>::max() - 64) {
        return "texture data is larger than 4 GiB";
    }
    return {};
}

std::vector<uint8_t> TexturePackage::serialize() const {
    Writer w;
    w.u32(kMagic);
    w.u32(kTexturePackageVersion);

    w.beginChunk(kChunkInfo);
    w.u8(uint8_t(type));
    w.u8(uint8_t(format));
    w.u32(width);
    w.u32(height);
    w.u32(levels);
    w.boolean(generateMipmaps);
    w.endChunk();

    w.beginChunk(kChunkData);
    w.bytes(data.data(), data.size());
    w.endChunk();

    return w.take();
}

std::optional<TexturePackage> TexturePackage::deserialize(std::span<const uint8_t> data, std::string& error) {
    Reader header(data);
    if (header.u32() != kMagic || !header.ok()) {
        error = "not a texture package";
        return std::nullopt;
    }
    uint32_t version = header.u32();
    if (version != kTexturePackageVersion) {
        error = "texture package version " + std::to_string(version) + ", engine expects " +
                std::to_string(kTexturePackageVersion) + " (rebuild the assets)";
        return std::nullopt;
    }

    TexturePackage pkg;
    bool hasInfo = false;
    Reader r(data.subspan(2 * sizeof(uint32_t)));

    while (r.ok() && !r.atEnd()) {
        uint32_t tag = r.u32();
        uint32_t size = r.u32();
        auto payload = r.sub(size);
        if (!r.ok()) break;
        Reader c(payload);

        switch (tag) {
            case kChunkInfo:
                pkg.type = c.enumeration(TextureType::Cubemap);
                pkg.format = c.enumeration(TextureFormat::RGBA32F);
                pkg.width = c.u32();
                pkg.height = c.u32();
                pkg.levels = c.u32();
                pkg.generateMipmaps = c.boolean();
                hasInfo = true;
                break;

            case kChunkData: {
                auto bytes = c.sub(size);
                pkg.data.assign(bytes.begin(), bytes.end());
                break;
            }

            default:
                break;
        }

        if (!c.ok()) {
            error = "corrupt texture package";
            return std::nullopt;
        }
    }

    if (!r.ok()) {
        error = "truncated texture package";
        return std::nullopt;
    }
    if (!hasInfo) {
        error = "texture package has no INFO chunk";
        return std::nullopt;
    }

    error = pkg.validate();
    if (!error.empty()) {
        return std::nullopt;
    }
    return pkg;
}

}
