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

enum class TextureFormat : uint8_t {
    RGBA8,
    RGBA8_SRGB,
    RGBA32F,
    RGBA16F,
    // Block-compressed, 4x4 texel blocks.
    BC1,        // rgb, opaque
    BC1_SRGB,
    BC3,        // rgba
    BC3_SRGB,
    BC4,        // r
    BC5,        // rg
    BC6H,       // rgb half float, unsigned
    BC7,        // rgba
    BC7_SRGB,
    // Texture packages store the values above, so new formats go at the end.
    R8,
    DEPTH32F,
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

constexpr std::string_view toString(TextureFormat format) {
    switch (format) {
        case TextureFormat::RGBA8:      return "rgba8";
        case TextureFormat::RGBA8_SRGB: return "rgba8_srgb";
        case TextureFormat::RGBA32F:    return "rgba32f";
        case TextureFormat::RGBA16F:    return "rgba16f";
        case TextureFormat::BC1:        return "bc1";
        case TextureFormat::BC1_SRGB:   return "bc1_srgb";
        case TextureFormat::BC3:        return "bc3";
        case TextureFormat::BC3_SRGB:   return "bc3_srgb";
        case TextureFormat::BC4:        return "bc4";
        case TextureFormat::BC5:        return "bc5";
        case TextureFormat::BC6H:       return "bc6h";
        case TextureFormat::BC7:        return "bc7";
        case TextureFormat::BC7_SRGB:   return "bc7_srgb";
        case TextureFormat::R8:         return "r8";
        case TextureFormat::DEPTH32F:   return "depth32f";
    }
    return "?";
}

// Texels per block side: 1 for uncompressed formats, 4 for block-compressed ones.
constexpr uint32_t blockExtent(TextureFormat format) {
    switch (format) {
        case TextureFormat::RGBA8:
        case TextureFormat::RGBA8_SRGB:
        case TextureFormat::RGBA32F:
        case TextureFormat::RGBA16F:
        case TextureFormat::R8:
        case TextureFormat::DEPTH32F: return 1;
        default:                      return 4;
    }
}

// Bytes per texel for uncompressed formats, per block for compressed ones.
constexpr uint32_t blockBytes(TextureFormat format) {
    switch (format) {
        case TextureFormat::R8:         return 1;
        case TextureFormat::RGBA8:
        case TextureFormat::RGBA8_SRGB:
        case TextureFormat::DEPTH32F:   return 4;
        case TextureFormat::RGBA32F:    return 16;
        case TextureFormat::RGBA16F:    return 8;
        case TextureFormat::BC1:
        case TextureFormat::BC1_SRGB:
        case TextureFormat::BC4:        return 8;
        case TextureFormat::BC3:
        case TextureFormat::BC3_SRGB:
        case TextureFormat::BC5:
        case TextureFormat::BC6H:
        case TextureFormat::BC7:
        case TextureFormat::BC7_SRGB:   return 16;
    }
    return 0;
}

constexpr bool isCompressed(TextureFormat format) {
    return blockExtent(format) > 1;
}

constexpr bool isSrgb(TextureFormat format) {
    switch (format) {
        case TextureFormat::RGBA8_SRGB:
        case TextureFormat::BC1_SRGB:
        case TextureFormat::BC3_SRGB:
        case TextureFormat::BC7_SRGB: return true;
        default:                      return false;
    }
}

constexpr bool isDepth(TextureFormat format) {
    return format == TextureFormat::DEPTH32F;
}

constexpr std::string_view toString(TextureType type) {
    switch (type) {
        case TextureType::Texture2D: return "2d";
        case TextureType::Cubemap:   return "cubemap";
    }
    return "?";
}

}
