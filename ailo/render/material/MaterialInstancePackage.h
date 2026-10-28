#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "MaterialEnums.h"

namespace ailo::material {

// Bump whenever the binary layout or the meaning of any field changes; packages with another version are rejected.
static constexpr uint32_t kMaterialInstancePackageVersion = 1;

struct InstanceParameter {
    std::string name;
    ParameterType type = ParameterType::Float;
    // componentCount(type) words per array element, tightly packed; matrices are column-major.
    std::vector<uint32_t> value;
};

struct InstanceTexture {
    std::string name;
    std::string texture;
};

// Compiled by matc from a .matinst file. The material and textures are full asset keys: "<source>:/<path>", or
// relative to the assets folder.
struct MaterialInstancePackage {
    std::string material;
    std::vector<InstanceParameter> parameters;
    std::vector<InstanceTexture> textures;

    std::vector<uint8_t> serialize() const;

    static std::optional<MaterialInstancePackage> deserialize(std::span<const uint8_t> data, std::string& error);
};

}
