#pragma once

#include <algorithm>
#include <format>
#include <span>
#include <stdexcept>
#include <string_view>

#include <vulkan/vulkan.hpp>

namespace ailo::vk_platform {

inline bool hasExtension(std::span<const vk::ExtensionProperties> available, std::string_view name) {
    return std::ranges::any_of(available, [&](const auto& extension) {
        return std::string_view(extension.extensionName) == name;
    });
}

inline void requireExtension(std::span<const vk::ExtensionProperties> available, std::string_view name) {
    if (!hasExtension(available, name)) {
        throw std::runtime_error(std::format("Vulkan instance extension {} is not supported", name));
    }
}

template<typename Function>
Function loadFunction(vk::Instance instance, const char* name) {
    const auto function = reinterpret_cast<Function>(VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr(instance, name));
    if (!function) {
        throw std::runtime_error(std::format("Vulkan function {} is not available", name));
    }
    return function;
}

inline void checkSurface(VkResult result) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::format("Failed to create window surface: {}",
                                             vk::to_string(static_cast<vk::Result>(result))));
    }
}

}
