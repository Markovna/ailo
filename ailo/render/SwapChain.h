#pragma once

#include <vector>
#include <vulkan/vulkan.hpp>
#include "RenderAPI.h"

namespace ailo {

class SwapChain {
public:
    explicit SwapChain(VulkanDevice&);
    vk::Result acquireNextImage(vk::Device, vk::Semaphore& semaphore, uint64_t timeout);
    vk::Result present(CommandBuffer& commandBuffer, vk::Queue graphicsQueue, vk::Queue presentQueue);
    void destroy(vk::Device device);

    vk::ImageView getCurrentImage() { return m_colors[m_currentImageIndex].imageView; }

    // TODO: replace with render target and frame buffer cache
    vk::Extent2D getExtent() { return { m_colors[0].width, m_colors[0].height }; }
    vk::ImageView getDepthImage() { return m_depth ? m_depth->imageView : vk::ImageView{}; }
    vk::Format getColorFormat() { return m_colors[0].format; }

private:
    vk::SwapchainKHR m_swapchain;
    std::vector<gpu::Texture> m_colors;
    std::optional<gpu::Texture> m_depth;
    uint32_t m_currentImageIndex = 0;
    std::vector<vk::Semaphore> m_renderFinishedSemaphores;
};

}