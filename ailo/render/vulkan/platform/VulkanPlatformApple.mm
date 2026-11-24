#include "VulkanPlatform.h"
#include "VulkanPlatformUtils.h"

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#include <vulkan/vulkan_metal.h>

namespace ailo::vk_platform {

std::vector<const char*> instanceExtensions(std::span<const vk::ExtensionProperties> available) {
    requireExtension(available, VK_KHR_SURFACE_EXTENSION_NAME);
    requireExtension(available, VK_EXT_METAL_SURFACE_EXTENSION_NAME);
    return { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_METAL_SURFACE_EXTENSION_NAME };
}

vk::SurfaceKHR createSurface(vk::Instance instance, const NativeWindow& window) {
    NSWindow* nsWindow = (__bridge NSWindow*) window.window;
    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.contentsScale = nsWindow.backingScaleFactor;
    nsWindow.contentView.layer = layer;
    nsWindow.contentView.wantsLayer = YES;

    VkMetalSurfaceCreateInfoEXT createInfo { VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT };
    createInfo.pLayer = layer;

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    checkSurface(loadFunction<PFN_vkCreateMetalSurfaceEXT>(instance, "vkCreateMetalSurfaceEXT")(
        instance, &createInfo, nullptr, &surface));
    return surface;
}

}
