#include "SwapChain.h"
#include "vulkan/VulkanUtils.h"

namespace ailo {

SwapChain::SwapChain(RenderAPI& api, vk::SurfaceKHR surface, vk::PhysicalDevice physicalDevice, vk::Device device, vk::Extent2D extent,
    vk::SurfaceFormatKHR surfaceFormat, vk::PresentModeKHR presentMode) {

    auto capabilities = physicalDevice.getSurfaceCapabilitiesKHR(surface);

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    vk::SwapchainCreateInfoKHR createInfo{};
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;

    auto indices = vkutils::findQueueFamilies(physicalDevice, surface);
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = vk::SharingMode::eConcurrent;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = vk::SharingMode::eExclusive;
    }

    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = nullptr;

    m_swapchain = device.createSwapchainKHR(createInfo);
    auto swapchainImages = device.getSwapchainImagesKHR(m_swapchain);
    for (auto& image : swapchainImages) {
        vk::ImageViewCreateInfo createInfo{};
        createInfo.image = image;
        createInfo.viewType = vk::ImageViewType::e2D;
        createInfo.format = surfaceFormat.format;
        createInfo.components.r = vk::ComponentSwizzle::eIdentity;
        createInfo.components.g = vk::ComponentSwizzle::eIdentity;
        createInfo.components.b = vk::ComponentSwizzle::eIdentity;
        createInfo.components.a = vk::ComponentSwizzle::eIdentity;
        createInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        auto imageView = device.createImageView(createInfo);
        m_colors.push_back(gpu::Texture{
            .image = image,
            .memory = nullptr,
            .imageView = imageView,
            .sampler = nullptr,
            .format = surfaceFormat.format,
            .width = extent.width,
            .height = extent.height,
        });

        vk::SemaphoreCreateInfo semaphoreInfo{};
        m_renderFinishedSemaphores.push_back(device.createSemaphore(semaphoreInfo));
    }

    vk::Format depthFormat = api.findDepthFormat();

    vk::Image depthImage;
    vk::DeviceMemory depthImageMemory;

    api.createImage(extent.width, extent.height, depthFormat,
                vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment,
                vk::MemoryPropertyFlagBits::eDeviceLocal, depthImage, depthImageMemory);

    auto depthImageView = api.createImageView(depthImage, depthFormat, vk::ImageAspectFlagBits::eDepth);
    m_depth = gpu::Texture {
        .image = depthImage,
        .memory = depthImageMemory,
        .imageView = depthImageView,
        .sampler = nullptr,
        .format = depthFormat,
        .width = extent.width,
        .height = extent.height,
    };
}

vk::Result SwapChain::acquireNextImage(vk::Device device, vk::Semaphore& semaphore, uint64_t timeout) {
    auto result = device.acquireNextImageKHR(m_swapchain, timeout, semaphore);
    if (result.result == vk::Result::eErrorOutOfDateKHR) {
        return vk::Result::eErrorOutOfDateKHR;
    }

    m_currentImageIndex = result.value;
    return result.result;
}

vk::Result SwapChain::present(CommandBuffer& commandBuffer, vk::Queue graphicsQueue, vk::Queue presentQueue) {
    commandBuffer.submit(graphicsQueue, m_renderFinishedSemaphores[m_currentImageIndex]);

    vk::PresentInfoKHR presentInfo{};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_renderFinishedSemaphores[m_currentImageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapchain;
    presentInfo.pImageIndices = &m_currentImageIndex;
    auto result = presentQueue.presentKHR(presentInfo);
    return result;
}

void SwapChain::destroy(vk::Device device) {
    device.destroyImageView(m_depth.imageView);
    device.destroyImage(m_depth.image);
    device.freeMemory(m_depth.memory);

    for (auto tex : m_colors) {
        device.destroyImageView(tex.imageView);
    }

    for (auto semaphore : m_renderFinishedSemaphores) {
        device.destroySemaphore(semaphore);
    }

    device.destroySwapchainKHR(m_swapchain);
}

}