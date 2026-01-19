#pragma once

#include <vulkan/vulkan.hpp>

#include "render/ResourcePtr.h"

namespace ailo::gpu {

class Texture : public enable_resource_ptr<Texture> {
public:
    Texture() = default;
    Texture(vk::Device device, vk::PhysicalDevice physicalDevice, vk::Format format, uint8_t levels, uint32_t width, uint32_t height, vk::Filter filter, vk::ImageUsageFlags usage, vk::ImageAspectFlags aspectFlags);
    Texture(vk::Device device, vk::Image, vk::Format, uint32_t width, uint32_t height, vk::ImageAspectFlags);

    ~Texture();

    void transitionLayout(vk::CommandBuffer, vk::ImageLayout layout);
    void transitionLayout(vk::CommandBuffer, vk::ImageLayout layout, vk::ImageSubresourceRange range);

    vk::ImageLayout getLayout(uint8_t level) {
        if (level >= m_rangeLayouts.size()) {
            return vk::ImageLayout::eUndefined;
        }

        return m_rangeLayouts[level];
    }

    void setLayout(uint32_t baseLevel, uint32_t levelCount, vk::ImageLayout layout) {
        m_rangeLayouts.resize(baseLevel + levelCount, vk::ImageLayout::eUndefined);
        for (uint32_t i = baseLevel; i < baseLevel + levelCount; i++) {
            m_rangeLayouts[i] = layout;
        }
    }

    void setLayout(uint8_t level, vk::ImageLayout layout) {
        m_rangeLayouts.resize(level + 1, vk::ImageLayout::eUndefined);
        m_rangeLayouts[level] = layout;
    }

    vk::Image image {};
    vk::DeviceMemory memory {};
    vk::ImageView imageView {};
    vk::Sampler sampler {};
    vk::Format format;
    vk::ImageAspectFlags aspect;
    uint32_t width;
    uint32_t height;

    uint8_t getLevels() const { return m_levels; }

private:
    vk::Device m_device {};
    std::vector<vk::ImageLayout> m_rangeLayouts;
    uint8_t m_levels = 0;

    vk::ImageView createImageView(vk::Device device, vk::Image image, vk::Format format, uint32_t levels, vk::ImageAspectFlags aspectFlags);
    uint32_t findMemoryType(vk::PhysicalDevice physicalDevice, uint32_t typeFilter, vk::MemoryPropertyFlags properties);
};

}
