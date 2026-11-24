#pragma once

#include <span>
#include <vector>

#include <vulkan/vulkan.hpp>

#include "platform/NativeWindow.h"

namespace ailo::vk_platform {

std::vector<const char*> instanceExtensions(std::span<const vk::ExtensionProperties> available);

vk::SurfaceKHR createSurface(vk::Instance instance, const NativeWindow& window);

}
