#include "MaterialEnums.h"

namespace ailo::material {

std::string_view toString(ShadingModel v) {
    switch (v) {
        case ShadingModel::Lit:   return "lit";
        case ShadingModel::Unlit: return "unlit";
    }
    return "?";
}

std::string_view toString(BlendingMode v) {
    switch (v) {
        case BlendingMode::Opaque: return "opaque";
    }
    return "?";
}

std::string_view toString(VertexDomain v) {
    switch (v) {
        case VertexDomain::Object: return "object";
    }
    return "?";
}

std::string_view toString(CullingMode v) {
    switch (v) {
        case CullingMode::None:         return "none";
        case CullingMode::Front:        return "front";
        case CullingMode::Back:         return "back";
        case CullingMode::FrontAndBack: return "frontAndBack";
    }
    return "?";
}

std::string_view toString(DepthFunc v) {
    switch (v) {
        case DepthFunc::Never:        return "never";
        case DepthFunc::Less:         return "less";
        case DepthFunc::Equal:        return "equal";
        case DepthFunc::LessEqual:    return "lessEqual";
        case DepthFunc::Greater:      return "greater";
        case DepthFunc::NotEqual:     return "notEqual";
        case DepthFunc::GreaterEqual: return "greaterEqual";
        case DepthFunc::Always:       return "always";
    }
    return "?";
}

std::string_view toString(ShaderStage v) {
    switch (v) {
        case ShaderStage::Vertex:   return "vertex";
        case ShaderStage::Fragment: return "fragment";
    }
    return "?";
}

std::string_view toString(ParameterType v) {
    switch (v) {
        case ParameterType::Bool:           return "bool";
        case ParameterType::Bool2:          return "bool2";
        case ParameterType::Bool3:          return "bool3";
        case ParameterType::Bool4:          return "bool4";
        case ParameterType::Float:          return "float";
        case ParameterType::Float2:         return "float2";
        case ParameterType::Float3:         return "float3";
        case ParameterType::Float4:         return "float4";
        case ParameterType::Int:            return "int";
        case ParameterType::Int2:           return "int2";
        case ParameterType::Int3:           return "int3";
        case ParameterType::Int4:           return "int4";
        case ParameterType::UInt:           return "uint";
        case ParameterType::UInt2:          return "uint2";
        case ParameterType::UInt3:          return "uint3";
        case ParameterType::UInt4:          return "uint4";
        case ParameterType::Float3x3:       return "float3x3";
        case ParameterType::Float4x4:       return "float4x4";
        case ParameterType::Sampler2D:      return "sampler2d";
        case ParameterType::SamplerCubemap: return "samplerCubemap";
    }
    return "?";
}

std::string_view toString(SamplerDefault v) {
    switch (v) {
        case SamplerDefault::White:  return "white";
        case SamplerDefault::Black:  return "black";
        case SamplerDefault::Normal: return "normal";
    }
    return "?";
}

}
