#pragma once

#include <array>
#include <bitset>

#include "Constants.h"
#include "render/vulkan/Resources.h"
#include "vulkan/vulkan.hpp"
#include "render/DeletionQueue.h"
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
    std::array<AttachmentDescription, kMaxColorAttachments + 1> attachments {};
    std::bitset<kMaxColorAttachments> hasResolve {};
    vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;

    bool operator==(const RenderPassCacheQuery& other) const = default;
};

class RenderPass {
public:
    RenderPass(vk::Device device, DeletionQueue& deletionQueue, const RenderPassCacheQuery& query);
    ~RenderPass();

    RenderPass(const RenderPass&) = delete;
    RenderPass& operator=(const RenderPass&) = delete;

    const vk::RenderPass& operator*() const& noexcept { return m_renderPass; }
    operator vk::RenderPass() const noexcept { return m_renderPass; }

private:
    vk::Device m_device;
    DeletionQueue& m_deletionQueue;
    vk::RenderPass m_renderPass;
};

class RenderPassCache {
    using key_type = RenderPassCacheQuery;

    struct RenderPassCacheQueryHash {
        size_t operator()(const RenderPassCacheQuery& query) const {
            size_t seed = 0;
            for (auto& color : query.attachments) {
                utils::hash_combine(seed, color.loadOp);
                utils::hash_combine(seed, color.storeOp);
                utils::hash_combine(seed, color.format);
            }
            utils::hash_combine(seed, query.hasResolve);
            utils::hash_combine(seed, query.samples);
            return seed;
        }
    };

public:
    static constexpr size_t kDefaultCacheSize = 32;

    RenderPassCache(vk::Device device, DeletionQueue& deletionQueue) : m_device(device), m_deletionQueue(deletionQueue) {}
    RenderPass& getOrCreate(const RenderPassDescription&, const gpu::FrameBufferFormat&);

    void clear();

private:
    LRUCache<key_type, RenderPass, RenderPassCacheQueryHash> m_cache { kDefaultCacheSize };
    vk::Device m_device;
    DeletionQueue& m_deletionQueue;
};
}