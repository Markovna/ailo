#pragma once

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "TextureFormats.h"
#include "render/RenderApiEnums.h"
#include "render/texture/TexturePacker.h"

namespace texc {

struct DefinitionError : std::runtime_error {
    int line;
    DefinitionError(int line, const std::string& message) : std::runtime_error(message), line(line) {}
};

struct TextureDefinition {
    ailo::TextureType type = ailo::TextureType::Texture2D;
    // One image for a 2d texture, six faces (+x -x +y -y +z -z) for a cubemap; relative to the .tex file.
    std::vector<std::filesystem::path> sources;
    // Cubemap only: sources[0] is one equirectangular image, resampled into faces of faceSize (0: its width / 4).
    bool equirectangular = false;
    uint32_t faceSize = 0;
    // Unset: hdr for an HDR source, color otherwise (resolveSettings).
    std::optional<Usage> usage;
    Quality quality = Quality::Normal;
    // Unset: whether any texel's alpha is below 255. Color textures only.
    std::optional<bool> alpha;
    // Overrides the target's format table; its sRGB variant is picked from `srgb`.
    std::optional<ailo::TextureFormat> format;
    // Unset: true. Color textures only.
    std::optional<bool> srgb;
    // options.srgb: color textures that are sRGB.
    ailo::texture::TextureImportOptions options;
};

TextureDefinition parseTextureDefinition(std::string_view source);

// Fills in the defaults that depend on the source (whether it is HDR) and checks the settings against each other.
// Throws DefinitionError with line 0.
void resolveSettings(TextureDefinition& def, bool hdrSource);

}
