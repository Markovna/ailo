#pragma once

#include <vulkan/vulkan_core.h>
#include "render/RenderAPI.h"

namespace ailo::vkutils {

vk::CullModeFlags getCullMode(CullingMode mode);
vk::BlendOp getBlendOp(BlendOperation);
vk::BlendFactor getBlendFunction(BlendFunction);
vk::CompareOp getCompareOperation(CompareOp);

}
