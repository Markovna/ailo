#include "FrameBufferCache.h"

namespace ailo {

FrameBuffer::FrameBuffer(
    vk::Device device,
    DeletionQueue& deletionQueue,
    vk::RenderPass renderPass,
    const gpu::FrameBufferImageView& views,
    uint32_t width, uint32_t height)
        : m_device(device), m_deletionQueue(deletionQueue) {

    std::array<vk::ImageView, 2 * views.color.size() + 1> attachments;
    uint32_t attachmentCount = 0;
    for (auto i : views.color) {
        if (i != VK_NULL_HANDLE) {
            attachments[attachmentCount++] = i;
        }
    }
    for (auto i : views.resolve) {
        if (i != VK_NULL_HANDLE) {
            attachments[attachmentCount++] = i;
        }
    }

    if (views.depth != VK_NULL_HANDLE) {
        attachments[attachmentCount++] = views.depth;
    }

    vk::FramebufferCreateInfo framebufferInfo{};
    framebufferInfo.renderPass = renderPass;
    framebufferInfo.attachmentCount = attachmentCount;
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = width;
    framebufferInfo.height = height;
    framebufferInfo.layers = 1;

    m_framebuffer = device.createFramebuffer(framebufferInfo);
}

FrameBuffer::~FrameBuffer() {
    m_deletionQueue.defer([device = m_device, framebuffer = m_framebuffer] {
        device.destroyFramebuffer(framebuffer);
    });
}

FrameBuffer& FrameBufferCache::getOrCreate(vk::RenderPass renderPass, const gpu::FrameBufferFormat& formats,
    const gpu::FrameBufferImageView& views, uint32_t width, uint32_t height) {

    // The render pass is intentionally not part of the key: a framebuffer can be used with any render pass
    // compatible with the one it was created with, and compatibility is fully determined by the attachments
    // (their formats follow from the views) and the sample count. This also keeps framebuffers independent
    // of render pass lifetime.
    CacheKey query {
        .color = views.color,
        .resolve = views.resolve,
        .depth = views.depth,
        .width = width,
        .height = height,
        .samples = formats.samples
    };

    auto [it, result] = m_cache.tryEmplace(query, m_device, m_deletionQueue, renderPass, views, width, height);
    return it->second;
}

void FrameBufferCache::clear() {
    m_cache.clear();
}
}
