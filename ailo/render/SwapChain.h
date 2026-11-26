#pragma once

#include <vector>
#include <vulkan/vulkan.hpp>

#include "CommandBuffer.h"
#include "FrameBufferCache.h"
#include "ResourceContainer.h"
#include "VulkanDevice.h"
#include "vulkan/Resources.h"
#include "vulkan/Texture.h"

namespace ailo {

struct NativeWindow;

namespace gpu {

class SwapChain : public Resource {
public:
    SwapChain(VulkanDevice& device, FrameBufferCache& framebufferCache, const NativeWindow& window,
              ResourceContainer<Texture>& textures, ResourceContainer<RenderTarget>& renderTargets);
    ~SwapChain();

    vk::Result acquireNextImage(vk::Semaphore semaphore, uint64_t timeout);
    vk::Result present(CommandBuffer& commandBuffer, vk::Queue queue);

    const Shared<RenderTarget>& getCurrentRenderTarget() const { return m_renderTargets[m_currentImageIndex]; }

private:
    vk::SurfaceFormatKHR getSurfaceFormat() const;
    vk::PresentModeKHR getPresentMode() const;
    static vk::SurfaceTransformFlagBitsKHR getPreTransform(const vk::SurfaceCapabilitiesKHR& capabilities);
    static vk::CompositeAlphaFlagBitsKHR getCompositeAlpha(const vk::SurfaceCapabilitiesKHR& capabilities);

    bool needsRecreate();
    // Returns false if the surface is empty or lost.
    bool recreate();
    void create(const vk::SurfaceCapabilitiesKHR& capabilities, vk::Extent2D extent);
    void destroySwapchain();

private:
    VulkanDevice& m_device;
    FrameBufferCache& m_framebufferCache;
    ResourceContainer<Texture>& m_textures;
    ResourceContainer<RenderTarget>& m_renderTargetContainer;

    vk::UniqueSurfaceKHR m_surface;
    vk::SwapchainKHR m_swapchain;
    vk::Extent2D m_extent;
    std::vector<Shared<RenderTarget>> m_renderTargets;
    uint32_t m_currentImageIndex = 0;
    std::vector<vk::Semaphore> m_renderFinishedSemaphores;
};

}

}
