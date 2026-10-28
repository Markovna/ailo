#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "render/material/MaterialPackage.h"

namespace ailo::json {
struct Value;
}

namespace matc {

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
    // The vertex code writes material.clipPosition, replacing the projected world position.
    bool hasClipPosition = false;
    // The fragment code provides surfaceShading(), called per light instead of the lit BRDF.
    bool customSurfaceShading = false;
    int customSurfaceShadingLine = 0;
};

MaterialDefinition parseMaterialDefinition(std::string_view source);

// A value of the parameter's type, as a number or array of numbers (bools for bool types), one word per component:
// componentCount(type) words per array element, matrices column-major. `what` names the value in errors.
std::vector<uint32_t> parseParameterValue(const ailo::material::MaterialParameter& p, const ailo::json::Value& value,
                                          const std::string& what);

}
