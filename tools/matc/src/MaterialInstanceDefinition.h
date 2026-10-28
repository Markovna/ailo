#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "MaterialDefinition.h"
#include "render/material/MaterialInstancePackage.h"

namespace matc {

struct MaterialInstanceDefinition {
    // Paths as written in the source.
    ailo::material::MaterialInstancePackage package;
    int materialLine = 0;
    std::vector<int> textureLines;      // per package.textures entry
};

// Returns the parsed definition of the material at `path` (the "material" property); `line` is for errors.
using MaterialResolver = std::function<MaterialDefinition(const std::string& path, int line)>;

MaterialInstanceDefinition parseMaterialInstanceDefinition(std::string_view source, const MaterialResolver& resolveMaterial);

}
