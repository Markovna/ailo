#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

#include "TexturePackage.h"

namespace ailo::texture {

struct TextureImportOptions {
    bool srgb = true;
    bool mipmaps = true;
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

std::array<std::filesystem::path, 6> cubemapFacePaths(const std::filesystem::path& path);

}
