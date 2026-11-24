#include "VulkanPlatform.h"
#include "VulkanPlatformUtils.h"

#include <cstdint>

#include <X11/Xlib.h>
#include <vulkan/vulkan_xlib.h>

namespace ailo::vk_platform {

std::vector<const char*> instanceExtensions(std::span<const vk::ExtensionProperties> available) {
    requireExtension(available, VK_KHR_SURFACE_EXTENSION_NAME);
    requireExtension(available, VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
    return { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XLIB_SURFACE_EXTENSION_NAME };
}

vk::SurfaceKHR createSurface(vk::Instance instance, const NativeWindow& window) {
    VkXlibSurfaceCreateInfoKHR createInfo { VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR };
    createInfo.dpy = static_cast<Display*>(window.display);
    createInfo.window = static_cast<::Window>(reinterpret_cast<std::uintptr_t>(window.window));

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    checkSurface(loadFunction<PFN_vkCreateXlibSurfaceKHR>(instance, "vkCreateXlibSurfaceKHR")(
        instance, &createInfo, nullptr, &surface));
    return surface;
}

}
