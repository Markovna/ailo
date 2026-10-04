#pragma once

#include <cstdint>
#include <string_view>

namespace ailo::material {

enum class ShadingModel : uint8_t {
    Lit,
    Unlit,
};

enum class BlendingMode : uint8_t {
    Opaque,
};

enum class VertexDomain : uint8_t {
    Object,
};

enum class CullingMode : uint8_t {
    None,
    Front,
    Back,
    FrontAndBack,
};

enum class DepthFunc : uint8_t {
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always,
};

// Position is always present. Lit materials always get Normal, Tangents implies Normal.
enum class VertexAttribute : uint8_t {
    Color    = 1 << 0,
    UV0      = 1 << 1,
    Tangents = 1 << 2,
    Normal   = 1 << 3,
};

enum class ShaderStage : uint8_t {
    Vertex,
    Fragment,
};

enum class ParameterType : uint8_t {
    Bool, Bool2, Bool3, Bool4,
    Float, Float2, Float3, Float4,
    Int, Int2, Int3, Int4,
    UInt, UInt2, UInt3, UInt4,
    Float3x3, Float4x4,
    Sampler2D,
    SamplerCubemap,
};

enum class SamplerDefault : uint8_t {
    White,
    Black,
    Normal,
};

constexpr bool isSampler(ParameterType type) {
    return type == ParameterType::Sampler2D || type == ParameterType::SamplerCubemap;
}

constexpr uint32_t componentCount(ParameterType type) {
    switch (type) {
        case ParameterType::Bool:  case ParameterType::Float:  case ParameterType::Int:  case ParameterType::UInt:  return 1;
        case ParameterType::Bool2: case ParameterType::Float2: case ParameterType::Int2: case ParameterType::UInt2: return 2;
        case ParameterType::Bool3: case ParameterType::Float3: case ParameterType::Int3: case ParameterType::UInt3: return 3;
        case ParameterType::Bool4: case ParameterType::Float4: case ParameterType::Int4: case ParameterType::UInt4: return 4;
        case ParameterType::Float3x3: return 9;
        case ParameterType::Float4x4: return 16;
        default: return 0;
    }
}

std::string_view toString(ShadingModel);
std::string_view toString(BlendingMode);
std::string_view toString(VertexDomain);
std::string_view toString(CullingMode);
std::string_view toString(DepthFunc);
std::string_view toString(ShaderStage);
std::string_view toString(ParameterType);
std::string_view toString(SamplerDefault);

}
