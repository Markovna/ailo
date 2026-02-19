#pragma once

#include <vector>
#include <vulkan/vulkan.hpp>
#include "RenderAPI.h"

namespace ailo {

class SwapChain {
public:
    explicit SwapChain(VulkanDevice&, ResourceContainer<gpu::Texture>&);
    vk::Result acquireNextImage(vk::Device, vk::Semaphore& semaphore, uint64_t timeout);
    vk::Result present(CommandBuffer& commandBuffer, vk::Queue graphicsQueue, vk::Queue presentQueue);
    void destroy(vk::Device device);

    resource_ptr<gpu::Texture> getColorTarget() {
        if (!m_msaa.empty()) {
            return m_msaa[m_currentImageIndex];
        }
        return m_colors[m_currentImageIndex];
    }

    resource_ptr<gpu::Texture> getResolveTarget() {
        if (!m_msaa.empty()) {
            return m_colors[m_currentImageIndex];
        }
        return {};
    }

    resource_ptr<gpu::Texture> getDepthTarget() { return m_depth; }

private:
    vk::SwapchainKHR m_swapchain;
    std::vector<resource_ptr<gpu::Texture>> m_colors;
    std::vector<resource_ptr<gpu::Texture>> m_msaa;
    resource_ptr<gpu::Texture> m_depth;
    uint32_t m_currentImageIndex = 0;
    std::vector<vk::Semaphore> m_renderFinishedSemaphores;
};

}