#include "VulkanUtils.h"

namespace ailo::vkutils {

vk::CullModeFlags getCullMode(CullingMode mode) {
  switch (mode) {
    case CullingMode::NONE: return vk::CullModeFlagBits::eNone;
    case CullingMode::FRONT: return vk::CullModeFlagBits::eFront;
    case CullingMode::BACK: return vk::CullModeFlagBits::eBack;
    case CullingMode::FRONT_AND_BACK: return vk::CullModeFlagBits::eFrontAndBack;
  }
}

vk::BlendOp getBlendOp(BlendOperation blendOperation) {
  return static_cast<vk::BlendOp>(blendOperation);
}

vk::BlendFactor getBlendFunction(BlendFunction blendFunction) {
  switch (blendFunction) {
    case BlendFunction::ZERO: return vk::BlendFactor::eZero;
    case BlendFunction::ONE: return vk::BlendFactor::eOne;
    case BlendFunction::SRC_COLOR: return vk::BlendFactor::eSrcColor;
    case BlendFunction::ONE_MINUS_SRC_COLOR: return vk::BlendFactor::eOneMinusSrcColor;
    case BlendFunction::DST_COLOR: return vk::BlendFactor::eDstColor;
    case BlendFunction::ONE_MINUS_DST_COLOR: return vk::BlendFactor::eOneMinusDstColor;
    case BlendFunction::SRC_ALPHA: return vk::BlendFactor::eSrcAlpha;
    case BlendFunction::ONE_MINUS_SRC_ALPHA: return vk::BlendFactor::eOneMinusSrcAlpha;
    case BlendFunction::DST_ALPHA: return vk::BlendFactor::eDstAlpha;
    case BlendFunction::ONE_MINUS_DST_ALPHA: return vk::BlendFactor::eOneMinusDstAlpha;
    case BlendFunction::SRC_ALPHA_SATURATE: return vk::BlendFactor::eSrcAlphaSaturate;
  }
}

vk::CompareOp getCompareOperation(CompareOp compOp) {
  return static_cast<vk::CompareOp>(compOp);
}

vk::BufferUsageFlagBits getBufferUsage(BufferBinding binding) {
  switch(binding) {
    case BufferBinding::INDEX: return vk::BufferUsageFlagBits::eIndexBuffer;
    case BufferBinding::VERTEX: return vk::BufferUsageFlagBits::eVertexBuffer;
    case BufferBinding::UNIFORM: return vk::BufferUsageFlagBits::eUniformBuffer;
    case BufferBinding::UNKNOWN: return static_cast<vk::BufferUsageFlagBits>(0);
  }
  return static_cast<vk::BufferUsageFlagBits>(0);
}

}