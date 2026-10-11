#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

#include "Ibl.h"

namespace lightc {

struct DefinitionError : std::runtime_error {
    int line;
    DefinitionError(int line, const std::string& message) : std::runtime_error(message), line(line) {}
};

struct LightDefinition {
    // Equirectangular HDR image, relative to the .light file.
    std::filesystem::path source;
    PrefilterOptions prefilter;
};

LightDefinition parseLightDefinition(std::string_view source);

}
