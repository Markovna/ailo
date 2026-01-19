#include "SwapChain.h"

#include <iostream>
#include <ostream>

#include "vulkan/VulkanUtils.h"

namespace ailo {

SwapChain::SwapChain(VulkanDevice& device) {
    vk::Device vkDevice = device.device();
    vk::SurfaceFormatKHR surfaceFormat = device.getSurfaceFormat();
    vk::PresentModeKHR presentMode = device.getPresentMode();
    vk::Extent2D extent = device.getSwapExtent();
    vk::Format depthFormat = device.getDepthFormat();

    m_depth.emplace(vkDevice, device.physicalDevice(), depthFormat, 1, extent.width, extent.height, vk::Filter{}, vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::ImageAspectFlagBits::eDepth);

    auto capabilities = device.physicalDevice().getSurfaceCapabilitiesKHR(device.surface());

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    vk::SwapchainCreateInfoKHR createInfo{};
    createInfo.surface = device.surface();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;

    uint32_t queueFamilyIndices[] = {device.graphicsQueueFamilyIndex(), device.presentQueueFamilyIndex()};

    if (device.graphicsQueueFamilyIndex() != device.presentQueueFamilyIndex()) {
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

    m_swapchain = vkDevice.createSwapchainKHR(createInfo);
    auto swapchainImages = vkDevice.getSwapchainImagesKHR(m_swapchain);
    m_colors.reserve(swapchainImages.size());
    for (auto& image : swapchainImages) {
        m_colors.emplace_back(vkDevice, image, surfaceFormat.format, extent.width, extent.height, vk::ImageAspectFlagBits::eColor);

        vk::SemaphoreCreateInfo semaphoreInfo{};
        m_renderFinishedSemaphores.push_back(vkDevice.createSemaphore(semaphoreInfo));
    }
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
    auto& texture = m_colors[m_currentImageIndex];
    texture.transitionLayout(commandBuffer.buffer(), vk::ImageLayout::ePresentSrcKHR);

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
    if (m_depth) {
        m_depth.reset();
    }

    m_colors.clear();

    for (auto semaphore : m_renderFinishedSemaphores) {
        device.destroySemaphore(semaphore);
    }

    device.destroySwapchainKHR(m_swapchain);
}

}