#pragma once

#include <vector>
#include <vulkan/vulkan.hpp>
#include "RenderAPI.h"

namespace ailo {

class SwapChain {
public:
    SwapChain(VulkanDevice& device, ResourceContainer<gpu::Texture>& textures,
              ResourceContainer<gpu::RenderTarget>& renderTargets);
    vk::Result acquireNextImage(vk::Device, vk::Semaphore& semaphore, uint64_t timeout);
    vk::Result present(CommandBuffer& commandBuffer, vk::Queue graphicsQueue, vk::Queue presentQueue);
    void destroy(vk::Device device);

    const Shared<gpu::RenderTarget>& getCurrentRenderTarget() const { return m_renderTargets[m_currentImageIndex]; }

private:
    vk::SwapchainKHR m_swapchain;
    std::vector<Shared<gpu::RenderTarget>> m_renderTargets;
    uint32_t m_currentImageIndex = 0;
    std::vector<vk::Semaphore> m_renderFinishedSemaphores;
};

}