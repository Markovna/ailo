#define VMA_IMPLEMENTATION
#include "RenderAPI.h"
#include "vulkan/VulkanUtils.h"
#include "SwapChain.h"
#include <iostream>
#include <stdexcept>

#include "entt/entity/view.hpp"

namespace ailo {

// Initialization and shutdown

RenderAPI::RenderAPI(GLFWwindow* window)
    : m_window(window),
    m_device(window),
    m_commandPool(m_device->createCommandPool(vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, m_device.graphicsQueueFamilyIndex()))),
    m_commands(*m_device, m_commandPool),
    m_descriptorPool(createDescriptorPoolS(*m_device)),
    m_Allocator(createAllocator(m_device.instance(), m_device.physicalDevice(), *m_device)),
    m_framebufferCache(*m_device),
    m_renderPassCache(*m_device),
    m_pipelineCache(*m_device, m_graphicsPipelines) {

    createSwapchain();
}

RenderAPI::~RenderAPI() = default;

void RenderAPI::shutdown() {
    m_device->waitIdle();

    cleanupSwapchain();

    m_framebufferCache.clear();
    m_renderPassCache.clear();
    m_pipelineCache.clear();

    m_buffers.clear();
    m_descriptorSetLayouts.clear();
    m_descriptorSets.clear();
    m_textures.clear();
    m_programs.clear();
    m_graphicsPipelines.clear();
    m_vertexBufferLayouts.clear();

    m_commands.destroy();

    destroyStageBuffers();

    cleanupDescriptorSets();

    vmaDestroyAllocator(m_Allocator);

    m_device->destroyDescriptorPool(m_descriptorPool);
    m_device->destroyCommandPool(m_commandPool);
}

// Frame lifecycle

bool RenderAPI::beginFrame() {
    UniqueVkHandle acquireSemaphore { *m_device, m_device->createSemaphore(vk::SemaphoreCreateInfo{}) };

    auto result = m_swapChain->acquireNextImage(*m_device, acquireSemaphore.get(), UINT64_MAX);
    m_commands.get().setSubmitSignal(std::move(acquireSemaphore));

    if (result == vk::Result::eErrorOutOfDateKHR) {
        recreateSwapchain();
        return false;
    }

    if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    return true;
}

void RenderAPI::endFrame() {
    auto& commands = m_commands.get();

    auto result = m_swapChain->present(commands, m_device.graphicsQueue(), m_device.presentQueue());

    if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR || m_framebufferResized) {
        m_framebufferResized = false;
        recreateSwapchain();
    } else if (result != vk::Result::eSuccess) {
        throw std::runtime_error("failed to present swap chain image!");
    }

    // free resources acquired by command buffer
    destroyStageBuffers();
    cleanupDescriptorSets();

    m_commands.next();
}

void RenderAPI::cleanupDescriptorSets() {
    const auto [first, last] =
        std::ranges::remove_if(m_descriptorSetsToDestroy, [this](const DescriptorSet& descriptorSet) {
            if (descriptorSet.isBound()) {
                return false;
            }

            (void) m_device->freeDescriptorSets(m_descriptorPool, 1, &descriptorSet.descriptorSet);
            return true;
        });

    m_descriptorSetsToDestroy.erase(first, last);
}

void RenderAPI::waitIdle() {
    m_device->waitIdle();
}

VertexBufferLayoutHandle RenderAPI::createVertexBufferLayout(const VertexInputDescription& description) {
    auto [handle, vbl] = m_vertexBufferLayouts.emplace();
    for(size_t i = 0; i < vbl.attributes.size(); i++) {
        if (i < description.attributes.size()) {
            vbl.attributes[i] = description.attributes[i];
            continue;
        }

        vbl.attributes[i].binding = std::numeric_limits<uint32_t>::max();
    }

    for (size_t i = 0; i < description.bindings.size(); i++) {
        vbl.bindings[i] = description.bindings[i];
    }
    vbl.attributesCount = static_cast<uint32_t>(description.attributes.size());
    vbl.bindingsCount = static_cast<uint32_t>(description.bindings.size());

    return handle;
}

void RenderAPI::destroyVertexBufferLayout(VertexBufferLayoutHandle handle) {
    m_vertexBufferLayouts.erase(handle);
}

// Buffer management

BufferHandle RenderAPI::createVertexBuffer(const void* data, uint64_t size) {
    auto [handle, vertexBuffer] = m_buffers.emplace();
    allocateBuffer(vertexBuffer, vk::BufferUsageFlagBits::eVertexBuffer, size);
    vertexBuffer.binding = BufferBinding::VERTEX;

    auto& commands = m_commands.get();
    if(data != nullptr) {
        loadFromCpu(*commands, vertexBuffer, data, 0, size);
    }
    return handle;
}

BufferHandle RenderAPI::createIndexBuffer(const void* data, uint64_t size) {
    // Create staging buffer
    auto [handle, indexBuffer] = m_buffers.emplace();
    allocateBuffer(indexBuffer, vk::BufferUsageFlagBits::eIndexBuffer, size);
    indexBuffer.binding = BufferBinding::INDEX;
    auto& commands = m_commands.get();
    if(data != nullptr) {
        loadFromCpu(*commands, indexBuffer, data, 0, size);
    }
    return handle;
}

VmaAllocator RenderAPI::createAllocator(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device) {
  VmaAllocator allocator;
  VmaVulkanFunctions const funcs {
#if VMA_DYNAMIC_VULKAN_FUNCTIONS
      .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
      .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
#else
      .vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties,
      .vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties,
      .vkAllocateMemory = vkAllocateMemory,
      .vkFreeMemory = vkFreeMemory,
      .vkMapMemory = vkMapMemory,
      .vkUnmapMemory = vkUnmapMemory,
      .vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges,
      .vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges,
      .vkBindBufferMemory = vkBindBufferMemory,
      .vkBindImageMemory = vkBindImageMemory,
      .vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements,
      .vkGetImageMemoryRequirements = vkGetImageMemoryRequirements,
      .vkCreateBuffer = vkCreateBuffer,
      .vkDestroyBuffer = vkDestroyBuffer,
      .vkCreateImage = vkCreateImage,
      .vkDestroyImage = vkDestroyImage,
      .vkCmdCopyBuffer = vkCmdCopyBuffer,
      .vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2KHR,
      .vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2KHR
#endif
  };
  VmaAllocatorCreateInfo const allocatorInfo {
      // Disable the internal VMA synchronization because the backend is singled threaded.
      // Improve CPU performance when using VMA functions. The backend will guarantee that all
      // access to VMA is done in a thread safe way.
      .flags = VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT,
      .physicalDevice = physicalDevice,
      .device = device,
      .pVulkanFunctions = &funcs,
      .instance = instance,
  };
  vmaCreateAllocator(&allocatorInfo, &allocator);
  return allocator;
}

void RenderAPI::allocateBuffer(Buffer& buffer, vk::BufferUsageFlags usageFlags, uint32_t numBytes) {
    VkBufferCreateInfo const bufferInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = numBytes,
        .usage = (VkBufferUsageFlags) (usageFlags | vk::BufferUsageFlagBits::eTransferDst),
    };

    VmaAllocationCreateFlags vmaFlags = 0;

    // TODO: In the case of UMA, the buffers will always be mappable
    // if(isUMA()) {
    //     vmaFlags |= VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    // }

    buffer.size = numBytes;

    VmaAllocationCreateInfo const allocInfo {
        .flags = vmaFlags,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };

    VkBuffer vkBuffer;
    VkResult result = vmaCreateBuffer(
        m_Allocator, &bufferInfo, &allocInfo, &vkBuffer, &buffer.vmaAllocation, &buffer.allocationInfo);

    buffer.buffer = vkBuffer;
}

BufferHandle RenderAPI::createBuffer(BufferBinding bufferBinding, uint64_t size) {
  auto [handle, buffer] = m_buffers.emplace();
  auto usageFlags = vkutils::getBufferUsage(bufferBinding);
  allocateBuffer(buffer, usageFlags, size);
  buffer.binding = bufferBinding;
  return handle;
}

void RenderAPI::destroyBuffer(const BufferHandle& handle) {
  if(!handle) { return; }

  auto& buffer = m_buffers.get(handle);
  vmaDestroyBuffer(m_Allocator, buffer.buffer, buffer.vmaAllocation);
  m_buffers.erase(handle);
}

void RenderAPI::updateBuffer(const BufferHandle& handle, const void* data, uint64_t size, uint64_t byteOffset) {
    auto& buffer = m_buffers.get(handle);
    auto& commands = m_commands.get();
    loadFromCpu(*commands, buffer, data, byteOffset, size);
}

// Texture management

TextureHandle RenderAPI::createTexture(vk::Format format, uint32_t width, uint32_t height, vk::Filter filter, uint8_t levels) {
    auto ptr = resource_ptr<Texture>::make(m_textures, *m_device, m_device.physicalDevice(), format, levels, width, height, filter, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::ImageAspectFlagBits::eColor);
    ptr->acquire(ptr);
    return ptr.getHandle();
}

void RenderAPI::destroyTexture(const TextureHandle& handle) {
    if (!handle) { return; }

    auto& texture = m_textures.get(handle);
    texture.release();
}

void RenderAPI::updateTextureImage(const TextureHandle& handle, const void* data, size_t dataSize, uint32_t width, uint32_t height, uint32_t xOffset, uint32_t yOffset) {
    auto& texture = m_textures.get(handle);

    // allocate stage buffer
    auto stageBuffer = allocateStageBuffer(dataSize);

    // mem copy to stage buffer
    memcpy(stageBuffer.mapping, data, dataSize);
    vmaFlushAllocation(m_Allocator, stageBuffer.vmaAllocation, 0, dataSize);

    vk::CommandBuffer commandBuffer = *m_commands.get();

    texture.transitionLayout(commandBuffer, vk::ImageLayout::eTransferDstOptimal);

    if(width == 0) width = texture.width;
    if(height == 0) height = texture.height;

    copyBufferToImage(commandBuffer, stageBuffer.buffer, texture.image, width, height, xOffset, yOffset);

    // Transition image layout to shader read
    texture.transitionLayout(commandBuffer, vk::ImageLayout::eShaderReadOnlyOptimal);
}

void RenderAPI::generateMipmaps(const TextureHandle& handle) {
    auto& texture = m_textures.get(handle);

    int32_t width = texture.width;
    int32_t height = texture.height;

    auto oldLayout = texture.getLayout(0);

    uint8_t levelCount = texture.getLevels();
    for (uint32_t level = 1; level < levelCount && (width > 1 || height > 1); level++) {
        int32_t dstWidth = std::max(width >> 1, 1);
        int32_t dstHeight = std::max(height >> 1, 1);

        vk::ImageSubresourceRange srcRange {};
        srcRange.aspectMask = texture.aspect;
        srcRange.baseMipLevel = level - 1;
        srcRange.levelCount = 1u;
        srcRange.baseArrayLayer = 0u;
        srcRange.layerCount = 1u;

        vk::ImageSubresourceRange dstRange = srcRange;
        dstRange.baseMipLevel = level;
        dstRange.levelCount = 1;

        texture.transitionLayout(*m_commands.get(), vk::ImageLayout::eTransferSrcOptimal, srcRange);
        texture.transitionLayout(*m_commands.get(), vk::ImageLayout::eTransferDstOptimal, dstRange);

        vk::ImageBlit blit{};
        blit.srcOffsets[0] = vk::Offset3D{ 0, 0, 0 };
        blit.srcOffsets[1] = vk::Offset3D{ width, height, 1 };
        blit.srcSubresource.aspectMask = srcRange.aspectMask;
        blit.srcSubresource.mipLevel = srcRange.baseMipLevel;
        blit.srcSubresource.baseArrayLayer = srcRange.baseArrayLayer;
        blit.srcSubresource.layerCount = srcRange.layerCount;
        blit.dstOffsets[0] = vk::Offset3D{ 0, 0, 0 };
        blit.dstOffsets[1] = vk::Offset3D{ dstWidth, dstHeight, 1 };
        blit.dstSubresource.aspectMask = dstRange.aspectMask;
        blit.dstSubresource.mipLevel = dstRange.baseMipLevel;
        blit.dstSubresource.baseArrayLayer = dstRange.baseArrayLayer;
        blit.dstSubresource.layerCount = dstRange.layerCount;

        m_commands.get()->blitImage(texture.image, vk::ImageLayout::eTransferSrcOptimal, texture.image, vk::ImageLayout::eTransferDstOptimal, 1, &blit, vk::Filter::eLinear);
        width = dstWidth;
        height = dstHeight;
    }

    texture.transitionLayout(*m_commands.get(), oldLayout);
}

// Descriptor set management

DescriptorSetLayoutHandle RenderAPI::createDescriptorSetLayout(const std::vector<DescriptorSetLayoutBinding>& bindings) {
    auto [handle, descriptorSetLayout] = m_descriptorSetLayouts.emplace();

    std::vector<vk::DescriptorSetLayoutBinding> vkBindings;
    vkBindings.resize(bindings.size());
    for(auto i = 0; i < bindings.size(); i++) {
      vkBindings[i].binding = bindings[i].binding;
      vkBindings[i].descriptorType = bindings[i].descriptorType;
      vkBindings[i].stageFlags = bindings[i].stageFlags;
      vkBindings[i].descriptorCount = 1;
    }

    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.bindingCount = static_cast<uint32_t>(vkBindings.size());
    layoutInfo.pBindings = vkBindings.data();

    descriptorSetLayout.layout = m_device->createDescriptorSetLayout(layoutInfo);
    for(auto& binding : bindings) {
      if(binding.descriptorType == vk::DescriptorType::eUniformBufferDynamic) {
        descriptorSetLayout.dynamicBindings.set(binding.binding, true);
      }
    }

    return handle;
}

void RenderAPI::destroyDescriptorSetLayout(DescriptorSetLayoutHandle& handle) {
  if(!handle) { return; }

  auto& descriptorSetLayout = m_descriptorSetLayouts.get(handle);
  m_device->destroyDescriptorSetLayout(descriptorSetLayout.layout);
  m_descriptorSetLayouts.erase(handle);
}

DescriptorSetHandle RenderAPI::createDescriptorSet(DescriptorSetLayoutHandle layoutHandle) {
    auto [handle, descriptorSet] = m_descriptorSets.emplace();

    createDescriptorSet(descriptorSet, layoutHandle);

    return handle;
}

void RenderAPI::createDescriptorSet(DescriptorSet& descriptorSet, DescriptorSetLayoutHandle layoutHandle) {
    auto& descriptorSetLayout = m_descriptorSetLayouts.get(layoutHandle);
    auto& layout = descriptorSetLayout.layout;

    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    auto result = m_device->allocateDescriptorSets(allocInfo);
    descriptorSet.descriptorSet = result[0];
    descriptorSet.boundBindings.reset();
    descriptorSet.dynamicBindings = descriptorSetLayout.dynamicBindings;
    descriptorSet.layoutHandle = layoutHandle;
    descriptorSet.boundFence = nullptr;
}

void RenderAPI::destroyDescriptorSet(const DescriptorSetHandle& handle) {
    if(!handle) { return; }

    auto& descriptorSet = m_descriptorSets.get(handle);
    m_descriptorSetsToDestroy.push_back(descriptorSet);
    m_descriptorSets.erase(handle);
}

void RenderAPI::updateDescriptorSetBuffer(const DescriptorSetHandle& descriptorSetHandle, const BufferHandle& bufferHandle, uint32_t binding, uint64_t offset, uint64_t size) {
    if (!descriptorSetHandle) {
        return;
    }

    auto& descriptorSet = m_descriptorSets.get(descriptorSetHandle);
    auto& buffer = m_buffers.get(bufferHandle);

    vk::DescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = buffer.buffer;
    bufferInfo.offset = offset;
    bufferInfo.range = size == std::numeric_limits<decltype(size)>::max() ? buffer.size : size;

    bool isDynamic = descriptorSet.dynamicBindings[binding];

    vk::WriteDescriptorSet descriptorWrite{};
    descriptorWrite.dstSet = descriptorSet.descriptorSet;
    descriptorWrite.dstBinding = binding;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = isDynamic ? vk::DescriptorType::eUniformBufferDynamic : vk::DescriptorType::eUniformBuffer;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    m_device->updateDescriptorSets(1, &descriptorWrite, 0, nullptr);
    descriptorSet.boundBindings[binding] = true;
}

void RenderAPI::updateDescriptorSetTexture(const DescriptorSetHandle& descriptorSetHandle, const TextureHandle& textureHandle, uint32_t binding) {
    if(!descriptorSetHandle) {
        return;
    }

    auto& descriptorSet = m_descriptorSets.get(descriptorSetHandle);
    auto& texture = m_textures.get(textureHandle);

    if (descriptorSet.isBound()) {
        // re-create descriptor set
        m_descriptorSetsToDestroy.push_back(descriptorSet);

        DescriptorSet newDescriptorSet;
        createDescriptorSet(newDescriptorSet, descriptorSet.layoutHandle);

        std::vector<vk::CopyDescriptorSet> copyDescriptors;
        for(size_t i = 0; i < descriptorSet.boundBindings.size(); i++) {
            if (!descriptorSet.boundBindings[i]) {
                continue;
            }
            vk::CopyDescriptorSet copyDescriptorSet {};
            copyDescriptorSet.srcSet = descriptorSet.descriptorSet;
            copyDescriptorSet.srcBinding = i;
            copyDescriptorSet.srcArrayElement = 0;
            copyDescriptorSet.dstSet = newDescriptorSet.descriptorSet;
            copyDescriptorSet.dstBinding = i;
            copyDescriptorSet.dstArrayElement = 0;

            copyDescriptors.push_back(copyDescriptorSet);
        }
        m_device->updateDescriptorSets(0, nullptr, copyDescriptors.size(), copyDescriptors.data());

        std::swap(descriptorSet, newDescriptorSet);
    }

    vk::DescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    imageInfo.imageView = texture.imageView;
    imageInfo.sampler = texture.sampler;

    vk::WriteDescriptorSet descriptorWrite{};
    descriptorWrite.dstSet = descriptorSet.descriptorSet;
    descriptorWrite.dstBinding = binding;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    m_device->updateDescriptorSets(1, &descriptorWrite, 0, nullptr);
    descriptorSet.boundBindings[binding] = true;
}

void RenderAPI::bindDescriptorSet(const DescriptorSetHandle& descriptorSetHandle, uint32_t setIndex, std::initializer_list<uint32_t> dynamicOffsets) {
  auto pipelineLayout = m_pipelineCache.pipelineLayout();
  assert(descriptorSetHandle);
  auto& descriptorSet = m_descriptorSets.get(descriptorSetHandle);

  std::array<uint32_t, 32> dynamicOffsetsArray { };
  assert(dynamicOffsets.size() <= dynamicOffsetsArray.size());
  std::copy(dynamicOffsets.begin(), dynamicOffsets.end(), dynamicOffsetsArray.begin());

  auto& commands = m_commands.get();
  commands->bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipelineLayout,
        setIndex,
        1,
        &descriptorSet.descriptorSet,
        static_cast<uint32_t>(dynamicOffsets.size()),
        dynamicOffsetsArray.data()
    );

    descriptorSet.boundFence = commands.getFenceStatusShared();
}

// Pipeline management

ProgramHandle RenderAPI::createProgram(const ShaderDescription& description) {
    auto ptr = resource_ptr<gpu::Program>::make(m_programs, *m_device, description);
    ptr->acquire(ptr);
    return ptr.getHandle();
}

void RenderAPI::destroyProgram(const ProgramHandle& handle) {
    auto& program = m_programs.get(handle);
    program.release();
}

// Command recording

void RenderAPI::beginRenderPass(const RenderPassDescription& description, vk::ClearColorValue clearColor) {
    auto colorTarget = m_swapChain->getColorTarget();
    auto resolveTarget = m_swapChain->getResolveTarget();
    auto depthTarget = m_swapChain->getDepthTarget();

    gpu::FrameBufferFormat fbFormat {
        .color = { colorTarget->format },
        .depth = depthTarget->format,
        .samples = vk::SampleCountFlagBits::e1
    };

    gpu::FrameBufferImageView fbImageView {
        .color = { colorTarget->imageView },
        .depth = depthTarget->imageView
    };

    if (resolveTarget) {
        fbImageView.resolve[0] = resolveTarget->imageView;
        fbFormat.hasResolve[0] = true;
        fbFormat.samples = colorTarget->getSamples();
    }

    auto& renderPass = m_renderPassCache.getOrCreate(description, fbFormat);
    auto& frameBuffer = m_framebufferCache.getOrCreate(renderPass, fbFormat, fbImageView, colorTarget->width, colorTarget->height);

    CommandBuffer& commandBuffer = m_commands.get();
    colorTarget->transitionLayout(*commandBuffer, vk::ImageLayout::eColorAttachmentOptimal);
    depthTarget->transitionLayout(*commandBuffer, vk::ImageLayout::eDepthStencilAttachmentOptimal);
    if (resolveTarget) {
        resolveTarget->transitionLayout(*commandBuffer, vk::ImageLayout::eColorAttachmentOptimal);
    }

    m_pipelineCache.bindRenderPass(renderPass, fbFormat);

    vk::Extent2D extent { colorTarget->width, colorTarget->height };
    vk::Rect2D rect { { 0, 0 }, extent};

    vk::RenderPassBeginInfo renderPassInfo{};
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = frameBuffer;
    renderPassInfo.renderArea = rect;

    std::array<vk::ClearValue, 2 * fbImageView.color.size() + 1> clearValues;
    uint32_t clearValuesCount = 0;
    for (size_t i = 0; i < fbImageView.color.size(); i++) {
        if (fbImageView.color[i] != VK_NULL_HANDLE) {
            clearValues[clearValuesCount++].color = clearColor;
        }
    }
    for (size_t i = 0; i < fbImageView.resolve.size(); i++) {
        if (fbImageView.resolve[i] != VK_NULL_HANDLE) {
            clearValues[clearValuesCount++].color = clearColor;
        }
    }
    const vk::ClearDepthStencilValue depthStencilClearValue { 1.0f, 0 };
    clearValues[clearValuesCount++].depthStencil = depthStencilClearValue;
    renderPassInfo.clearValueCount = clearValuesCount;
    renderPassInfo.pClearValues = clearValues.data();

    commandBuffer->beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

    // Set default viewport and scissor
    vk::Viewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    commandBuffer->setViewport(0, 1, &viewport);

    vk::Rect2D scissor {{0, 0}, extent };
    commandBuffer->setScissor(0, 1, &scissor);
}

void RenderAPI::endRenderPass() {
    auto& commands = m_commands.get();
    commands->endRenderPass();
}

void RenderAPI::bindPipeline(const PipelineState& state) {
    auto& program = m_programs.get(state.program);
    auto& vertexLayout = m_vertexBufferLayouts.get(state.vertexBufferLayout);

    m_pipelineCache.bindProgram(program.getSharedPtr());
    m_pipelineCache.bindVertexLayout(vertexLayout);
}

void RenderAPI::bindVertexBuffer(const BufferHandle& handle) {
    auto& buffer = m_buffers.get(handle);
    vk::Buffer vertexBuffers[] = {buffer.buffer};
    vk::DeviceSize offsets[] = {0};
    auto& commands = m_commands.get();
    commands->bindVertexBuffers(0, 1, vertexBuffers, offsets);
}

void RenderAPI::bindIndexBuffer(const BufferHandle& handle, vk::IndexType indexType) {
    auto& buffer = m_buffers.get(handle);
    auto& commands = m_commands.get();
    commands->bindIndexBuffer(buffer.buffer, 0, indexType);
}

void RenderAPI::drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset) {
    auto& commands = m_commands.get();
    auto pipeline = m_pipelineCache.getOrCreate();
    assert(pipeline);

    commands->bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    commands->drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, 0);
}

void RenderAPI::setViewport(float x, float y, float width, float height) {
    vk::Viewport viewport{x, y, width, height, 0.0f, 1.0f};
    auto& commands = m_commands.get();
    commands->setViewport(0, 1, &viewport);
}

void RenderAPI::setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) {
    vk::Rect2D scissor{{x, y}, {width, height}};
    auto& commands = m_commands.get();
    commands->setScissor(0, 1, &scissor);
}

// Swapchain management

void RenderAPI::handleWindowResize() {
    m_framebufferResized = true;
}

// Internal initialization

void RenderAPI::createSwapchain() {
    m_swapChain = std::make_unique<SwapChain>(m_device);
}

vk::DescriptorPool RenderAPI::createDescriptorPoolS(vk::Device device) {
    // Create a descriptor pool that can allocate descriptor sets
    // We'll allocate enough for a reasonable number of uniform buffers and textures
    std::array poolSizes {
        vk::DescriptorPoolSize {vk::DescriptorType::eUniformBuffer, 500 },
        vk::DescriptorPoolSize {vk::DescriptorType::eUniformBufferDynamic, 500 },
        vk::DescriptorPoolSize {vk::DescriptorType::eCombinedImageSampler, 500 }
    };
    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1000;
    poolInfo.flags |= vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;

    return device.createDescriptorPool(poolInfo);
}

void RenderAPI::cleanupSwapchain() {
    m_swapChain->destroy(*m_device);
}

void RenderAPI::recreateSwapchain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(m_window, &width, &height);
        glfwWaitEvents();
    }

    m_device->waitIdle();

    cleanupSwapchain();
    createSwapchain();
}

void RenderAPI::copyBufferToImage(vk::CommandBuffer commandBuffer, vk::Buffer buffer, vk::Image image, uint32_t width, uint32_t height, uint32_t xOffset, uint32_t yOffset) {
    vk::BufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = vk::Offset3D{static_cast<int32_t>(xOffset), static_cast<int32_t>(yOffset), 0};
    region.imageExtent = vk::Extent3D{width, height, 1};

    commandBuffer.copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, 1, &region);
}

gpu::StageBuffer RenderAPI::allocateStageBuffer(uint32_t capacity) {
    VkBufferCreateInfo bufferInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = capacity,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    };
    VmaAllocationCreateInfo allocInfo { .usage = VMA_MEMORY_USAGE_CPU_ONLY };
    VkBuffer buffer;
    VmaAllocation memory;
    VkResult result = vmaCreateBuffer(m_Allocator, &bufferInfo, &allocInfo, &buffer, &memory, nullptr);

    void* pMapping = nullptr;
    if(result == VK_SUCCESS) {
        result = vmaMapMemory(m_Allocator, memory, &pMapping);
    }
    StageBuffer stageBuffer {
      .buffer = buffer,
      .size = capacity,
      .vmaAllocation = memory,
      .mapping = pMapping
    };
    stageBuffer.setFence(m_commands.get().getFenceStatusShared());
    m_stageBuffers.push_back(stageBuffer);
    return stageBuffer;
}

void RenderAPI::destroyStageBuffers() {
    const auto [first, last] =
        std::ranges::remove_if(m_stageBuffers, [this](const StageBuffer& stageBuffer) {
            if (stageBuffer.isAcquired()) {
                return false;
            }

            vmaUnmapMemory(m_Allocator, stageBuffer.vmaAllocation);
            vmaDestroyBuffer(m_Allocator, stageBuffer.buffer, stageBuffer.vmaAllocation);
            return true;
        });

    m_stageBuffers.erase(first, last);
}

void getReadBarrierAccessAndStage(BufferBinding bufferBinding, VkAccessFlags& access, VkPipelineStageFlags& stage) {
  if (bufferBinding == BufferBinding::UNIFORM) {
    access = VK_ACCESS_SHADER_READ_BIT;
    stage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else if (bufferBinding == BufferBinding::VERTEX) {
    access = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    stage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
  } else if (bufferBinding == BufferBinding::INDEX) {
    access = VK_ACCESS_INDEX_READ_BIT;
    stage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
  }

}

void RenderAPI::loadFromCpu(vk::CommandBuffer& commandBuffer, const Buffer& bufferHandle, const void* data, uint32_t byteOffset, uint32_t numBytes) {
  // allocate stage buffer
  auto stageBuffer = allocateStageBuffer(numBytes);

  // mem copy to stage buffer
  memcpy(stageBuffer.mapping, data, numBytes);
  vmaFlushAllocation(m_Allocator, stageBuffer.vmaAllocation, 0, numBytes);

  VkAccessFlags srcAccess = 0;
  VkPipelineStageFlags srcStage = 0;
  getReadBarrierAccessAndStage(bufferHandle.binding, srcAccess, srcStage);

  // TODO: we might want to skip this barrier in some cases, e.g if buffer is static
  {
    VkBufferMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = srcAccess,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = bufferHandle.buffer,
        .offset = byteOffset,
        .size = numBytes,
    };
    vkCmdPipelineBarrier(commandBuffer, srcStage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                         nullptr, 1, &barrier, 0, nullptr);
  }

  VkBufferCopy region = {
      .srcOffset = 0,
      .dstOffset = byteOffset,
      .size = numBytes,
  };
  vkCmdCopyBuffer(commandBuffer, stageBuffer.buffer, bufferHandle.buffer, 1, &region);

  VkAccessFlags dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | srcAccess;
  VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT | srcStage;

  VkBufferMemoryBarrier barrier = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstAccessMask = dstAccessMask,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .buffer = bufferHandle.buffer,
      .offset = byteOffset,
      .size = numBytes,
  };

  vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, dstStageMask, 0, 0,
                       nullptr, 1, &barrier, 0, nullptr);
}

} // namespace ailo
