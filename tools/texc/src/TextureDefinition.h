#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "render/texture/TexturePacker.h"

namespace texc {

struct DefinitionError : std::runtime_error {
    int line;
    DefinitionError(int line, const std::string& message) : std::runtime_error(message), line(line) {}
};

struct TextureDefinition {
    ailo::texture::TextureType type = ailo::texture::TextureType::Texture2D;
    // One image for a 2d texture, six faces (+x -x +y -y +z -z) for a cubemap; relative to the .tex file.
    std::vector<std::filesystem::path> sources;
    ailo::texture::TextureImportOptions options;
};

TextureDefinition parseTextureDefinition(std::string_view source);

}
