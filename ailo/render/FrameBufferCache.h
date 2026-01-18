#pragma once

#include <vulkan/vulkan.hpp>
#include "render/vulkan/Resources.h"

#include "common/LRUCache.h"
#include "utils/Utils.h"

namespace ailo {

struct FrameBufferCacheQuery {
    PerColorAttachment<vk::Format> colorFormat;
    PerColorAttachment<vk::ImageView> color;
    vk::Format depthFormat;
    vk::ImageView depth;
    uint32_t width {};
    uint32_t height {};

    bool operator==(const FrameBufferCacheQuery& other) const = default;
};

struct FrameBufferCacheQueryHash {
    std::size_t operator()(const FrameBufferCacheQuery& key) const {
        size_t seed = 0;
        for (auto& format : key.colorFormat) {
            utils::hash_combine(seed, static_cast<VkFormat>(format));
        }
        for (auto& imageView : key.color) {
            utils::hash_combine(seed, static_cast<VkImageView>(imageView));
        }
        utils::hash_combine(seed, static_cast<VkImageView>(key.depth));
        utils::hash_combine(seed, static_cast<VkFormat>(key.depthFormat));
        utils::hash_combine(seed, key.width);
        utils::hash_combine(seed, key.height);
        return seed;
    }
};

class FrameBuffer {
public:
    FrameBuffer(vk::Device device, vk::RenderPass renderPass, const gpu::FrameBufferImageView& views, uint32_t width,
                uint32_t height);
    ~FrameBuffer();

    vk::Framebuffer operator*() const noexcept { return m_framebuffer; }
    operator vk::Framebuffer() const noexcept { return m_framebuffer; }

private:
    vk::Device m_device;
    vk::Framebuffer m_framebuffer;
};

class FrameBufferCache {
public:
    static constexpr size_t kDefaultCacheSize = 32;

    explicit FrameBufferCache(vk::Device device) : m_device(device) {}

    FrameBuffer& getOrCreate(
        vk::RenderPass,
        const gpu::FrameBufferFormat& formats,
        const gpu::FrameBufferImageView& views,
        uint32_t width, uint32_t height);

    void clear();

private:
    LRUCache<FrameBufferCacheQuery, FrameBuffer, FrameBufferCacheQueryHash> m_cache { kDefaultCacheSize };
    vk::Device m_device;
};

}
