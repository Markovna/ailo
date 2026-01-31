#include "Texture.h"

#include "VulkanUtils.h"

namespace ailo::gpu {

Texture::Texture(vk::Device device, vk::PhysicalDevice physicalDevice, TextureType type, vk::Format format, uint8_t levels, uint32_t width, uint32_t height, vk::Filter filter, vk::ImageUsageFlags usage, vk::ImageAspectFlags aspect, vk::SampleCountFlagBits samples)
    : m_device(device), format(format), width(width), height(height), aspect(aspect), m_levels(std::max(levels, uint8_t(1))), m_type(type), m_samples(samples) {

    if (m_levels > 1) {
        usage |= vk::ImageUsageFlagBits::eTransferSrc;
    }

    m_layerCount = type == TextureType::TEXTURE_CUBEMAP ? 6 : 1;

    vk::ImageCreateInfo imageInfo{};
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = m_levels;
    imageInfo.arrayLayers = m_layerCount;
    imageInfo.format = format;
    imageInfo.tiling = vk::ImageTiling::eOptimal;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    imageInfo.usage = usage;
    imageInfo.samples = m_samples;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;

    if (type == TextureType::TEXTURE_CUBEMAP) {
        imageInfo.flags = vk::ImageCreateFlagBits::eCubeCompatible;
    }

    image = device.createImage(imageInfo);

    vk::MemoryRequirements memRequirements = device.getImageMemoryRequirements(image);

    vk::MemoryAllocateInfo allocInfo{};
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

    memory = device.allocateMemory(allocInfo);
    device.bindImageMemory(image, memory, 0);

    imageView = createImageView(device, image, format, m_levels, aspect);

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
        samplerInfo.maxLod = VK_LOD_CLAMP_NONE;

        sampler = device.createSampler(samplerInfo);
    }
}

Texture::Texture(vk::Device device, vk::Image image, vk::Format format, uint32_t width, uint32_t height, vk::ImageAspectFlags aspectFlags)
    : m_device(device), image(image), format(format), width(width), height(height), aspect(aspectFlags) {
    imageView = createImageView(device, image, format, m_levels, aspectFlags);
}

vk::ImageView Texture::createImageView(vk::Device device, vk::Image image, vk::Format format, uint32_t levels,
    vk::ImageAspectFlags aspectFlags) {
    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image = image;
    viewInfo.viewType = m_type == TextureType::TEXTURE_2D ? vk::ImageViewType::e2D : vk::ImageViewType::eCube;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = levels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = m_layerCount;

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
    vk::ImageSubresourceRange range{};
    range.aspectMask = aspect;
    range.baseMipLevel = 0;
    range.levelCount = m_levels;
    range.baseArrayLayer = 0;
    range.layerCount = m_layerCount;
    transitionLayout(commandBuffer, newLayout, range);
}

void Texture::transitionLayout(vk::CommandBuffer commandBuffer, vk::ImageLayout newLayout, vk::ImageSubresourceRange range) {
    range.baseMipLevel = std::min(range.baseMipLevel, uint32_t(m_levels - 1));
    range.levelCount = std::min(range.levelCount, m_levels - range.baseMipLevel);

    vk::ImageLayout layout = getLayout(range.baseMipLevel);
    uint8_t baseLevel = 0;

    while (baseLevel < range.levelCount) {
        uint8_t level = baseLevel + 1;
        while (level < range.levelCount && layout == getLayout(range.baseMipLevel + level)) {
            level++;
        }

        if (layout != newLayout) {
            auto [srcAccess, srcStage] = vkutils::getTransitionSrcAccess(layout);
            auto [dstAccess, dstStage] = vkutils::getTransitionDstAccess(newLayout);

            vk::ImageSubresourceRange subRange = range;
            subRange.baseMipLevel = range.baseMipLevel + baseLevel;
            subRange.levelCount = level - baseLevel;

            vk::ImageMemoryBarrier barrier{};
            barrier.oldLayout = layout;
            barrier.newLayout = newLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange = subRange;
            barrier.srcAccessMask = srcAccess;
            barrier.dstAccessMask = dstAccess;

            commandBuffer.pipelineBarrier(
                    srcStage, dstStage,
                    {},
                    0, nullptr,
                    0, nullptr,
                    1, &barrier
                );
        }

        baseLevel = level;
        layout = getLayout(range.baseMipLevel + baseLevel);
    }

    setLayout(range.baseMipLevel, range.levelCount, newLayout);
}
}
