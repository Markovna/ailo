#pragma once

#include <vulkan/vulkan.hpp>
#include "Constants.h"

namespace ailo {

struct FrameBufferCacheQuery {
    vk::RenderPass renderPass;
    std::array<vk::ImageView, kMaxColorAttachments> color;
    vk::ImageView depth;
    uint32_t width {};
    uint32_t height {};
};

class FrameBufferCache {
public:

};

}