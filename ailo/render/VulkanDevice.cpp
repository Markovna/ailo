#include "VulkanDevice.h"
#include <iostream>

namespace ailo {

// Static helper to load debug extension functions
static VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                              const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    }
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

static void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger,
                                           const VkAllocationCallbacks* pAllocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
}

VulkanDevice::VulkanDevice(GLFWwindow* window)
    : m_window(window) {
    createInstance();

    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(m_instance, window, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create window surface!");
    }

    m_surface = surface;

    static const std::vector<std::string_view> requiredDeviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    struct PhysicalDeviceSearchResult {
        vk::PhysicalDevice physicalDevice = nullptr;
        int32_t graphicsQueueFamilyIndex = -1;
        int32_t presentQueueFamilyIndex = -1;
    };

    auto findPhysicalDevice = [&surface](auto&& range) -> PhysicalDeviceSearchResult {
        for(auto& device : range) {
            auto deviceExtensionProperties = device.enumerateDeviceExtensionProperties();
            bool supportRequiredExtensions = std::ranges::all_of(requiredDeviceExtensions, [&](const auto& extension) {
                return std::ranges::any_of(deviceExtensionProperties, [&](const auto& prop) { return prop.extensionName == extension; });
            });
            if (!supportRequiredExtensions) { continue; }

            auto formats = device.getSurfaceFormatsKHR(surface);
            if (formats.empty()) { continue; }

            auto presentModes = device.getSurfacePresentModesKHR(surface);
            if (presentModes.empty()) { continue; }

            vk::PhysicalDeviceFeatures supportedFeatures;
            device.getFeatures(&supportedFeatures);
            if (!supportedFeatures.samplerAnisotropy) { continue; }

            int32_t graphicsQueueFamilyIndex = -1;
            int32_t presentQueueFamilyIndex = -1;
            auto queueFamilyProperties = device.getQueueFamilyProperties();
            for (size_t i = 0; i < queueFamilyProperties.size(); i++) {
                if (queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eGraphics) {
                    graphicsQueueFamilyIndex = i;
                }
                if (device.getSurfaceSupportKHR(i, surface)) {
                    presentQueueFamilyIndex = i;
                }
            }

            if (graphicsQueueFamilyIndex < 0 || presentQueueFamilyIndex < 0) { continue; }

            return { device, graphicsQueueFamilyIndex, presentQueueFamilyIndex };
        }
        return {};
    };

    auto physicalDevices = m_instance.enumeratePhysicalDevices();
    auto physicalDeviceSearchResult = findPhysicalDevice(physicalDevices);
    if (!physicalDeviceSearchResult.physicalDevice) {
        throw std::runtime_error("failed to find a suitable GPU!");
    }

    m_physicalDevice = physicalDeviceSearchResult.physicalDevice;
    m_graphicsQueueFamilyIndex = physicalDeviceSearchResult.graphicsQueueFamilyIndex;
    m_presentQueueFamilyIndex = physicalDeviceSearchResult.presentQueueFamilyIndex;
    m_msaaSamples = getMaxUsableSampleCount();

    uint32_t queueCreateInfoCount = 1;
    std::array<vk::DeviceQueueCreateInfo, 2> queueCreateInfos;
    constexpr float queuePriority = 1.0f;
    queueCreateInfos[0].queueFamilyIndex = m_graphicsQueueFamilyIndex;
    queueCreateInfos[0].queueCount = 1;
    queueCreateInfos[0].pQueuePriorities = &queuePriority;

    if (m_presentQueueFamilyIndex != m_graphicsQueueFamilyIndex) {
        queueCreateInfos[1].queueFamilyIndex = m_presentQueueFamilyIndex;
        queueCreateInfos[1].queueCount = 1;
        queueCreateInfos[1].pQueuePriorities = &queuePriority;
        queueCreateInfoCount = 2;
    }

    vk::PhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = true;

    std::vector<const char*> enabledExtensions;
    std::ranges::transform(requiredDeviceExtensions, std::back_inserter(enabledExtensions), [](const auto& extension) { return extension.data(); });

    auto availableExtensions = m_physicalDevice.enumerateDeviceExtensionProperties();
    std::vector<const char*> desiredExtension = { "VK_KHR_portability_subset" };
    for (const auto& desiredExt : desiredExtension) {
        bool supportExt = std::ranges::any_of(availableExtensions, [&](auto& extension) {
            return strcmp(extension.extensionName, desiredExt) == 0;
        });
        if (supportExt) {
            enabledExtensions.emplace_back(desiredExt);
        }
    }

    vk::DeviceCreateInfo createInfo{};
    createInfo.queueCreateInfoCount = queueCreateInfoCount;
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
    createInfo.ppEnabledExtensionNames = enabledExtensions.data();

    m_device = m_physicalDevice.createDevice(createInfo);
    m_graphicsQueue = m_device.getQueue(m_graphicsQueueFamilyIndex, 0);
    m_presentQueue = m_device.getQueue(m_presentQueueFamilyIndex, 0);
}

VulkanDevice::~VulkanDevice() {
    m_device.destroy();

    m_instance.destroySurfaceKHR(m_surface);

    if (m_debugMessenger) {
        DestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
    }

    m_instance.destroy();
}

vk::SampleCountFlagBits VulkanDevice::getMaxUsableSampleCount() {
    vk::PhysicalDeviceProperties properties = m_physicalDevice.getProperties();

    vk::SampleCountFlags counts = properties.limits.framebufferColorSampleCounts & properties.limits.framebufferDepthSampleCounts;
    if (counts & vk::SampleCountFlagBits::e64) { return vk::SampleCountFlagBits::e64; }
    if (counts & vk::SampleCountFlagBits::e32) { return vk::SampleCountFlagBits::e32; }
    if (counts & vk::SampleCountFlagBits::e16) { return vk::SampleCountFlagBits::e16; }
    if (counts & vk::SampleCountFlagBits::e8) { return vk::SampleCountFlagBits::e8; }
    if (counts & vk::SampleCountFlagBits::e4) { return vk::SampleCountFlagBits::e4; }
    if (counts & vk::SampleCountFlagBits::e2) { return vk::SampleCountFlagBits::e2; }

    return vk::SampleCountFlagBits::e1;
}

void VulkanDevice::createInstance() {
    vk::ApplicationInfo appInfo{};
    appInfo.pApplicationName = "Ailo";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Ailo Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    vk::InstanceCreateInfo createInfo{};
    createInfo.pApplicationInfo = &appInfo;
    createInfo.flags = vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;

    uint32_t extensionCount = 0;
    auto glfwRequiredExtensions = glfwGetRequiredInstanceExtensions(&extensionCount);
    std::vector<const char*> extensions(glfwRequiredExtensions, glfwRequiredExtensions + extensionCount);

    extensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    extensions.emplace_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

    std::vector<const char*> enabledLayers;

#if AILO_VK_ENABLED(AILO_VK_VALIDATION)
    extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    enabledLayers.emplace_back("VK_LAYER_KHRONOS_validation");
#endif


    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    createInfo.enabledLayerCount = static_cast<uint32_t>(enabledLayers.size());
    createInfo.ppEnabledLayerNames = enabledLayers.data();

    m_instance = vk::createInstance(createInfo);
}

vk::SurfaceFormatKHR VulkanDevice::getSurfaceFormat() const {
    auto formats = m_physicalDevice.getSurfaceFormatsKHR(m_surface);
    auto findFormat = std::ranges::find_if(formats, [](const auto& format) {
        return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
    });
    if (findFormat == formats.end()) { return formats[0]; }
    return *findFormat;
}

vk::PresentModeKHR VulkanDevice::getPresentMode() const {
    auto presentModes = m_physicalDevice.getSurfacePresentModesKHR(m_surface);
    auto findMode = std::ranges::find(presentModes, vk::PresentModeKHR::eMailbox);
    if (findMode != presentModes.end()) { return *findMode; }
    return vk::PresentModeKHR::eFifo;
}

vk::Extent2D VulkanDevice::getSwapExtent() const {
    const auto capabilities = m_physicalDevice.getSurfaceCapabilitiesKHR(m_surface);
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    int width, height;
    glfwGetFramebufferSize(m_window, &width, &height);

    vk::Extent2D actualExtent = {
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height)
    };

    actualExtent.width = std::clamp(actualExtent.width,
                                    capabilities.minImageExtent.width,
                                    capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height,
                                     capabilities.minImageExtent.height,
                                     capabilities.maxImageExtent.height);

    return actualExtent;
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDevice::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {

    std::ostream* out = nullptr;
    auto errorMask = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    if((pCallbackData->flags & errorMask) > 0) {
        out = &std::cerr;
    } else {
        out = &std::cout;
    }

    *out << "[Vulkan] " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}



vk::Format VulkanDevice::getDepthFormat() const {
    for (vk::Format format : {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint}) {
        vk::FormatProperties props = m_physicalDevice.getFormatProperties(format);
        if (props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
            return format;
        }
    }

    return vk::Format::eUndefined;
}

vk::FormatProperties VulkanDevice::getFormatProperties(vk::Format format) const {
    return m_physicalDevice.getFormatProperties(format);
}

void VulkanDevice::setupDebugMessenger() {
#if AILO_VK_ENABLED(AILO_VK_ENABLE_VALIDATION_LAYERS)

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;

    if (CreateDebugUtilsMessengerEXT(m_instance, &createInfo, nullptr,
        reinterpret_cast<VkDebugUtilsMessengerEXT*>(&m_debugMessenger)) != VK_SUCCESS) {
        throw std::runtime_error("failed to set up debug messenger!");
        }
#endif
}

}
