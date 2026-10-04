#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

#include "render/material/MaterialPackage.h"

namespace matcomp {

struct DefinitionError : std::runtime_error {
    int line;
    DefinitionError(int line, const std::string& message) : std::runtime_error(message), line(line) {}
};

struct CodeBlock {
    std::string code;
    int line = 0;           // line of the first character of code in the .mat file
    bool present = false;
};

struct MaterialDefinition {
    // Everything except shaders and blobs.
    ailo::material::MaterialPackage package;

    CodeBlock vertex;
    CodeBlock fragment;

    bool hasNormal = false;
};

MaterialDefinition parseMaterialDefinition(std::string_view source);

}
