#include "RenderPassCache.h"

#include <iostream>
#include <ostream>

namespace ailo {
RenderPass::RenderPass(vk::Device device, const RenderPassCacheQuery& query) : m_device(device) {
    std::array<vk::AttachmentDescription, 2 * kMaxColorAttachments + 1> attachments; // color / resolve / depth
    std::array<vk::AttachmentReference, kMaxColorAttachments> colorAttachmentRefs;
    std::array<vk::AttachmentReference, kMaxColorAttachments> resolveAttachmentRefs;
    vk::AttachmentReference depthAttachmentRef {};

    uint32_t attachmentCount = 0;
    for (size_t i = 0; i < colorAttachmentRefs.size(); i++) {
        auto& attachmentRef = colorAttachmentRefs[i];

        if (query.attachments[i].format == vk::Format::eUndefined) {
            attachmentRef.attachment = VK_ATTACHMENT_UNUSED;
            attachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;
            continue;
        }

        auto& attachmentDesc = query.attachments[i];
        attachmentRef.attachment = attachmentCount;
        attachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

        auto& attachment = attachments[attachmentCount++];
        attachment.format = attachmentDesc.format;
        attachment.samples = query.samples;
        attachment.loadOp = attachmentDesc.loadOp;
        attachment.storeOp = attachmentDesc.storeOp;
        attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        attachment.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
        attachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
    }

    for (size_t i = 0; i < resolveAttachmentRefs.size(); i++) {
        auto& attachmentRef = resolveAttachmentRefs[i];

        if (!query.hasResolve[i]) {
            attachmentRef.attachment = VK_ATTACHMENT_UNUSED;
            attachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;
            continue;
        }

        auto& attachmentDesc = query.attachments[i];
        attachmentRef.attachment = attachmentCount;
        attachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

        auto& attachment = attachments[attachmentCount++];
        attachment.format = attachmentDesc.format;
        attachment.samples = vk::SampleCountFlagBits::e1;
        attachment.loadOp = vk::AttachmentLoadOp::eDontCare;
        attachment.storeOp = vk::AttachmentStoreOp::eStore;
        attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        attachment.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
        attachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
    }

    auto& depthQuery = query.attachments[kMaxColorAttachments];
    depthAttachmentRef.attachment = depthQuery.format != vk::Format::eUndefined ? attachmentCount : VK_ATTACHMENT_UNUSED;
    depthAttachmentRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    if (depthQuery.format != vk::Format::eUndefined) {
        auto& depthAttachment = attachments[attachmentCount++];
        depthAttachment.format = depthQuery.format;
        depthAttachment.samples = query.samples;
        depthAttachment.loadOp = depthQuery.loadOp;
        depthAttachment.storeOp = depthQuery.storeOp;
        depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        depthAttachment.initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    }

    vk::SubpassDescription subpass{};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = colorAttachmentRefs.size();
    subpass.pColorAttachments = colorAttachmentRefs.data();
    subpass.pResolveAttachments = resolveAttachmentRefs.data();
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    vk::SubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
    dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
    dependency.dstStageMask = vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
    dependency.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

    vk::RenderPassCreateInfo renderPassInfo{};
    renderPassInfo.attachmentCount = attachmentCount;
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    m_renderPass = m_device.createRenderPass(renderPassInfo);
}

RenderPass::~RenderPass() {
    m_device.destroyRenderPass(m_renderPass);
}

RenderPass& RenderPassCache::getOrCreate(const RenderPassDescription& description,
                                         const gpu::FrameBufferFormat& format) {
    RenderPassCacheQuery query {};
    for (uint32_t i = 0; i < format.color.size(); i++) {
        auto& colorAttachmentDesc = query.attachments[i];
        colorAttachmentDesc.format = format.color[i];
        colorAttachmentDesc.loadOp = description.color[i].load;
        colorAttachmentDesc.storeOp = description.color[i].store;
    }

    constexpr auto depthAttachmentIndex = kMaxColorAttachments;
    auto& depthAttachmentDesc = query.attachments[depthAttachmentIndex];
    depthAttachmentDesc.format = format.depth;
    depthAttachmentDesc.loadOp = description.depth.load;
    depthAttachmentDesc.storeOp = description.depth.store;

    query.hasResolve = format.hasResolve;
    query.samples = format.samples;

    auto [it, result] = m_cache.tryEmplace(query, m_device, query);
    return it->second;
}

void RenderPassCache::clear() {
    m_cache.clear();
}
}
