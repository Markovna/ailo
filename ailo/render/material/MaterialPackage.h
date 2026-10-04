#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "MaterialEnums.h"
#include "Variant.h"

namespace ailo::material {

// Bump whenever the binary layout or the meaning of any field changes; packages with another version are rejected.
static constexpr uint32_t kMaterialPackageVersion = 1;

// Descriptor set 2 (PER_MATERIAL) layout: binding 0 is the MaterialParams uniform block (if the material
// has uniform parameters), samplers follow from binding 1 in declaration order.
static constexpr uint32_t kMaterialParamsBinding = 0;
static constexpr uint32_t kFirstSamplerBinding = 1;

static constexpr uint32_t kMaxVariables = 4;

struct RasterState {
    CullingMode culling = CullingMode::Back;
    bool colorWrite = true;
    bool depthWrite = true;
    bool depthCulling = true;
    bool doubleSided = false;
};

struct MaterialParameter {
    std::string name;
    ParameterType type = ParameterType::Float;
    uint32_t arraySize = 0;         // 0: not an array

    uint32_t offset = 0;
    uint32_t size = 0;
    std::vector<uint8_t> defaultValue;

    uint32_t binding = 0;
    SamplerDefault samplerDefault = SamplerDefault::White;
};

struct ShaderEntry {
    ShaderStage stage;
    Variant variant;                // already filtered for the stage (Variant::filterVertex/filterFragment)
    uint32_t blobIndex;
};

struct MaterialPackage {
    std::string name;
    ShadingModel shadingModel = ShadingModel::Lit;
    BlendingMode blending = BlendingMode::Opaque;
    VertexDomain vertexDomain = VertexDomain::Object;
    RasterState raster;
    uint8_t requiredAttributes = 0;         // VertexAttribute bits
    std::vector<std::string> variables;
    uint8_t variantFilter = 0;

    std::vector<MaterialParameter> parameters;
    uint32_t uniformBlockSize = 0;          // 0: no MaterialParams block

    std::vector<ShaderEntry> shaders;
    std::vector<std::vector<uint32_t>> blobs;

    const MaterialParameter* findParameter(std::string_view name) const;

    // Takes the full variant and filters it for the stage; empty if the material doesn't have it.
    std::span<const uint32_t> getShader(ShaderStage stage, Variant variant) const;

    std::vector<uint8_t> serialize() const;

    static std::optional<MaterialPackage> deserialize(std::span<const uint8_t> data, std::string& error);
};

}
