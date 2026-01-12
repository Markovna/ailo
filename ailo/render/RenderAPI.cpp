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
    m_commandPool(m_device.device().createCommandPool(vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, m_device.graphicsQueueFamilyIndex()))),
    m_commands(m_device.device(), m_commandPool),
    m_descriptorPool(createDescriptorPoolS(m_device.device())),
    m_Allocator(createAllocator(m_device.instance(), m_device.physicalDevice(), m_device.device())),
    m_framebufferCache(m_device.device()) {

    createSwapchain();
    createRenderPass();
}

RenderAPI::~RenderAPI() = default;

void RenderAPI::shutdown() {
    vk::Device device = m_device.device();

    device.waitIdle();

    cleanupSwapchain();

    m_framebufferCache.clear();

    m_commands.destroy();

    destroyStageBuffers();

    cleanupDescriptorSets();

    vmaDestroyAllocator(m_Allocator);

    device.destroyRenderPass(m_renderPass);
    device.destroyDescriptorPool(m_descriptorPool);
    device.destroyCommandPool(m_commandPool);
}

// Frame lifecycle

bool RenderAPI::beginFrame() {
    auto device = m_device.device();
    UniqueVkHandle acquireSemaphore { device, device.createSemaphore(vk::SemaphoreCreateInfo{}) };

    auto result = m_swapChain->acquireNextImage(device, acquireSemaphore.get(), UINT64_MAX);
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

            (void) m_device.device().freeDescriptorSets(m_descriptorPool, 1, &descriptorSet.descriptorSet);
            return true;
        });

    m_descriptorSetsToDestroy.erase(first, last);
}

void RenderAPI::waitIdle() {
    m_device.device().waitIdle();
}

// Buffer management

BufferHandle RenderAPI::createVertexBuffer(const void* data, uint64_t size) {
    auto [handle, vertexBuffer] = m_buffers.emplace();
    allocateBuffer(vertexBuffer, vk::BufferUsageFlagBits::eVertexBuffer, size);
    vertexBuffer.binding = BufferBinding::VERTEX;

    auto& commands = m_commands.get();
    if(data != nullptr) {
        loadFromCpu(commands.buffer(), vertexBuffer, data, 0, size);
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
        loadFromCpu(commands.buffer(), indexBuffer, data, 0, size);
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
    loadFromCpu(commands.buffer(), buffer, data, byteOffset, size);
}

// Texture management

TextureHandle RenderAPI::createTexture(vk::Format format, uint32_t width, uint32_t height, vk::Filter filter) {
    auto [handle, texture] = m_textures.emplace(m_device.device(), m_device.physicalDevice(), format, width, height, filter, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::ImageAspectFlagBits::eColor);
    return handle;
}

void RenderAPI::destroyTexture(const TextureHandle& handle) {
    if(!handle) { return; }

    m_textures.erase(handle);
}

void RenderAPI::updateTextureImage(const TextureHandle& handle, const void* data, size_t dataSize, uint32_t width, uint32_t height, uint32_t xOffset, uint32_t yOffset) {
    auto& texture = m_textures.get(handle);

    // allocate stage buffer
    auto stageBuffer = allocateStageBuffer(dataSize);

    // mem copy to stage buffer
    memcpy(stageBuffer.mapping, data, dataSize);
    vmaFlushAllocation(m_Allocator, stageBuffer.vmaAllocation, 0, dataSize);

    // Transition image layout to transfer destination
    transitionImageLayout(texture.image, texture.format,
                         vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);

    if(width == 0) width = texture.width;
    if(height == 0) height = texture.height;

    copyBufferToImage(stageBuffer.buffer, texture.image, width, height, xOffset, yOffset);

    // Transition image layout to shader read
    transitionImageLayout(texture.image, texture.format,
                         vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
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

    descriptorSetLayout.layout = m_device.device().createDescriptorSetLayout(layoutInfo);
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
  m_device.device().destroyDescriptorSetLayout(descriptorSetLayout.layout);
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

    auto result = m_device.device().allocateDescriptorSets(allocInfo);
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

    m_device.device().updateDescriptorSets(1, &descriptorWrite, 0, nullptr);
    descriptorSet.boundBindings[binding] = true;
}

void RenderAPI::updateDescriptorSetTexture(const DescriptorSetHandle& descriptorSetHandle, const TextureHandle& textureHandle, uint32_t binding) {
    if(!descriptorSetHandle) {
        return;
    }

    auto& descriptorSet = m_descriptorSets.get(descriptorSetHandle);
    auto& texture = m_textures.get(textureHandle);
    auto& device = m_device.device();

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
        device.updateDescriptorSets(0, nullptr, copyDescriptors.size(), copyDescriptors.data());

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

    device.updateDescriptorSets(1, &descriptorWrite, 0, nullptr);
    descriptorSet.boundBindings[binding] = true;
}

void RenderAPI::bindDescriptorSet(const DescriptorSetHandle& descriptorSetHandle, uint32_t setIndex, std::initializer_list<uint32_t> dynamicOffsets) {
  auto& currentPipeline = m_pipelines.get(m_currentPipeline);
  assert(descriptorSetHandle);
  auto& descriptorSet = m_descriptorSets.get(descriptorSetHandle);

  std::array<uint32_t, 32> dynamicOffsetsArray { };
  assert(dynamicOffsets.size() <= dynamicOffsetsArray.size());
  std::copy(dynamicOffsets.begin(), dynamicOffsets.end(), dynamicOffsetsArray.begin());

  auto& commands = m_commands.get();
  commands.buffer().bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        currentPipeline.layout,
        setIndex,
        1,
        &descriptorSet.descriptorSet,
        static_cast<uint32_t>(dynamicOffsets.size()),
        dynamicOffsetsArray.data()
    );

    descriptorSet.boundFence = commands.getFenceStatusShared();
}

void RenderAPI::createRenderPass() {
    // Create render pass
    vk::AttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapChain->getColorFormat();
    colorAttachment.samples = vk::SampleCountFlagBits::e1;
    colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
    colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
    colorAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

    vk::AttachmentDescription depthAttachment{};
    depthAttachment.format = m_device.getDepthFormat();
    depthAttachment.samples = vk::SampleCountFlagBits::e1;
    depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
    depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    depthAttachment.initialLayout = vk::ImageLayout::eUndefined;
    depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::AttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::AttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::SubpassDescription subpass{};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    vk::SubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
    dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
    dependency.dstStageMask = vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests;
    dependency.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

    std::array<vk::AttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};
    vk::RenderPassCreateInfo renderPassInfo{};
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    m_renderPass = m_device.device().createRenderPass(renderPassInfo);
}

// Pipeline management

PipelineHandle RenderAPI::createGraphicsPipeline(const PipelineDescription& description) {

    vk::Device device = m_device.device();

    auto [handle, pipeline] = m_pipelines.emplace();
    const ShaderDescription& shader = description.shader;

    // Load shaders
    auto vertShaderCode = shader.vertexShader;
    auto fragShaderCode = shader.fragmentShader;

    vk::ShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    vk::ShaderModule fragShaderModule = createShaderModule(fragShaderCode);

    vk::PipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    vk::PipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    vk::PipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    auto vertexInput = description.vertexInput;
    // Vertex input
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInput.bindings.size());
    vertexInputInfo.pVertexBindingDescriptions = vertexInput.bindings.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInput.attributes.size());
    vertexInputInfo.pVertexAttributeDescriptions = vertexInput.attributes.data();

    // Input assembly
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport state
    vk::PipelineViewportStateCreateInfo viewportState{};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    auto& raster = shader.raster;

    // Rasterization
    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = vk::PolygonMode::eFill;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = vkutils::getCullMode(raster.cullingMode);
    rasterizer.frontFace = raster.inverseFrontFace ? vk::FrontFace::eClockwise : vk::FrontFace::eCounterClockwise;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

    // Depth and stencil testing
    vk::PipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = raster.depthWriteEnable;
    depthStencil.depthCompareOp = vkutils::getCompareOperation(raster.depthCompareOp);
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color blending
    vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    colorBlendAttachment.blendEnable = raster.blendEnable;
    colorBlendAttachment.colorBlendOp = vkutils::getBlendOp(raster.rgbBlendOp);
    colorBlendAttachment.alphaBlendOp = vkutils::getBlendOp(raster.alphaBlendOp);
    colorBlendAttachment.srcColorBlendFactor = vkutils::getBlendFunction(raster.srcRgbBlendFunc);
    colorBlendAttachment.srcAlphaBlendFactor = vkutils::getBlendFunction(raster.srcAlphaBlendFunc);
    colorBlendAttachment.dstColorBlendFactor = vkutils::getBlendFunction(raster.dstRgbBlendFunc);
    colorBlendAttachment.dstAlphaBlendFactor = vkutils::getBlendFunction(raster.dstAlphaBlendFunc);

    vk::PipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Dynamic state
    std::array dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };
    vk::PipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    std::vector<vk::DescriptorSetLayout> setLayouts;
    auto& layout = shader.layout;
    for(auto& set : layout) {
      std::vector<vk::DescriptorSetLayoutBinding> bindings;
      for(auto& binding : set) {
        vk::DescriptorSetLayoutBinding vkBinding;
        vkBinding.binding = binding.binding;
        vkBinding.descriptorType = binding.descriptorType;
        vkBinding.stageFlags = binding.stageFlags;
        vkBinding.descriptorCount = 1;
        bindings.push_back(vkBinding);
      }

      vk::DescriptorSetLayoutCreateInfo layoutInfo {};
      layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
      layoutInfo.pBindings = bindings.data();

      auto descriptorSetLayout = device.createDescriptorSetLayout(layoutInfo);
      setLayouts.push_back(descriptorSetLayout);
    }

    pipeline.descriptorSetLayouts = setLayouts;

    // Pipeline layout
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.setLayoutCount = setLayouts.size();
    pipelineLayoutInfo.pSetLayouts = setLayouts.data();

    pipeline.layout = device.createPipelineLayout(pipelineLayoutInfo);

    // Create graphics pipeline
    vk::GraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipeline.layout;
    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.subpass = 0;

    auto result = device.createGraphicsPipeline(nullptr, pipelineInfo);
    if (result.result != vk::Result::eSuccess) {
        throw std::runtime_error("failed to create graphics pipeline!");
    }
    pipeline.pipeline = result.value;

    // Cleanup shader modules
    device.destroyShaderModule(vertShaderModule);
    device.destroyShaderModule(fragShaderModule);

    return handle;
}

void RenderAPI::destroyPipeline(const PipelineHandle& handle) {
    if(!handle) { return; }

    auto& pipeline = m_pipelines.get(handle);
    auto& device = m_device.device();

    device.destroyPipeline(pipeline.pipeline);
    for(auto descriptorSetLayout : pipeline.descriptorSetLayouts) {
      device.destroyDescriptorSetLayout(descriptorSetLayout);
    }
    device.destroyPipelineLayout(pipeline.layout);

    m_pipelines.erase(handle);
}

// Command recording

void RenderAPI::beginRenderPass(vk::ClearColorValue clearColor) {
    auto extent = m_swapChain->getExtent();

    FrameBufferCacheQuery frameBufferCacheQuery {
        .renderPass = m_renderPass,
        .attachmentCount = 1,
        .depth = m_swapChain->getDepthImage(),
        .width = extent.width,
        .height = extent.height
    };
    frameBufferCacheQuery.color[0] = m_swapChain->getCurrentImage();

    auto& frameBuffer = m_framebufferCache.getOrCreate(frameBufferCacheQuery);

    vk::RenderPassBeginInfo renderPassInfo{};
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.framebuffer = frameBuffer.getHandle();
    renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
    renderPassInfo.renderArea.extent = extent;

    std::array<vk::ClearValue, 2> clearValues{};
    clearValues[0].color = clearColor;
    clearValues[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();
    auto& commands = m_commands.get();

    commands.buffer().beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

    // Set default viewport and scissor
    vk::Viewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    commands.buffer().setViewport(0, 1, &viewport);

    vk::Rect2D scissor{};
    scissor.offset = vk::Offset2D{0, 0};
    scissor.extent = extent;
    commands.buffer().setScissor(0, 1, &scissor);
}

void RenderAPI::endRenderPass() {
    auto& commands = m_commands.get();
    commands.buffer().endRenderPass();
}

void RenderAPI::bindPipeline(const PipelineHandle& handle) {
    auto& currentPipeline = m_pipelines.get(handle);
    auto& commands = m_commands.get();
    commands.buffer().bindPipeline(vk::PipelineBindPoint::eGraphics, currentPipeline.pipeline);
    m_currentPipeline = handle;
}

void RenderAPI::bindVertexBuffer(const BufferHandle& handle) {
    auto& buffer = m_buffers.get(handle);
    vk::Buffer vertexBuffers[] = {buffer.buffer};
    vk::DeviceSize offsets[] = {0};
    auto& commands = m_commands.get();
    commands.buffer().bindVertexBuffers(0, 1, vertexBuffers, offsets);
}

void RenderAPI::bindIndexBuffer(const BufferHandle& handle, vk::IndexType indexType) {
    auto& buffer = m_buffers.get(handle);
    auto& commands = m_commands.get();
    commands.buffer().bindIndexBuffer(buffer.buffer, 0, indexType);
}

void RenderAPI::drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset) {
    auto& commands = m_commands.get();
    commands.buffer().drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, 0);
}

void RenderAPI::setViewport(float x, float y, float width, float height) {
    vk::Viewport viewport{x, y, width, height, 0.0f, 1.0f};
    auto& commands = m_commands.get();
    commands.buffer().setViewport(0, 1, &viewport);
}

void RenderAPI::setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) {
    vk::Rect2D scissor{{x, y}, {width, height}};
    auto& commands = m_commands.get();
    commands.buffer().setScissor(0, 1, &scissor);
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
    std::vector<vk::DescriptorPoolSize> poolSizes{};
    poolSizes.push_back({vk::DescriptorType::eUniformBuffer, 100});
    poolSizes.push_back({vk::DescriptorType::eUniformBufferDynamic, 100});
    poolSizes.push_back({vk::DescriptorType::eCombinedImageSampler, 100});

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 100;
    poolInfo.flags |= vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;

    return device.createDescriptorPool(poolInfo);
}

void RenderAPI::cleanupSwapchain() {
    m_swapChain->destroy(m_device.device());
}

void RenderAPI::recreateSwapchain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(m_window, &width, &height);
        glfwWaitEvents();
    }

    m_device.device().waitIdle();

    cleanupSwapchain();
    createSwapchain();
}

vk::ShaderModule RenderAPI::createShaderModule(const std::vector<char>& code) {
    vk::ShaderModuleCreateInfo createInfo{};
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    return m_device.device().createShaderModule(createInfo);
}

void RenderAPI::copyBuffer(vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::DeviceSize size) {
    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;

    vk::CommandBuffer commandBuffer = m_device.device().allocateCommandBuffers(allocInfo)[0];

    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

    commandBuffer.begin(beginInfo);

    vk::BufferCopy copyRegion{};
    copyRegion.size = size;
    commandBuffer.copyBuffer(srcBuffer, dstBuffer, 1, &copyRegion);

    commandBuffer.end();

    vk::SubmitInfo submitInfo{};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    m_device.graphicsQueue().submit(submitInfo, nullptr);
    m_device.graphicsQueue().waitIdle();

    m_device.device().freeCommandBuffers(m_commandPool, 1, &commandBuffer);
}

void RenderAPI::transitionImageLayout(vk::Image image, vk::Format format, vk::ImageLayout oldLayout, vk::ImageLayout newLayout) {
    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;

    vk::CommandBuffer commandBuffer = m_device.device().allocateCommandBuffers(allocInfo)[0];

    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    commandBuffer.begin(beginInfo);

    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vk::PipelineStageFlags sourceStage;
    vk::PipelineStageFlags destinationStage;

    if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal) {
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

        sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
        destinationStage = vk::PipelineStageFlagBits::eTransfer;
    } else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        sourceStage = vk::PipelineStageFlagBits::eTransfer;
        destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
    } else {
        throw std::invalid_argument("unsupported layout transition!");
    }

    commandBuffer.pipelineBarrier(
        sourceStage, destinationStage,
        {},
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    commandBuffer.end();

    vk::SubmitInfo submitInfo{};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    m_device.graphicsQueue().submit(submitInfo, nullptr);
    m_device.graphicsQueue().waitIdle();

    m_device.device().freeCommandBuffers(m_commandPool, 1, &commandBuffer);
}

void RenderAPI::copyBufferToImage(vk::Buffer buffer, vk::Image image, uint32_t width, uint32_t height, uint32_t xOffset, uint32_t yOffset) {
    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;

    vk::CommandBuffer commandBuffer = m_device.device().allocateCommandBuffers(allocInfo)[0];

    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    commandBuffer.begin(beginInfo);

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

    commandBuffer.end();

    vk::SubmitInfo submitInfo{};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    m_device.graphicsQueue().submit(submitInfo, nullptr);
    m_device.graphicsQueue().waitIdle();

    m_device.device().freeCommandBuffers(m_commandPool, 1, &commandBuffer);
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
