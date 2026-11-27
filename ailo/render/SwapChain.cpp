#include "SwapChain.h"

#include <algorithm>
#include <stdexcept>

#include "utils/Log.h"
#include "vulkan/platform/VulkanPlatform.h"
#include "vulkan/VulkanUtils.h"

namespace ailo::gpu {

SwapChain::SwapChain(VulkanDevice& device, FrameBufferCache& framebufferCache, const NativeWindow& window,
                     ResourceContainer<Texture>& textures, ResourceContainer<RenderTarget>& renderTargets)
    : m_device(device), m_framebufferCache(framebufferCache), m_textures(textures), m_renderTargetContainer(renderTargets),
      m_surface(vk_platform::createSurface(device.instance(), window), device.instance()) {
    const auto physicalDevice = device.physicalDevice();
    if (!physicalDevice.getSurfaceSupportKHR(device.graphicsQueueFamilyIndex(), *m_surface)) {
        throw std::runtime_error("The GPU cannot present to this window");
    }
    if (physicalDevice.getSurfaceFormatsKHR(*m_surface).empty()) {
        throw std::runtime_error("The GPU supports no surface format for this window");
    }
    recreate();
}

SwapChain::~SwapChain() {
    destroySwapchain();
}

bool SwapChain::needsRecreate() {
    if (!m_swapchain) {
        return true;
    }

    try {
        return m_device.physicalDevice().getSurfaceCapabilitiesKHR(*m_surface).currentExtent != m_extent;
    } catch (const vk::SurfaceLostKHRError&) {
        return true;
    }
}

bool SwapChain::recreate() {
    try {
        const auto capabilities = m_device.physicalDevice().getSurfaceCapabilitiesKHR(*m_surface);
        const vk::Extent2D extent = capabilities.currentExtent;
        if (extent.width == 0 || extent.height == 0) {
            return false;
        }
        create(capabilities, extent);
        return true;
    } catch (const vk::SurfaceLostKHRError&) {
        // A failed creation retires the old swapchain too, so it can't be used anymore.
        if (m_swapchain) {
            log::error("SwapChain: the window surface is lost");
            destroySwapchain();
        }
        return false;
    }
}

void SwapChain::create(const vk::SurfaceCapabilitiesKHR& capabilities, vk::Extent2D extent) {
    const vk::SurfaceFormatKHR surfaceFormat = getSurfaceFormat();
    const vk::Format depthFormat = m_device.getDepthFormat();

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    vk::SwapchainCreateInfoKHR createInfo{};
    createInfo.surface = *m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;

    createInfo.imageSharingMode = vk::SharingMode::eExclusive;
    createInfo.preTransform = getPreTransform(capabilities);
    createInfo.compositeAlpha = getCompositeAlpha(capabilities);
    createInfo.presentMode = getPresentMode();
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = m_swapchain;
    const vk::SwapchainKHR swapchain = m_device->createSwapchainKHR(createInfo);

    destroySwapchain();
    m_swapchain = swapchain;
    m_extent = extent;
    m_currentImageIndex = 0;

    Shared<gpu::Texture> depth = m_textures.make(
        *m_device, m_device.physicalDevice(), TextureType::Texture2D,
        depthFormat, 1, extent.width, extent.height,
        vk::ImageUsageFlagBits::eDepthStencilAttachment,
        vk::ImageAspectFlagBits::eDepth);

    auto swapchainImages = m_device->getSwapchainImagesKHR(m_swapchain);
    m_renderTargets.reserve(swapchainImages.size());
    for (auto& image : swapchainImages) {
        auto& rt = m_renderTargets.emplace_back(m_renderTargetContainer.make());
        rt->samples = vk::SampleCountFlagBits::e1;
        rt->width = extent.width;
        rt->height = extent.height;

        rt->colors[0] = m_textures.make(*m_device, ExternalImageDesc {
            .image = image,
            .format = surfaceFormat.format,
            .width = extent.width,
            .height = extent.height,
            .usage = createInfo.imageUsage,
            .aspect = vk::ImageAspectFlagBits::eColor,
        });

        rt->depth = depth;

        vk::SemaphoreCreateInfo semaphoreInfo{};
        m_renderFinishedSemaphores.push_back(m_device->createSemaphore(semaphoreInfo));
    }
}

vk::Result SwapChain::acquireNextImage(vk::Semaphore semaphore, uint64_t timeout) {
    if (needsRecreate() && !recreate()) {
        return vk::Result::eErrorOutOfDateKHR;
    }

    uint32_t imageIndex = 0;
    const auto result = m_device->acquireNextImageKHR(m_swapchain, timeout, semaphore, nullptr, &imageIndex);
    if (result == vk::Result::eSuccess || result == vk::Result::eSuboptimalKHR) {
        m_currentImageIndex = imageIndex;
    }

    if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eErrorSurfaceLostKHR) {
        recreate();
    }

    return result;
}

vk::Result SwapChain::present(CommandBuffer& commandBuffer, vk::Queue queue) {
    auto& texture = m_renderTargets[m_currentImageIndex]->colors[0];
    texture->transitionLayout(commandBuffer.buffer(), vk::ImageLayout::ePresentSrcKHR);

    commandBuffer.submit(queue, m_renderFinishedSemaphores[m_currentImageIndex]);

    vk::PresentInfoKHR presentInfo{};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_renderFinishedSemaphores[m_currentImageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapchain;
    presentInfo.pImageIndices = &m_currentImageIndex;

    return queue.presentKHR(&presentInfo);
}

void SwapChain::destroySwapchain() {
    if (!m_swapchain) return;

    // Called by the destructor, which must not throw
    try {
        m_device->waitIdle();
    } catch (const vk::SystemError& e) {
        log::error("SwapChain: waitIdle failed: {}", e.what());
    }

    m_framebufferCache.clear();
    m_renderTargets.clear();

    for (auto semaphore : m_renderFinishedSemaphores) {
        m_device->destroySemaphore(semaphore);
    }
    m_renderFinishedSemaphores.clear();

    m_device->destroySwapchainKHR(m_swapchain);
    m_swapchain = nullptr;
}

vk::SurfaceFormatKHR SwapChain::getSurfaceFormat() const {
    auto formats = m_device.physicalDevice().getSurfaceFormatsKHR(*m_surface);
    for (auto preferred : { vk::Format::eB8G8R8A8Srgb, vk::Format::eR8G8B8A8Srgb }) {
        const vk::SurfaceFormatKHR surfaceFormat { preferred, vk::ColorSpaceKHR::eSrgbNonlinear };
        if (std::ranges::find(formats, surfaceFormat) != formats.end()) { return surfaceFormat; }
    }
    return formats[0];
}

vk::PresentModeKHR SwapChain::getPresentMode() const {
    auto presentModes = m_device.physicalDevice().getSurfacePresentModesKHR(*m_surface);
    auto findMode = std::ranges::find(presentModes, vk::PresentModeKHR::eFifo);
    if (findMode != presentModes.end()) { return *findMode; }
    return vk::PresentModeKHR::eMailbox;
}

// The renderer doesn't pre-rotate, so rotation is left to the compositor.
vk::SurfaceTransformFlagBitsKHR SwapChain::getPreTransform(const vk::SurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity) {
        return vk::SurfaceTransformFlagBitsKHR::eIdentity;
    }
    return capabilities.currentTransform;
}

vk::CompositeAlphaFlagBitsKHR SwapChain::getCompositeAlpha(const vk::SurfaceCapabilitiesKHR& capabilities) {
    using enum vk::CompositeAlphaFlagBitsKHR;
    for (auto mode : { eOpaque, eInherit, ePreMultiplied }) {
        if (capabilities.supportedCompositeAlpha & mode) { return mode; }
    }
    return ePostMultiplied;
}

}
