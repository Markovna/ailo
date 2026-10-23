#pragma once

// Included by the offline tools, so it must not include Vulkan, VMA or any other engine header.

#include <cstdint>
#include <string_view>

namespace ailo {

enum class BufferBinding : uint8_t {
  UNKNOWN,
  VERTEX,
  INDEX,
  UNIFORM,
};

enum class IndexType : uint8_t {
  UINT16,
  UINT32,
};

enum class CullingMode : uint8_t {
    None,
    Front,
    Back,
    FrontAndBack,
};

enum class BlendOperation : uint8_t {
  ADD,
  SUBTRACT,
  REVERSE_SUBTRACT,
  MIN,
  MAX
};

enum class BlendFunction : uint8_t {
  ZERO,
  ONE,
  SRC_COLOR,
  ONE_MINUS_SRC_COLOR,
  DST_COLOR,
  ONE_MINUS_DST_COLOR,
  SRC_ALPHA,
  ONE_MINUS_SRC_ALPHA,
  DST_ALPHA,
  ONE_MINUS_DST_ALPHA,
  SRC_ALPHA_SATURATE
};

// Same order as VkCompareOp.
enum class CompareOp : uint8_t {
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always,
};

enum class SamplerFilter : uint8_t {
  NEAREST,
  LINEAR
};

enum class SamplerMipmapMode : uint8_t {
  NEAREST,
  LINEAR
};

enum class SamplerWrapMode : uint8_t {
  REPEAT,
  MIRRORED_REPEAT,
  CLAMP_TO_EDGE,
  CLAMP_TO_BORDER
};

enum class SamplerCompareMode : uint8_t {
  NONE,
  COMPARE_TO_TEXTURE
};

struct SamplerParams {
  SamplerFilter filterMag = SamplerFilter::LINEAR;
  SamplerFilter filterMin = SamplerFilter::LINEAR;
  SamplerMipmapMode mipmapMode = SamplerMipmapMode::LINEAR;
  SamplerWrapMode wrapS = SamplerWrapMode::REPEAT;
  SamplerWrapMode wrapT = SamplerWrapMode::REPEAT;
  SamplerWrapMode wrapR = SamplerWrapMode::REPEAT;
  SamplerCompareMode compareMode = SamplerCompareMode::NONE;
  CompareOp compareFunc = CompareOp::Always;

  bool operator==(const SamplerParams& other) const = default;
};

enum class TextureType : uint8_t {
    Texture2D,
    Cubemap,
};

enum class TextureUsage : uint16_t {
    None = 0,
    Sampled = 1 << 0,
    Storage = 1 << 1,
    ColorAttachment = 1 << 2,
    DepthStencilAttachment = 1 << 3,
};

inline TextureUsage operator&(TextureUsage lhs, TextureUsage rhs) {
    return static_cast<TextureUsage>(static_cast<uint16_t>(lhs) & static_cast<uint16_t>(rhs));
}
inline TextureUsage operator|(TextureUsage lhs, TextureUsage rhs) {
    return static_cast<TextureUsage>(static_cast<uint16_t>(lhs) | static_cast<uint16_t>(rhs));
}

constexpr std::string_view toString(CullingMode mode) {
    switch (mode) {
        case CullingMode::None:         return "none";
        case CullingMode::Front:        return "front";
        case CullingMode::Back:         return "back";
        case CullingMode::FrontAndBack: return "frontAndBack";
    }
    return "?";
}

constexpr std::string_view toString(CompareOp op) {
    switch (op) {
        case CompareOp::Never:        return "never";
        case CompareOp::Less:         return "less";
        case CompareOp::Equal:        return "equal";
        case CompareOp::LessEqual:    return "lessEqual";
        case CompareOp::Greater:      return "greater";
        case CompareOp::NotEqual:     return "notEqual";
        case CompareOp::GreaterEqual: return "greaterEqual";
        case CompareOp::Always:       return "always";
    }
    return "?";
}

constexpr std::string_view toString(TextureType type) {
    switch (type) {
        case TextureType::Texture2D: return "2d";
        case TextureType::Cubemap:   return "cubemap";
    }
    return "?";
}

}
