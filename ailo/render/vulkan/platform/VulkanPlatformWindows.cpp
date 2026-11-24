#include "VulkanPlatform.h"
#include "VulkanPlatformUtils.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <vulkan/vulkan_win32.h>

namespace ailo::vk_platform {

std::vector<const char*> instanceExtensions(std::span<const vk::ExtensionProperties> available) {
    requireExtension(available, VK_KHR_SURFACE_EXTENSION_NAME);
    requireExtension(available, VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
    return { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
}

vk::SurfaceKHR createSurface(vk::Instance instance, const NativeWindow& window) {
    VkWin32SurfaceCreateInfoKHR createInfo { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
    createInfo.hinstance = GetModuleHandleW(nullptr);
    createInfo.hwnd = static_cast<HWND>(window.window);

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    checkSurface(loadFunction<PFN_vkCreateWin32SurfaceKHR>(instance, "vkCreateWin32SurfaceKHR")(
        instance, &createInfo, nullptr, &surface));
    return surface;
}

}
