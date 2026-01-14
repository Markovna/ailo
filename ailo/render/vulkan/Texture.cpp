#include "Texture.h"

#include "VulkanUtils.h"

namespace ailo::gpu {

Texture::Texture(vk::Device device, vk::PhysicalDevice physicalDevice, vk::Format format, uint32_t width, uint32_t height, vk::Filter filter, vk::ImageUsageFlags usage, vk::ImageAspectFlags aspect)
    : m_device(device), format(format), width(width), height(height), aspect(aspect) {

    vk::ImageCreateInfo imageInfo{};
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = vk::ImageTiling::eOptimal;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    imageInfo.usage = usage;
    imageInfo.samples = vk::SampleCountFlagBits::e1;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;

    image = device.createImage(imageInfo);

    vk::MemoryRequirements memRequirements = device.getImageMemoryRequirements(image);

    vk::MemoryAllocateInfo allocInfo{};
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

    memory = device.allocateMemory(allocInfo);
    device.bindImageMemory(image, memory, 0);

    imageView = createImageView(device, image, format, aspect);

    if (usage & vk::ImageUsageFlagBits::eSampled) {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = filter;
        samplerInfo.minFilter = filter;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
        samplerInfo.anisotropyEnable = VK_TRUE;

        vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();
        samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
        samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = vk::CompareOp::eAlways;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;

        sampler = device.createSampler(samplerInfo);
    }
}

Texture::Texture(vk::Device device, vk::Image image, vk::Format format, uint32_t width, uint32_t height, vk::ImageAspectFlags aspectFlags)
    : m_device(device), image(image), format(format), width(width), height(height), aspect(aspectFlags) {
    imageView = createImageView(device, image, format, aspectFlags);
}

vk::ImageView Texture::createImageView(vk::Device device, vk::Image image, vk::Format format,
    vk::ImageAspectFlags aspectFlags) {
    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image = image;
    viewInfo.viewType = vk::ImageViewType::e2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    return device.createImageView(viewInfo);
}

uint32_t Texture::findMemoryType(vk::PhysicalDevice physicalDevice, uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
    vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable memory type!");
}

Texture::~Texture() {
    m_device.destroySampler(sampler);
    m_device.destroyImageView(imageView);

    if (memory) {
        m_device.destroyImage(image);
        m_device.freeMemory(memory);
    }
}

void Texture::transitionLayout(vk::CommandBuffer commandBuffer, vk::ImageLayout newLayout) {
    if (layout == newLayout) {
        return;
    }

    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = layout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    auto [srcAccess, srcStage] = vkutils::getTransitionSrcAccess(layout);
    auto [dstAccess, dstStage] = vkutils::getTransitionDstAccess(newLayout);

    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;

    commandBuffer.pipelineBarrier(
            srcStage, dstStage,
            {},
            0, nullptr,
            0, nullptr,
            1, &barrier
        );

    layout = newLayout;

}
}
