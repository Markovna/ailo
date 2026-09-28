#include "VulkanUtils.h"

namespace ailo::vkutils {

vk::CullModeFlags getCullMode(CullingMode mode) {
  switch (mode) {
    case CullingMode::NONE: return vk::CullModeFlagBits::eNone;
    case CullingMode::FRONT: return vk::CullModeFlagBits::eFront;
    case CullingMode::BACK: return vk::CullModeFlagBits::eBack;
    case CullingMode::FRONT_AND_BACK: return vk::CullModeFlagBits::eFrontAndBack;
  }
  return vk::CullModeFlagBits::eNone;
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
  return vk::BlendFactor::eZero;
}

vk::CompareOp getCompareOperation(CompareOp compOp) {
  return static_cast<vk::CompareOp>(compOp);
}

vk::Filter getFilter(SamplerFilter filter) {
  switch (filter) {
    case SamplerFilter::NEAREST: return vk::Filter::eNearest;
    case SamplerFilter::LINEAR: return vk::Filter::eLinear;
  }
  return vk::Filter::eLinear;
}

vk::SamplerMipmapMode getMipmapMode(SamplerMipmapMode mode) {
  switch (mode) {
    case SamplerMipmapMode::NEAREST: return vk::SamplerMipmapMode::eNearest;
    case SamplerMipmapMode::LINEAR: return vk::SamplerMipmapMode::eLinear;
  }
  return vk::SamplerMipmapMode::eLinear;
}

vk::SamplerAddressMode getWrapMode(SamplerWrapMode mode) {
  switch (mode) {
    case SamplerWrapMode::REPEAT: return vk::SamplerAddressMode::eRepeat;
    case SamplerWrapMode::MIRRORED_REPEAT: return vk::SamplerAddressMode::eMirroredRepeat;
    case SamplerWrapMode::CLAMP_TO_EDGE: return vk::SamplerAddressMode::eClampToEdge;
    case SamplerWrapMode::CLAMP_TO_BORDER: return vk::SamplerAddressMode::eClampToBorder;
  }
  return vk::SamplerAddressMode::eRepeat;
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

vk::ImageUsageFlags getTextureUsage(TextureUsage usage) {
  vk::ImageUsageFlags usageFlags;
  if ((usage & TextureUsage::Sampled) == TextureUsage::Sampled ) usageFlags |= vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
  if ((usage & TextureUsage::Storage) == TextureUsage::Storage) usageFlags |= vk::ImageUsageFlagBits::eStorage;
  if ((usage & TextureUsage::ColorAttachment) == TextureUsage::ColorAttachment) usageFlags |= vk::ImageUsageFlagBits::eColorAttachment;
  if ((usage & TextureUsage::DepthStencilAttachment) == TextureUsage::DepthStencilAttachment) usageFlags |= vk::ImageUsageFlagBits::eDepthStencilAttachment;

  return usageFlags;
}

std::tuple<vk::AccessFlags, vk::PipelineStageFlags> getTransitionSrcAccess(vk::ImageLayout layout) {
  switch (layout) {
    case vk::ImageLayout::eUndefined:
      return { vk::AccessFlagBits::eMemoryRead, vk::PipelineStageFlagBits::eAllGraphics };
    case vk::ImageLayout::eColorAttachmentOptimal:
      return { vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite, vk::PipelineStageFlagBits::eColorAttachmentOutput };
    case vk::ImageLayout::eDepthStencilAttachmentOptimal:
      return { vk::AccessFlagBits::eDepthStencilAttachmentWrite, vk::PipelineStageFlagBits::eLateFragmentTests };
    case vk::ImageLayout::eTransferSrcOptimal:
      return { vk::AccessFlagBits::eTransferRead, vk::PipelineStageFlagBits::eTransfer };
    case vk::ImageLayout::eTransferDstOptimal:
      return { vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eTransfer };
    case vk::ImageLayout::ePresentSrcKHR:
      // Must match the acquire semaphore's wait stage so the layout transition waits for the acquire.
      return { vk::AccessFlagBits::eNone, vk::PipelineStageFlagBits::eColorAttachmentOutput };
    case vk::ImageLayout::eShaderReadOnlyOptimal:
      return { vk::AccessFlagBits::eNone, vk::PipelineStageFlagBits::eFragmentShader };
    default: return { vk::AccessFlagBits::eNone, vk::PipelineStageFlagBits::eNone };
  }
}

std::tuple<vk::AccessFlags, vk::PipelineStageFlags> getTransitionDstAccess(vk::ImageLayout layout) {
  switch (layout) {
    case vk::ImageLayout::eColorAttachmentOptimal:
      return { vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite, vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eFragmentShader };
    case vk::ImageLayout::eDepthStencilAttachmentOptimal:
      return { vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite, vk::PipelineStageFlagBits::eEarlyFragmentTests };
    case vk::ImageLayout::eTransferSrcOptimal:
      return { vk::AccessFlagBits::eTransferRead, vk::PipelineStageFlagBits::eTransfer };
    case vk::ImageLayout::eTransferDstOptimal:
      return { vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eTransfer };
    case vk::ImageLayout::eShaderReadOnlyOptimal:
      return { vk::AccessFlagBits::eShaderRead, vk::PipelineStageFlagBits::eFragmentShader };
    case vk::ImageLayout::ePresentSrcKHR:
    case vk::ImageLayout::eUndefined:
      return { vk::AccessFlagBits::eNone, vk::PipelineStageFlagBits::eTopOfPipe };
    default: return { vk::AccessFlagBits::eNone, vk::PipelineStageFlagBits::eNone };
  }
}
}
