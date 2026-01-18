#include "FrameBufferCache.h"

namespace ailo {

FrameBuffer::FrameBuffer(
    vk::Device device,
    const FrameBufferCacheQuery& query)
        : m_device(device) {

    std::array<vk::ImageView, query.color.size() + 1> attachments;
    std::copy_n(query.color.begin(), query.attachmentCount, attachments.begin());
    attachments[query.attachmentCount] = query.depth;

    vk::FramebufferCreateInfo framebufferInfo{};
    framebufferInfo.renderPass = query.renderPass;
    framebufferInfo.attachmentCount = query.attachmentCount + 1;
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = query.width;
    framebufferInfo.height = query.height;
    framebufferInfo.layers = 1;

    m_framebuffer = device.createFramebuffer(framebufferInfo);
}

FrameBuffer::~FrameBuffer() {
    m_device.destroyFramebuffer(m_framebuffer);
}

FrameBuffer& FrameBufferCache::getOrCreate(const FrameBufferCacheQuery& query) {
    auto [it, result] = m_cache.tryEmplace(query, m_device, query);
    return it->second;
}

void FrameBufferCache::clear() {
    m_cache.clear();
}
}
