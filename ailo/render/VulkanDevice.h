#pragma once

#include <vulkan/vulkan.hpp>
#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"
#include "VulkanConstants.h"

namespace ailo {

class VulkanDevice {
public:
    explicit VulkanDevice(GLFWwindow* window);
    ~VulkanDevice();

    vk::Device& device() { return m_device; }
    vk::PhysicalDevice& physicalDevice() { return m_physicalDevice; }
    vk::Instance& instance() { return m_instance; }
    vk::SurfaceKHR& surface() { return m_surface; }
    vk::Queue& graphicsQueue() { return m_graphicsQueue; }
    vk::Queue& presentQueue() { return m_presentQueue; }

    uint32_t graphicsQueueFamilyIndex() const { return m_graphicsQueueFamilyIndex; }
    uint32_t presentQueueFamilyIndex() const { return m_presentQueueFamilyIndex; }

    vk::SurfaceFormatKHR getSurfaceFormat() const;
    vk::PresentModeKHR getPresentMode() const;
    vk::Extent2D getSwapExtent() const;
    vk::Format getDepthFormat() const;

private:
    void createInstance();

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                           VkDebugUtilsMessageTypeFlagsEXT messageType,
                           const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);
    void setupDebugMessenger();

private:
    GLFWwindow* m_window;
    vk::Instance m_instance;
    vk::SurfaceKHR m_surface;
    vk::PhysicalDevice m_physicalDevice;
    vk::Device m_device;
    vk::Queue m_graphicsQueue;
    vk::Queue m_presentQueue;
    uint32_t m_graphicsQueueFamilyIndex;
    uint32_t m_presentQueueFamilyIndex;
    vk::DebugUtilsMessengerEXT m_debugMessenger;
};

}
