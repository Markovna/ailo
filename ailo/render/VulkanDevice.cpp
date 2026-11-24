#include "VulkanDevice.h"
#include <algorithm>

#include "utils/Log.h"
#include "vulkan/platform/VulkanPlatform.h"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace ailo {

VulkanDevice::VulkanDevice() {
    createInstance();
    setupDebugMessenger();

    static const std::vector<std::string_view> requiredDeviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    struct PhysicalDeviceSearchResult {
        vk::PhysicalDevice physicalDevice = nullptr;
        int32_t graphicsQueueFamilyIndex = -1;
    };

    auto findPhysicalDevice = [this](auto&& range) -> PhysicalDeviceSearchResult {
        for(auto& device : range) {
            auto deviceExtensionProperties = device.enumerateDeviceExtensionProperties();
            bool supportRequiredExtensions = std::ranges::all_of(requiredDeviceExtensions, [&](const auto& extension) {
                return std::ranges::any_of(deviceExtensionProperties, [&](const auto& prop) { return prop.extensionName == extension; });
            });
            if (!supportRequiredExtensions) { continue; }

            vk::PhysicalDeviceFeatures supportedFeatures;
            device.getFeatures(&supportedFeatures);
            if (!supportedFeatures.samplerAnisotropy) { continue; }
            // texc stores textures BC-compressed (the desktop target).
            if (!supportedFeatures.textureCompressionBC) { continue; }

            vk::PhysicalDeviceVulkan12Features supportedFeatures12{};
            vk::PhysicalDeviceFeatures2 supportedFeatures2{};
            supportedFeatures2.pNext = &supportedFeatures12;
            device.getFeatures2(&supportedFeatures2);
            if (!supportedFeatures12.timelineSemaphore) { continue; }

            // Shadow maps are sampled through a comparison sampler written into a descriptor set.
            const bool hasPortabilitySubset = std::ranges::any_of(deviceExtensionProperties, [](const auto& prop) {
                return std::string_view(prop.extensionName) == VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME;
            });
            if (hasPortabilitySubset) {
                vk::PhysicalDevicePortabilitySubsetFeaturesKHR portabilityFeatures{};
                vk::PhysicalDeviceFeatures2 features2{};
                features2.pNext = &portabilityFeatures;
                device.getFeatures2(&features2);
                if (!portabilityFeatures.mutableComparisonSamplers) { continue; }
            }

            int32_t graphicsQueueFamilyIndex = -1;
            auto queueFamilyProperties = device.getQueueFamilyProperties();
            for (uint32_t i = 0; i < queueFamilyProperties.size(); i++) {
                if (queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eGraphics) {
                    graphicsQueueFamilyIndex = static_cast<int32_t>(i);
                    break;
                }
            }

            if (graphicsQueueFamilyIndex < 0) { continue; }

            return { device, graphicsQueueFamilyIndex };
        }
        return {};
    };

    // The first suitable device wins. Prefer discrete over integrated GPUs
    auto deviceTypeRank = [](const vk::PhysicalDevice& device) {
        switch (device.getProperties().deviceType) {
            case vk::PhysicalDeviceType::eDiscreteGpu: return 0;
            case vk::PhysicalDeviceType::eIntegratedGpu: return 1;
            case vk::PhysicalDeviceType::eVirtualGpu: return 2;
            default: return 3;
        }
    };
    auto physicalDevices = m_instance.enumeratePhysicalDevices();
    std::ranges::stable_sort(physicalDevices, {}, deviceTypeRank);
    auto physicalDeviceSearchResult = findPhysicalDevice(physicalDevices);
    if (!physicalDeviceSearchResult.physicalDevice) {
        throw std::runtime_error("failed to find a suitable GPU!");
    }

    m_physicalDevice = physicalDeviceSearchResult.physicalDevice;
    m_graphicsQueueFamilyIndex = physicalDeviceSearchResult.graphicsQueueFamilyIndex;
    m_msaaSamples = getMaxUsableSampleCount();

    constexpr float queuePriority = 1.0f;
    vk::DeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.queueFamilyIndex = m_graphicsQueueFamilyIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    vk::PhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = true;
    deviceFeatures.textureCompressionBC = true;

    vk::PhysicalDeviceVulkan12Features deviceFeatures12{};
    deviceFeatures12.timelineSemaphore = true;

    std::vector<const char*> enabledExtensions;
    std::ranges::transform(requiredDeviceExtensions, std::back_inserter(enabledExtensions), [](const auto& extension) { return extension.data(); });

    auto availableExtensions = m_physicalDevice.enumerateDeviceExtensionProperties();
    const bool hasPortabilitySubset = std::ranges::any_of(availableExtensions, [](const auto& extension) {
        return strcmp(extension.extensionName, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME) == 0;
    });

    // Must be enabled when present (VUID-VkDeviceCreateInfo-pProperties-04451). Support for the chained features
    // was checked when picking the physical device.
    vk::PhysicalDevicePortabilitySubsetFeaturesKHR portabilityFeatures{};
    if (hasPortabilitySubset) {
        enabledExtensions.emplace_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
        portabilityFeatures.mutableComparisonSamplers = VK_TRUE;
        deviceFeatures12.pNext = &portabilityFeatures;
    }

    vk::DeviceCreateInfo createInfo{};
    createInfo.pNext = &deviceFeatures12;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
    createInfo.ppEnabledExtensionNames = enabledExtensions.data();

    m_device = m_physicalDevice.createDevice(createInfo);
    // Device-level pointers skip the loader's dispatch.
    VULKAN_HPP_DEFAULT_DISPATCHER.init(m_device);
    m_graphicsQueue = m_device.getQueue(m_graphicsQueueFamilyIndex, 0);
}

VulkanDevice::~VulkanDevice() {
    m_device.destroy();

    if (m_debugMessenger) {
        m_instance.destroyDebugUtilsMessengerEXT(m_debugMessenger);
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
    // Loads vulkan-1 (throws if there is no Vulkan driver) and the global functions.
    VULKAN_HPP_DEFAULT_DISPATCHER.init();

    vk::ApplicationInfo appInfo{};
    appInfo.pApplicationName = "Ailo";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Ailo Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    vk::InstanceCreateInfo createInfo{};
    createInfo.pApplicationInfo = &appInfo;

    auto extensions = vk_platform::instanceExtensions(vk::enumerateInstanceExtensionProperties());

    // TODO: this makes app crash in RenderDoc
    // createInfo.flags = vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
    // extensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    extensions.emplace_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

    std::vector<const char*> enabledLayers;

#if AILO_VK_ENABLED(AILO_VK_ENABLE_VALIDATION_LAYERS)
    // The layer comes with the Vulkan SDK, which the engine does not need: run without it when it is not installed.
    constexpr std::string_view validationLayer = "VK_LAYER_KHRONOS_validation";
    const auto layers = vk::enumerateInstanceLayerProperties();
    if (std::ranges::any_of(layers, [&](const auto& layer) { return layer.layerName == validationLayer; })) {
        extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        enabledLayers.emplace_back(validationLayer.data());
    } else {
        LOG_WARNING("{} not found: validation is disabled. Install the Vulkan SDK or check VK_LAYER_PATH",
                    validationLayer);
    }
#endif

    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    createInfo.enabledLayerCount = static_cast<uint32_t>(enabledLayers.size());
    createInfo.ppEnabledLayerNames = enabledLayers.data();

    m_instance = vk::createInstance(createInfo);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(m_instance);
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDevice::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {

    std::ostream* out = nullptr;
    auto errorMask = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    if((messageSeverity & errorMask) != 0) {
        LOG_ERROR("[Vulkan] {}", pCallbackData->pMessage);
    } else {
        LOG_WARNING("[Vulkan] {}", pCallbackData->pMessage);
    }

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

    // Not loaded when the validation layer (and so VK_EXT_debug_utils) was not enabled.
    const auto create = VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateDebugUtilsMessengerEXT;
    if (!create) {
        return;
    }
    if (create(m_instance, &createInfo, nullptr, reinterpret_cast<VkDebugUtilsMessengerEXT*>(&m_debugMessenger))
        != VK_SUCCESS) {
        throw std::runtime_error("failed to set up debug messenger!");
    }
#endif
}

}
