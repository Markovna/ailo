#pragma once

#include <vulkan/vulkan_core.h>
#include "render/RenderAPI.h"

namespace ailo::vkutils {

vk::CullModeFlags getCullMode(CullingMode mode);
vk::BlendOp getBlendOp(BlendOperation);
vk::BlendFactor getBlendFunction(BlendFunction);
vk::CompareOp getCompareOperation(CompareOp);
vk::BufferUsageFlagBits getBufferUsage(BufferBinding);

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;
    bool isComplete() const { return graphicsFamily.has_value() && presentFamily.has_value(); }
};

QueueFamilyIndices findQueueFamilies(vk::PhysicalDevice device, vk::SurfaceKHR surface);
vk::Format findSupportedFormat(vk::PhysicalDevice device, const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features);

}
