#pragma once

#include <vulkan/vulkan.hpp>
#include "render/vulkan/Resources.h"

#include "common/LRUCache.h"
#include "utils/Utils.h"

namespace ailo {

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
private:
    struct CacheKey {
        // TODO: replace with render target id
        PerColorAttachment<vk::ImageView> color;
        PerColorAttachment<vk::ImageView> resolve;
        vk::ImageView depth;
        uint32_t width {};
        uint32_t height {};
        vk::SampleCountFlagBits samples {};

        bool operator==(const CacheKey& other) const = default;
    };

    struct CacheKeyHash {
        std::size_t operator()(const CacheKey& key) const {
            size_t seed = 0;
            for (size_t i = 0; i < key.color.size(); i++) {
                utils::hash_combine(seed, static_cast<VkImageView>(key.color[i]));
                utils::hash_combine(seed, static_cast<VkImageView>(key.resolve[i]));
            }
            utils::hash_combine(seed, static_cast<VkImageView>(key.depth));
            utils::hash_combine(seed, key.width);
            utils::hash_combine(seed, key.height);
            utils::hash_combine(seed, key.samples);
            return seed;
        }
    };

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
    LRUCache<CacheKey, FrameBuffer, CacheKeyHash> m_cache { kDefaultCacheSize };
    vk::Device m_device;
};

}
