#pragma once

#include <vulkan/vulkan.hpp>
#include "VulkanConstants.h"

namespace ailo {

class VulkanDevice {
public:
    VulkanDevice();

    ~VulkanDevice();

    vk::Device& device() { return m_device; }
    vk::PhysicalDevice& physicalDevice() { return m_physicalDevice; }
    vk::Instance& instance() { return m_instance; }
    vk::Queue& graphicsQueue() { return m_graphicsQueue; }

    uint32_t graphicsQueueFamilyIndex() const { return m_graphicsQueueFamilyIndex; }

    vk::Format getDepthFormat() const;
    vk::FormatProperties getFormatProperties(vk::Format format) const;

    vk::Device operator*() { return m_device; }
    vk::Device* operator->() { return &m_device; }

    auto getMSAASamples() const { return m_msaaSamples; }

private:
    void createInstance();

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                           VkDebugUtilsMessageTypeFlagsEXT messageType,
                           const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);
    void setupDebugMessenger();

    vk::SampleCountFlagBits getMaxUsableSampleCount();

private:
    vk::Instance m_instance;
    vk::PhysicalDevice m_physicalDevice;
    vk::SampleCountFlagBits m_msaaSamples = vk::SampleCountFlagBits::e1;
    vk::Device m_device;
    vk::Queue m_graphicsQueue;
    uint32_t m_graphicsQueueFamilyIndex;
    vk::DebugUtilsMessengerEXT m_debugMessenger;
};

}
