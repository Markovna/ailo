#pragma once

#include <vulkan/vulkan.hpp>
#include "Constants.h"

#include "common/LRUCache.h"
#include "utils/Utils.h"
#include "vulkan/Resources.h"

namespace ailo {

struct FrameBufferCacheQuery {
    vk::RenderPass renderPass;
    PerColorAttachment<vk::ImageView> color;
    uint32_t attachmentCount;
    vk::ImageView depth;
    uint32_t width {};
    uint32_t height {};

    bool operator==(const FrameBufferCacheQuery& other) const {
        return renderPass == other.renderPass &&
            width == other.width &&
            height == other.height &&
            attachmentCount == other.attachmentCount &&
            color == other.color &&
            depth == other.depth;
    }
};

struct FrameBufferCacheQueryHash {
    std::size_t operator()(const FrameBufferCacheQuery& key) const {
        size_t seed = 0;
        utils::hash_combine(seed, static_cast<VkRenderPass>(key.renderPass));
        for (size_t i = 0; i < key.attachmentCount; ++i) {
            utils::hash_combine(seed, static_cast<VkImageView>(key.color[i]));
        }
        utils::hash_combine(seed, key.attachmentCount);
        utils::hash_combine(seed, static_cast<VkImageView>(key.depth));
        utils::hash_combine(seed, key.width);
        utils::hash_combine(seed, key.height);
        return seed;
    }
};

class FrameBuffer {
public:
    FrameBuffer(vk::Device device, const FrameBufferCacheQuery&);
    ~FrameBuffer();

    vk::Framebuffer getHandle() const { return m_framebuffer; }

private:
    vk::Device m_device;
    vk::Framebuffer m_framebuffer;
};

class FrameBufferCache {
public:
    static constexpr size_t kDefaultCacheSize = 32;

    explicit FrameBufferCache(vk::Device device) : m_device(device) {}

    FrameBuffer& getOrCreate(const FrameBufferCacheQuery& query);

    void clear();

private:
    LRUCache<FrameBufferCacheQuery, FrameBuffer, FrameBufferCacheQueryHash> m_cache { kDefaultCacheSize };
    vk::Device m_device;
};

}
