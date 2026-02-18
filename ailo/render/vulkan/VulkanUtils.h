#pragma once

#include <vulkan/vulkan_core.h>
#include "render/RenderAPI.h"

namespace ailo::vkutils {

vk::CullModeFlags getCullMode(CullingMode mode);
vk::BlendOp getBlendOp(BlendOperation);
vk::BlendFactor getBlendFunction(BlendFunction);
vk::CompareOp getCompareOperation(CompareOp);
vk::BufferUsageFlagBits getBufferUsage(BufferBinding);
vk::ImageUsageFlags getTextureUsage(TextureUsage);

std::tuple<vk::AccessFlags, vk::PipelineStageFlags> getTransitionSrcAccess(vk::ImageLayout);
std::tuple<vk::AccessFlags, vk::PipelineStageFlags> getTransitionDstAccess(vk::ImageLayout);

}
