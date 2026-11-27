#include "Texture.h"

#include "VulkanUtils.h"

#include <cassert>

namespace ailo::gpu {

Texture::Texture(vk::Device device, vk::PhysicalDevice physicalDevice, TextureType type, vk::Format format, uint8_t levels, uint32_t width, uint32_t height, vk::ImageUsageFlags usage, vk::ImageAspectFlags aspect, vk::SampleCountFlagBits samples, uint8_t layers)
    : format(format), aspect(aspect), width(width), height(height), m_device(device), m_levels(std::max(levels, uint8_t(1))), m_type(type), m_samples(samples), m_usage(usage), m_ownsImage(true) {

    if (m_levels > 1) {
        m_usage |= vk::ImageUsageFlagBits::eTransferSrc;
    }

    m_layerCount = type == TextureType::Cubemap ? 6 : std::max(layers, uint8_t(1));

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
    imageInfo.usage = m_usage;
    imageInfo.samples = m_samples;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;

    if (type == TextureType::Cubemap) {
        imageInfo.flags = vk::ImageCreateFlagBits::eCubeCompatible;
    }

    image = device.createImage(imageInfo);

    vk::MemoryRequirements memRequirements = device.getImageMemoryRequirements(image);

    vk::MemoryAllocateInfo allocInfo{};
    allocInfo.allocationSize = memRequirements.size;
    std::optional<uint32_t> memoryType;
    if (m_usage & vk::ImageUsageFlagBits::eTransientAttachment) {
        memoryType = findMemoryType(physicalDevice, memRequirements.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eDeviceLocal | vk::MemoryPropertyFlagBits::eLazilyAllocated);
    }
    if (!memoryType) {
        memoryType = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
    }
    if (!memoryType) {
        throw std::runtime_error("failed to find suitable memory type!");
    }
    allocInfo.memoryTypeIndex = *memoryType;

    memory = device.allocateMemory(allocInfo);
    device.bindImageMemory(image, memory, 0);

    createViews();
}

Texture::Texture(vk::Device device, const ExternalImageDesc& desc)
    : image(desc.image), format(desc.format), aspect(desc.aspect), width(desc.width), height(desc.height),
      m_device(device), m_layerCount(std::max(desc.layers, uint8_t(1))), m_samples(desc.samples),
      m_usage(desc.usage), m_ownsImage(false) {
    assert(desc.image && "ExternalImageDesc::image is null");
    createViews();
    setLayout(0, m_levels, desc.initialLayout);
}

void Texture::createViews() {
    if (m_type == TextureType::Cubemap) {
        imageView = createImageView(vk::ImageViewType::eCube, 0, m_layerCount);
        return;
    }

    if (m_layerCount == 1) {
        imageView = createImageView(vk::ImageViewType::e2D, 0, 1);
        return;
    }

    imageView = createImageView(vk::ImageViewType::e2DArray, 0, m_layerCount);
    m_layerViews.reserve(m_layerCount);
    for (uint32_t layer = 0; layer < m_layerCount; layer++) {
        m_layerViews.push_back(createImageView(vk::ImageViewType::e2D, layer, 1));
    }
}

vk::ImageView Texture::createImageView(vk::ImageViewType viewType, uint32_t baseLayer, uint32_t layerCount) const {
    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image = image;
    viewInfo.viewType = viewType;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspect;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = m_levels;
    viewInfo.subresourceRange.baseArrayLayer = baseLayer;
    viewInfo.subresourceRange.layerCount = layerCount;

    return m_device.createImageView(viewInfo);
}

std::optional<uint32_t> Texture::findMemoryType(vk::PhysicalDevice physicalDevice, uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
    vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    return std::nullopt;
}

Texture::~Texture() {
    if (!m_device) {
        return;
    }

    for (auto view : m_layerViews) {
        m_device.destroyImageView(view);
    }
    m_device.destroyImageView(imageView);

    // An imported image belongs to whoever created it.
    if (m_ownsImage) {
        m_device.destroyImage(image);
        m_device.freeMemory(memory);
    }
}

void Texture::transitionLayout(vk::CommandBuffer commandBuffer, vk::ImageLayout newLayout,
    uint32_t baseLevel, uint32_t levelCount) {
    vk::ImageSubresourceRange range{};
    range.aspectMask = aspect;
    range.baseMipLevel = baseLevel;
    range.levelCount = levelCount > 0 ? levelCount : m_levels;
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
