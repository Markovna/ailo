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

    gpu::Texture* getColorTarget() {
        if (!m_msaa.empty()) {
            return &m_msaa[m_currentImageIndex];
        }
        return &m_colors[m_currentImageIndex];
    }

    gpu::Texture* getResolveTarget() {
        if (!m_msaa.empty()) {
            return &m_colors[m_currentImageIndex];
        }
        return nullptr;
    }

    gpu::Texture* getDepthTarget() { return &*m_depth; }

private:
    vk::SwapchainKHR m_swapchain;
    std::vector<gpu::Texture> m_colors;
    std::optional<gpu::Texture> m_depth;
    std::vector<gpu::Texture> m_msaa;
    uint32_t m_currentImageIndex = 0;
    std::vector<vk::Semaphore> m_renderFinishedSemaphores;
};

}