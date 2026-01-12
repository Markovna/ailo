#pragma once

#include <vulkan/vulkan.hpp>

namespace ailo::gpu {

class Texture {
public:
    Texture() = default;
    Texture(vk::Device device, vk::PhysicalDevice physicalDevice, vk::Format format, uint32_t width, uint32_t height, vk::Filter filter, vk::ImageUsageFlags usage, vk::ImageAspectFlags aspectFlags);
    Texture(vk::Device device, vk::Image, vk::Format, uint32_t width, uint32_t height, vk::ImageAspectFlags);

    ~Texture();

    vk::Image image {};
    vk::DeviceMemory memory {};
    vk::ImageView imageView {};
    vk::Sampler sampler {};
    vk::Format format;
    uint32_t width;
    uint32_t height;

private:
    vk::Device m_device {};

    vk::ImageView createImageView(vk::Device device, vk::Image image, vk::Format format, vk::ImageAspectFlags aspectFlags);
    uint32_t findMemoryType(vk::PhysicalDevice physicalDevice, uint32_t typeFilter, vk::MemoryPropertyFlags properties);
};

}