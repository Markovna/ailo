#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

#include "TexturePackage.h"

namespace ailo::texture {

enum class MipmapMode : uint8_t {
    None,
    Runtime,    // only level 0 is stored, the engine generates the chain at load time
    Offline,    // the whole chain is generated here and stored
};

struct TextureImportOptions {
    bool srgb = true;
    MipmapMode mipmaps = MipmapMode::Runtime;
};

enum class PixelLayout : uint8_t {
    RGBA8,
    BGRA8,
};

std::optional<TexturePackage> packTexture(const std::filesystem::path& source, const TextureImportOptions&, std::string& error);

std::optional<TexturePackage> packTextureFromMemory(std::span<const uint8_t> encoded, const TextureImportOptions&, std::string& error);

std::optional<TexturePackage> packTextureFromPixels(const void* pixels, uint32_t width, uint32_t height, PixelLayout,
                                                    const TextureImportOptions&, std::string& error);

std::optional<TexturePackage> packCubemap(std::span<const std::filesystem::path, 6> faces, const TextureImportOptions&,
                                          std::string& error);

// Resamples an equirectangular image into a cubemap with faces of faceSize (0: a quarter of the image's width).
std::optional<TexturePackage> packCubemapFromEquirect(const std::filesystem::path& source, uint32_t faceSize,
                                                      const TextureImportOptions&, std::string& error);

// Box-filters the full mip chain of every layer from level 0 and stores it; rgba8_srgb is filtered in linear space.
// The package must store exactly one level and not ask the engine to generate mipmaps.
bool generateMipLevels(TexturePackage&, std::string& error);

std::array<std::filesystem::path, 6> cubemapFacePaths(const std::filesystem::path& path);

}
