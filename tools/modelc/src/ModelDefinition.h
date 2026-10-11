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
};

ModelDefinition parseModelDefinition(std::string_view source);

}
