#pragma once

#include <array>
#include <bitset>

#include "Constants.h"
#include "vulkan/vulkan.hpp"
#include "common/LRUCache.h"
#include "utils/Utils.h"

namespace ailo {
struct AttachmentDescription {
    vk::Format format;
    vk::AttachmentLoadOp loadOp;
    vk::AttachmentStoreOp storeOp;

    bool operator==(const AttachmentDescription& other) const = default;
};

struct RenderPassCacheQuery {
    std::array<AttachmentDescription, kMaxColorAttachments> colors;
    std::bitset<kMaxColorAttachments + 1> attachmentsUsed;
    AttachmentDescription depth;

    bool operator==(const RenderPassCacheQuery& other) const = default;
};

struct RenderPassCacheQueryHash {
    size_t operator()(const RenderPassCacheQuery& query) const {
        size_t seed = 0;
        utils::hash_combine(seed, query.depth.loadOp);
        utils::hash_combine(seed, query.depth.storeOp);
        utils::hash_combine(seed, query.depth.format);
        for (auto& color : query.colors) {
            utils::hash_combine(seed, color.loadOp);
            utils::hash_combine(seed, color.storeOp);
            utils::hash_combine(seed, color.format);
        }
        utils::hash_combine(seed, query.attachmentsUsed);
        return seed;
    }
};

class RenderPass {
public:
    RenderPass(vk::Device device, const RenderPassCacheQuery& query);
    ~RenderPass();

    vk::RenderPass getHandle() const { return m_renderPass; }

private:
    vk::Device m_device;
    vk::RenderPass m_renderPass;
};

class RenderPassCache {
public:
    static constexpr size_t kDefaultCacheSize = 32;

    explicit RenderPassCache(vk::Device device) : m_device(device) {}

    RenderPass& getOrCreate(const RenderPassCacheQuery& query);

    void clear();

private:
    LRUCache<RenderPassCacheQuery, RenderPass, RenderPassCacheQueryHash> m_cache { kDefaultCacheSize };
    vk::Device m_device;
};
}