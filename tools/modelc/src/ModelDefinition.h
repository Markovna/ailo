#pragma once

#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

namespace modelc {

struct DefinitionError : std::runtime_error {
    int line;
    DefinitionError(int line, const std::string& message) : std::runtime_error(message), line(line) {}
};

struct ModelDefinition {
    std::filesystem::path source;                   // relative to the .model file, or to the assets folder if it starts with /
    std::string material;                           // asset key of the material every mesh uses by default
    std::map<std::string, std::string> materials;   // source material name -> asset key, overrides `material`
    std::map<std::string, std::string> nodeMaterials; // source node name -> asset key for its meshes, overrides both
    std::map<std::string, std::filesystem::path> textures;  // source texture URI -> path replacing it, relative like `source`
    std::map<std::string, std::filesystem::path> animations; // clip name -> file whose animations are added, relative like `source`
    bool optimize = true;                           // reorder meshes for the vertex cache, overdraw and vertex fetch
};

ModelDefinition parseModelDefinition(std::string_view source);

}
