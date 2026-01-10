#pragma once

#include <vector>
#include <vulkan/vulkan.hpp>
#include "RenderAPI.h"

namespace ailo {

class SwapChain {
public:
    SwapChain(RenderAPI& api, vk::SurfaceKHR surface, vk::PhysicalDevice physicalDevice, vk::Device device, vk::Extent2D extent, vk::SurfaceFormatKHR surfaceFormat, vk::PresentModeKHR presentMode);
    vk::Result acquireNextImage(vk::Device, vk::Semaphore& semaphore, uint64_t timeout);
    vk::Result present(CommandBuffer& commandBuffer, vk::Queue graphicsQueue, vk::Queue presentQueue);
    void destroy(vk::Device device);

    // TODO: replace with render target and frame buffer cache
    vk::Extent2D getExtent() { return { m_colors[0].width, m_colors[0].height }; }
    uint32_t getImageCount() { return m_colors.size(); }
    vk::ImageView getColorImage(uint32_t index) { return m_colors[index].imageView; }
    vk::ImageView getDepthImage() { return m_depth.imageView; }
    uint32_t getCurrentImageIndex() { return m_currentImageIndex; }
    vk::Format getColorFormat() { return m_colors[0].format; }

private:
    vk::SwapchainKHR m_swapchain;
    std::vector<gpu::Texture> m_colors;
    gpu::Texture m_depth;
    uint32_t m_currentImageIndex = 0;
    std::vector<vk::Semaphore> m_renderFinishedSemaphores;
};

}