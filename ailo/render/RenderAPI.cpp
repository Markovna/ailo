#define VMA_IMPLEMENTATION
#include "RenderAPI.h"
#include "vulkan/VulkanUtils.h"
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <set>
#include <algorithm>
#include <cstring>

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

namespace ailo {

// Static helper to load debug extension functions
static VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                              const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    }
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

static void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger,
                                           const VkAllocationCallbacks* pAllocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
}

static std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open file: " + filename);
    }

    size_t fileSize = (size_t) file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

// Initialization and shutdown

void RenderAPI::init(GLFWwindow* window, uint32_t width, uint32_t height) {
    m_window = window;
    m_windowWidth = width;
    m_windowHeight = height;

    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapchain();
    createImageViews();
    createDepthResources();
    createCommandPool();
    createCommandBuffers();
    createSyncObjects();
    createDescriptorPool();
    createAllocator();
    createRenderPass();
    createSwapchainFramebuffers();

    flushCommandBuffer();
}

void RenderAPI::shutdown() {
    m_device.waitIdle();

    cleanupSwpachainFramebuffer();
    cleanupSwapchain();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_device.destroySemaphore(m_renderFinishedSemaphores[i]);
        m_device.destroySemaphore(m_imageAvailableSemaphores[i]);
        m_device.destroyFence(m_inFlightFences[i]);
        destroyStageBuffers(i);
    }

    cleanupDescriptorSets();

    vmaDestroyAllocator(m_Allocator);

    m_device.destroyRenderPass(m_renderPass);
    m_device.destroyDescriptorPool(m_descriptorPool);
    m_device.destroyCommandPool(m_commandPool);
    m_device.destroy();

    if (enableValidationLayers) {
        DestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
    }

    m_instance.destroySurfaceKHR(m_surface);
    m_instance.destroy();

    assert(pipelines.size() == 0);
    assert(buffers.size() == 0);
    assert(descriptorSetLayouts.size() == 0);
    assert(descriptorSets.size() == 0);
    assert(textures.size() == 0);
}

// Frame lifecycle

bool RenderAPI::beginFrame() {
    auto result = m_device.acquireNextImageKHR(m_swapchain, UINT64_MAX,
                                                m_imageAvailableSemaphores[m_currentFrame], nullptr);

    if (result.result == vk::Result::eErrorOutOfDateKHR) {
        recreateSwapchain();
        return false;
    } else if (result.result != vk::Result::eSuccess && result.result != vk::Result::eSuboptimalKHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    m_swapChainImageIndex = result.value;

    return true;
}

void RenderAPI::endFrame() {
    m_currentCommandBuffer.end();

    vk::SubmitInfo submitInfo{};
    vk::Semaphore waitSemaphores[] = {m_imageAvailableSemaphores[m_currentFrame]};
    vk::PipelineStageFlags waitStages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_currentCommandBuffer;

    vk::Semaphore signalSemaphores[] = {m_renderFinishedSemaphores[m_currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    m_graphicsQueue.submit(submitInfo, m_inFlightFences[m_currentCommandBufferIndex]);

    vk::PresentInfoKHR presentInfo{};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    vk::SwapchainKHR swapchains[] = {m_swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &m_swapChainImageIndex;

    auto result = m_presentQueue.presentKHR(presentInfo);

    if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR || m_framebufferResized) {
        m_framebufferResized = false;
        recreateSwapchain();
    } else if (result != vk::Result::eSuccess) {
        throw std::runtime_error("failed to present swap chain image!");
    }

    cleanupDescriptorSets();

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    flushCommandBuffer();
    m_swapChainImageIndex = -1;
}

void RenderAPI::cleanupDescriptorSets() {
  for(auto descriptorSetHandle : m_descriptorSetsToDestroy) {
    DescriptorSet& descriptorSet = descriptorSets.get(descriptorSetHandle);
    m_device.freeDescriptorSets(m_descriptorPool, 1, &descriptorSet.descriptorSet);
    descriptorSets.free(descriptorSetHandle);
  }
  m_descriptorSetsToDestroy.clear();
}

void RenderAPI::waitIdle() {
    m_device.waitIdle();
}

// Buffer management

BufferHandle RenderAPI::createVertexBuffer(const void* data, uint64_t size) {
    BufferHandle handle = buffers.allocate();
    Buffer& vertexBuffer = buffers.get(handle);
    allocateBuffer(vertexBuffer, vk::BufferUsageFlagBits::eVertexBuffer, size);
    vertexBuffer.binding = BufferBinding::VERTEX;
    if(data != nullptr) {
        loadFromCpu(m_currentCommandBuffer, vertexBuffer, data, 0, size);
    }
    return handle;
}

BufferHandle RenderAPI::createIndexBuffer(const void* data, uint64_t size) {
    // Create staging buffer
    BufferHandle handle = buffers.allocate();
    Buffer& indexBuffer = buffers.get(handle);
    allocateBuffer(indexBuffer, vk::BufferUsageFlagBits::eIndexBuffer, size);
    indexBuffer.binding = BufferBinding::INDEX;
    if(data != nullptr) {
        loadFromCpu(m_currentCommandBuffer, indexBuffer, data, 0, size);
    }
    return handle;
}

VmaAllocator createAllocator(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device) {
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
  BufferHandle handle = buffers.allocate();
  Buffer& buffer = buffers.get(handle);
  auto usageFlags = vkutils::getBufferUsage(bufferBinding);
  allocateBuffer(buffer, usageFlags, size);
  buffer.binding = bufferBinding;
  return handle;
}

void RenderAPI::destroyBuffer(const BufferHandle& handle) {
  if(!handle) { return; }

  Buffer& buffer = buffers.get(handle);
  vmaDestroyBuffer(m_Allocator, buffer.buffer, buffer.vmaAllocation);
  buffers.free(handle);
}

void RenderAPI::updateBuffer(const BufferHandle& handle, const void* data, uint64_t size, uint64_t byteOffset) {
    Buffer& buffer = buffers.get(handle);
    loadFromCpu(m_currentCommandBuffer, buffer, data, byteOffset, size);
}

// Texture management

TextureHandle RenderAPI::createTexture(vk::Format format, uint32_t width, uint32_t height, vk::Filter filter) {
    TextureHandle handle = textures.allocate();
    Texture& texture = textures.get(handle);

    texture.format = format;
    texture.width = width;
    texture.height = height;

    // Create image
    createImage(width, height, format, vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal,
                texture.image, texture.memory);

    // Create image view
    texture.imageView = createImageView(texture.image, format, vk::ImageAspectFlagBits::eColor);

    // Create sampler
    vk::SamplerCreateInfo samplerInfo{};
    samplerInfo.magFilter = filter;
    samplerInfo.minFilter = filter;
    samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
    samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
    samplerInfo.anisotropyEnable = VK_TRUE;

    vk::PhysicalDeviceProperties properties = m_physicalDevice.getProperties();
    samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;

    samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = vk::CompareOp::eAlways;
    samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    texture.sampler = m_device.createSampler(samplerInfo);

    return handle;
}

void RenderAPI::destroyTexture(const TextureHandle& handle) {
    if(!handle) { return; }

    Texture& texture = textures.get(handle);
    m_device.destroySampler(texture.sampler);
    m_device.destroyImageView(texture.imageView);
    m_device.destroyImage(texture.image);
    m_device.freeMemory(texture.memory);
    textures.free(handle);
}

void RenderAPI::updateTextureImage(const TextureHandle& handle, const void* data, size_t dataSize, uint32_t width, uint32_t height, uint32_t xOffset, uint32_t yOffset) {
    Texture& texture = textures.get(handle);

    // Create staging buffer
    // allocate stage buffer
    StageBuffer stageBuffer = allocateStageBuffer(dataSize);

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
    DescriptorSetLayoutHandle handle = descriptorSetLayouts.allocate();
    DescriptorSetLayout& descriptorSetLayout = descriptorSetLayouts.get(handle);

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

    descriptorSetLayout.layout = m_device.createDescriptorSetLayout(layoutInfo);
    for(auto& binding : bindings) {
      if(binding.descriptorType == vk::DescriptorType::eUniformBufferDynamic) {
        descriptorSetLayout.dynamicBindings.set(binding.binding, true);
      }
    }

    return handle;
}

void RenderAPI::destroyDescriptorSetLayout(DescriptorSetLayoutHandle& handle) {
  if(!handle) { return; }

  DescriptorSetLayout& descriptorSetLayout = descriptorSetLayouts.get(handle);
  m_device.destroyDescriptorSetLayout(descriptorSetLayout.layout);
  descriptorSetLayouts.free(handle);
}

DescriptorSetHandle RenderAPI::createDescriptorSet(DescriptorSetLayoutHandle layoutHandle) {
    DescriptorSetLayout& descriptorSetLayout = descriptorSetLayouts.get(layoutHandle);
    auto& layout = descriptorSetLayout.layout;

    DescriptorSetHandle handle = descriptorSets.allocate();
    DescriptorSet& descriptorSet = descriptorSets.get(handle);

    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    auto result = m_device.allocateDescriptorSets(allocInfo);
    descriptorSet.descriptorSet = result[0];
    descriptorSet.dynamicBindings = descriptorSetLayout.dynamicBindings;
    descriptorSet.layoutHandle = layoutHandle;

    return handle;
}

void RenderAPI::destroyDescriptorSet(const DescriptorSetHandle& handle) {
    if(!handle) { return; }

    m_descriptorSetsToDestroy.push_back(handle);
}

void RenderAPI::updateDescriptorSetBuffer(const DescriptorSetHandle& descriptorSetHandle, const BufferHandle& bufferHandle, uint32_t binding) {
    DescriptorSet& descriptorSet = descriptorSets.get(descriptorSetHandle);
    Buffer& buffer = buffers.get(bufferHandle);

    vk::DescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = buffer.buffer;
    bufferInfo.offset = 0;
    bufferInfo.range = buffer.size;

    bool isDynamic = descriptorSet.dynamicBindings[binding];

    vk::WriteDescriptorSet descriptorWrite{};
    descriptorWrite.dstSet = descriptorSet.descriptorSet;
    descriptorWrite.dstBinding = binding;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = isDynamic ? vk::DescriptorType::eUniformBufferDynamic : vk::DescriptorType::eUniformBuffer;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    m_device.updateDescriptorSets(1, &descriptorWrite, 0, nullptr);
}

void RenderAPI::updateDescriptorSetTexture(const DescriptorSetHandle& descriptorSetHandle, const TextureHandle& textureHandle, uint32_t binding) {
    DescriptorSet& descriptorSet = descriptorSets.get(descriptorSetHandle);
    Texture& texture = textures.get(textureHandle);

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

    m_device.updateDescriptorSets(1, &descriptorWrite, 0, nullptr);
}

void RenderAPI::bindDescriptorSet(const DescriptorSetHandle& descriptorSetHandle, uint32_t setIndex, std::initializer_list<uint32_t> dynamicOffsets) {
  Pipeline& currentPipeline = pipelines.get(m_currentPipeline);
  assert(descriptorSetHandle);
  DescriptorSet& descriptorSet = descriptorSets.get(descriptorSetHandle);
  DescriptorSetLayout& layout = descriptorSetLayouts.get(descriptorSet.layoutHandle);

  std::array<uint32_t, 32> dynamicOffsetsArray { };
  assert(dynamicOffsets.size() <= dynamicOffsetsArray.size());
  std::copy(dynamicOffsets.begin(), dynamicOffsets.end(), dynamicOffsetsArray.begin());

  m_currentCommandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        currentPipeline.layout,
        setIndex,
        1,
        &descriptorSet.descriptorSet,
        static_cast<uint32_t>(dynamicOffsets.size()),
        dynamicOffsetsArray.data()
    );
}

void RenderAPI::createRenderPass() {
    // Create render pass
    vk::AttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapchainImageFormat;
    colorAttachment.samples = vk::SampleCountFlagBits::e1;
    colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
    colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
    colorAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

    vk::AttachmentDescription depthAttachment{};
    depthAttachment.format = findDepthFormat();
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

    m_renderPass = m_device.createRenderPass(renderPassInfo);
}

// Pipeline management

PipelineHandle RenderAPI::createGraphicsPipeline(
    const std::string& vertShaderPath,
    const std::string& fragShaderPath,
    const PipelineDescription& description,
    const VertexInputDescription& vertexInput) {

    PipelineHandle handle = pipelines.allocate();

    Pipeline& pipeline = pipelines.get(handle);

    // Load shaders
    auto vertShaderCode = readFile(vertShaderPath);
    auto fragShaderCode = readFile(fragShaderPath);

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

    auto& raster = description.raster;

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
    std::vector<vk::DynamicState> dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };
    vk::PipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    std::vector<vk::DescriptorSetLayout> setLayouts;
    auto& layout = description.layout;
    for(auto& set : layout.sets) {
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

      auto descriptorSetLayout = m_device.createDescriptorSetLayout(layoutInfo);
      setLayouts.push_back(descriptorSetLayout);
    }

    pipeline.descriptorSetLayouts = setLayouts;

    // Pipeline layout
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.setLayoutCount = setLayouts.size();
    pipelineLayoutInfo.pSetLayouts = setLayouts.data();

    pipeline.layout = m_device.createPipelineLayout(pipelineLayoutInfo);

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

    auto result = m_device.createGraphicsPipeline(nullptr, pipelineInfo);
    if (result.result != vk::Result::eSuccess) {
        throw std::runtime_error("failed to create graphics pipeline!");
    }
    pipeline.pipeline = result.value;

    // Cleanup shader modules
    m_device.destroyShaderModule(vertShaderModule);
    m_device.destroyShaderModule(fragShaderModule);

    return handle;
}

void RenderAPI::destroyPipeline(const PipelineHandle& handle) {
    if(!handle) { return; }

    Pipeline& pipeline = pipelines.get(handle);

    m_device.destroyPipeline(pipeline.pipeline);
    for(auto descriptorSetLayout : pipeline.descriptorSetLayouts) {
      m_device.destroyDescriptorSetLayout(descriptorSetLayout);
    }
    m_device.destroyPipelineLayout(pipeline.layout);

    pipelines.free(handle);
}

// Command recording

void RenderAPI::beginRenderPass(vk::ClearColorValue clearColor) {
    vk::RenderPassBeginInfo renderPassInfo{};
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.framebuffer = m_swapchainFramebuffers[m_swapChainImageIndex];
    renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
    renderPassInfo.renderArea.extent = m_swapchainExtent;

    std::array<vk::ClearValue, 2> clearValues{};
    clearValues[0].color = clearColor;
    clearValues[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    m_currentCommandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

    // Set default viewport and scissor
    vk::Viewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)m_swapchainExtent.width;
    viewport.height = (float)m_swapchainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    m_currentCommandBuffer.setViewport(0, 1, &viewport);

    vk::Rect2D scissor{};
    scissor.offset = vk::Offset2D{0, 0};
    scissor.extent = m_swapchainExtent;
    m_currentCommandBuffer.setScissor(0, 1, &scissor);
}

void RenderAPI::endRenderPass() {
    m_currentCommandBuffer.endRenderPass();
}

void RenderAPI::bindPipeline(const PipelineHandle& handle) {
    auto& currentPipeline = pipelines.get(handle);
    m_currentCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, currentPipeline.pipeline);
    m_currentPipeline = handle;
}

void RenderAPI::bindVertexBuffer(const BufferHandle& handle) {
    Buffer& buffer = buffers.get(handle);
    vk::Buffer vertexBuffers[] = {buffer.buffer};
    vk::DeviceSize offsets[] = {0};
    m_currentCommandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);
}

void RenderAPI::bindIndexBuffer(const BufferHandle& handle, vk::IndexType indexType) {
    Buffer& buffer = buffers.get(handle);
    m_currentCommandBuffer.bindIndexBuffer(buffer.buffer, 0, indexType);
}

void RenderAPI::drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset) {
    m_currentCommandBuffer.drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, 0);
}

void RenderAPI::setViewport(float x, float y, float width, float height) {
    vk::Viewport viewport{x, y, width, height, 0.0f, 1.0f};
    m_currentCommandBuffer.setViewport(0, 1, &viewport);
}

void RenderAPI::setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) {
    vk::Rect2D scissor{{x, y}, {width, height}};
    m_currentCommandBuffer.setScissor(0, 1, &scissor);
}

// Swapchain management

void RenderAPI::handleWindowResize() {
    m_framebufferResized = true;
}

// Internal initialization

void RenderAPI::createInstance() {
    if (enableValidationLayers && !checkValidationLayerSupport()) {
        throw std::runtime_error("validation layers requested, but not available!");
    }

    glfwInit();

    vk::ApplicationInfo appInfo{};
    appInfo.pApplicationName = "Vulkan Application";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    vk::InstanceCreateInfo createInfo{};
    createInfo.pApplicationInfo = &appInfo;
    createInfo.flags = vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;

    auto extensions = getRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        createInfo.ppEnabledLayerNames = m_validationLayers.data();

        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = debugCallback;
        createInfo.pNext = &debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }

    m_instance = vk::createInstance(createInfo);
}

void RenderAPI::setupDebugMessenger() {
    if (!enableValidationLayers) return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;

    if (CreateDebugUtilsMessengerEXT(m_instance, &createInfo, nullptr,
        reinterpret_cast<VkDebugUtilsMessengerEXT*>(&m_debugMessenger)) != VK_SUCCESS) {
        throw std::runtime_error("failed to set up debug messenger!");
    }
}

void RenderAPI::createSurface() {
    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(m_instance, m_window, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("failed to create window surface!");
    }
    m_surface = surface;
}

void RenderAPI::pickPhysicalDevice() {
    auto devices = m_instance.enumeratePhysicalDevices();
    if (devices.empty()) {
        throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }

    for (const auto& device : devices) {
        if (isDeviceSuitable(device)) {
            m_physicalDevice = device;
            break;
        }
    }

    if (!m_physicalDevice) {
        throw std::runtime_error("failed to find a suitable GPU!");
    }
}

void RenderAPI::createLogicalDevice() {
    QueueFamilyIndices indices = findQueueFamilies(m_physicalDevice);

    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily.value(), indices.presentFamily.value()};

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        vk::DeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    vk::PhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = true;

    // Check for portability subset extension
    std::vector<const char*> enabledExtensions(m_deviceExtensions.begin(), m_deviceExtensions.end());
    auto availableExtensions = m_physicalDevice.enumerateDeviceExtensionProperties();

    bool hasPortabilitySubset = false;
    for (const auto& extension : availableExtensions) {
        if (strcmp(extension.extensionName, "VK_KHR_portability_subset") == 0) {
            hasPortabilitySubset = true;
            break;
        }
    }

    if (hasPortabilitySubset) {
        enabledExtensions.push_back("VK_KHR_portability_subset");
    }

    vk::DeviceCreateInfo createInfo{};
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
    createInfo.ppEnabledExtensionNames = enabledExtensions.data();

    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        createInfo.ppEnabledLayerNames = m_validationLayers.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }

    m_device = m_physicalDevice.createDevice(createInfo);

    m_graphicsQueue = m_device.getQueue(indices.graphicsFamily.value(), 0);
    m_presentQueue = m_device.getQueue(indices.presentFamily.value(), 0);
}

void RenderAPI::createSwapchain() {
    SwapchainSupportDetails swapchainSupport = querySwapchainSupport(m_physicalDevice);

    vk::SurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapchainSupport.formats);
    vk::PresentModeKHR presentMode = chooseSwapPresentMode(swapchainSupport.presentModes);
    vk::Extent2D extent = chooseSwapExtent(swapchainSupport.capabilities);

    uint32_t imageCount = swapchainSupport.capabilities.minImageCount + 1;
    if (swapchainSupport.capabilities.maxImageCount > 0 && imageCount > swapchainSupport.capabilities.maxImageCount) {
        imageCount = swapchainSupport.capabilities.maxImageCount;
    }

    vk::SwapchainCreateInfoKHR createInfo{};
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;

    QueueFamilyIndices indices = findQueueFamilies(m_physicalDevice);
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = vk::SharingMode::eConcurrent;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = vk::SharingMode::eExclusive;
    }

    createInfo.preTransform = swapchainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = nullptr;

    m_swapchain = m_device.createSwapchainKHR(createInfo);
    m_swapchainImages = m_device.getSwapchainImagesKHR(m_swapchain);

    m_swapchainImageFormat = surfaceFormat.format;
    m_swapchainExtent = extent;
}

void RenderAPI::createImageViews() {
    m_swapchainImageViews.resize(m_swapchainImages.size());

    for (size_t i = 0; i < m_swapchainImages.size(); i++) {
        vk::ImageViewCreateInfo createInfo{};
        createInfo.image = m_swapchainImages[i];
        createInfo.viewType = vk::ImageViewType::e2D;
        createInfo.format = m_swapchainImageFormat;
        createInfo.components.r = vk::ComponentSwizzle::eIdentity;
        createInfo.components.g = vk::ComponentSwizzle::eIdentity;
        createInfo.components.b = vk::ComponentSwizzle::eIdentity;
        createInfo.components.a = vk::ComponentSwizzle::eIdentity;
        createInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        m_swapchainImageViews[i] = m_device.createImageView(createInfo);
    }
}

void RenderAPI::createDepthResources() {
    vk::Format depthFormat = findDepthFormat();

    createImage(m_swapchainExtent.width, m_swapchainExtent.height, depthFormat,
                vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment,
                vk::MemoryPropertyFlagBits::eDeviceLocal, m_depthImage, m_depthImageMemory);

    m_depthImageView = createImageView(m_depthImage, depthFormat, vk::ImageAspectFlagBits::eDepth);
}

void RenderAPI::createCommandPool() {
    QueueFamilyIndices queueFamilyIndices = findQueueFamilies(m_physicalDevice);

    vk::CommandPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

    m_commandPool = m_device.createCommandPool(poolInfo);
}

void RenderAPI::createCommandBuffers() {
    m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    m_stageBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());

    m_commandBuffers = m_device.allocateCommandBuffers(allocInfo);
}

void RenderAPI::createSyncObjects() {
    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    vk::SemaphoreCreateInfo semaphoreInfo{};
    vk::FenceCreateInfo fenceInfo{};
    fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_imageAvailableSemaphores[i] = m_device.createSemaphore(semaphoreInfo);
        m_renderFinishedSemaphores[i] = m_device.createSemaphore(semaphoreInfo);
        m_inFlightFences[i] = m_device.createFence(fenceInfo);
    }
}

void RenderAPI::createDescriptorPool() {
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

    m_descriptorPool = m_device.createDescriptorPool(poolInfo);
}

void RenderAPI::cleanupSwapchain() {
    m_device.destroyImageView(m_depthImageView);
    m_device.destroyImage(m_depthImage);
    m_device.freeMemory(m_depthImageMemory);

    for (auto imageView : m_swapchainImageViews) {
        m_device.destroyImageView(imageView);
    }

    m_device.destroySwapchainKHR(m_swapchain);
}

void RenderAPI::cleanupSwpachainFramebuffer() {
    for(auto swapchainFramebuffer : m_swapchainFramebuffers) {
        m_device.destroyFramebuffer(swapchainFramebuffer);
    }
    m_swapchainFramebuffers.clear();
}

void RenderAPI::createSwapchainFramebuffers() {
    m_swapchainFramebuffers.resize(m_swapchainImageViews.size());
    for (size_t i = 0; i < m_swapchainImageViews.size(); i++) {
        std::array<vk::ImageView, 2> attachments = {
            m_swapchainImageViews[i],
            m_depthImageView
        };

        vk::FramebufferCreateInfo framebufferInfo{};
        framebufferInfo.renderPass = m_renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = m_swapchainExtent.width;
        framebufferInfo.height = m_swapchainExtent.height;
        framebufferInfo.layers = 1;

        m_swapchainFramebuffers[i] = m_device.createFramebuffer(framebufferInfo);
    }
}

void RenderAPI::recreateSwapchain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(m_window, &width, &height);
        glfwWaitEvents();
    }

    m_device.waitIdle();

    cleanupSwpachainFramebuffer();
    cleanupSwapchain();

    createSwapchain();
    createImageViews();
    createDepthResources();
    createSwapchainFramebuffers();
}

// Helper functions

bool RenderAPI::checkValidationLayerSupport() {
    auto availableLayers = vk::enumerateInstanceLayerProperties();

    for (const char* layerName : m_validationLayers) {
        bool layerFound = false;

        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) {
            return false;
        }
    }

    return true;
}

std::vector<const char*> RenderAPI::getRequiredExtensions() {
    uint32_t extensionCount = 0;
    const char** requiredExtensions = glfwGetRequiredInstanceExtensions(&extensionCount);

    std::vector<const char*> extensions(requiredExtensions, requiredExtensions + extensionCount);

    if (enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

    return extensions;
}

bool RenderAPI::isDeviceSuitable(vk::PhysicalDevice device) {
    QueueFamilyIndices indices = findQueueFamilies(device);

    bool extensionsSupported = checkDeviceExtensionSupport(device);

    bool swapchainAdequate = false;
    if (extensionsSupported) {
        SwapchainSupportDetails swapchainSupport = querySwapchainSupport(device);
        swapchainAdequate = !swapchainSupport.formats.empty() && !swapchainSupport.presentModes.empty();
    }

    vk::PhysicalDeviceFeatures supportedFeatures;
    device.getFeatures(&supportedFeatures);

    return indices.isComplete() && extensionsSupported && swapchainAdequate && supportedFeatures.samplerAnisotropy;
}

bool RenderAPI::checkDeviceExtensionSupport(vk::PhysicalDevice device) {
    auto availableExtensions = device.enumerateDeviceExtensionProperties();

    std::set<std::string> requiredExtensions(m_deviceExtensions.begin(), m_deviceExtensions.end());

    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

RenderAPI::QueueFamilyIndices RenderAPI::findQueueFamilies(vk::PhysicalDevice device) {
    QueueFamilyIndices indices;

    auto queueFamilies = device.getQueueFamilyProperties();

    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics) {
            indices.graphicsFamily = i;
        }

        if (device.getSurfaceSupportKHR(i, m_surface)) {
            indices.presentFamily = i;
        }

        if (indices.isComplete()) {
            break;
        }

        i++;
    }

    return indices;
}

RenderAPI::SwapchainSupportDetails RenderAPI::querySwapchainSupport(vk::PhysicalDevice device) {
    SwapchainSupportDetails details;

    details.capabilities = device.getSurfaceCapabilitiesKHR(m_surface);
    details.formats = device.getSurfaceFormatsKHR(m_surface);
    details.presentModes = device.getSurfacePresentModesKHR(m_surface);

    return details;
}

vk::SurfaceFormatKHR RenderAPI::chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats) {
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == vk::Format::eB8G8R8A8Srgb &&
            availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            return availableFormat;
        }
    }
    return availableFormats[0];
}

vk::PresentModeKHR RenderAPI::chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes) {
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == vk::PresentModeKHR::eMailbox) {
            return availablePresentMode;
        }
    }
    return vk::PresentModeKHR::eFifo;
}

vk::Extent2D RenderAPI::chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    } else {
        int width, height;
        glfwGetFramebufferSize(m_window, &width, &height);

        vk::Extent2D actualExtent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height)
        };

        actualExtent.width = std::clamp(actualExtent.width,
                                        capabilities.minImageExtent.width,
                                        capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height,
                                         capabilities.minImageExtent.height,
                                         capabilities.maxImageExtent.height);

        return actualExtent;
    }
}

vk::ShaderModule RenderAPI::createShaderModule(const std::vector<char>& code) {
    vk::ShaderModuleCreateInfo createInfo{};
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    return m_device.createShaderModule(createInfo);
}

uint32_t RenderAPI::findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
    vk::PhysicalDeviceMemoryProperties memProperties = m_physicalDevice.getMemoryProperties();

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable memory type!");
}

void RenderAPI::copyBuffer(vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::DeviceSize size) {

  vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;

    vk::CommandBuffer commandBuffer = m_device.allocateCommandBuffers(allocInfo)[0];

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

    m_graphicsQueue.submit(submitInfo, nullptr);
    m_graphicsQueue.waitIdle();

    m_device.freeCommandBuffers(m_commandPool, 1, &commandBuffer);
}

vk::Format RenderAPI::findSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features) {
    for (vk::Format format : candidates) {
        vk::FormatProperties props = m_physicalDevice.getFormatProperties(format);

        if (tiling == vk::ImageTiling::eLinear && (props.linearTilingFeatures & features) == features) {
            return format;
        } else if (tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    throw std::runtime_error("failed to find supported format!");
}

vk::Format RenderAPI::findDepthFormat() {
    return findSupportedFormat(
        {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint},
        vk::ImageTiling::eOptimal,
        vk::FormatFeatureFlagBits::eDepthStencilAttachment
    );
}

void RenderAPI::createImage(uint32_t width, uint32_t height, vk::Format format, vk::ImageTiling tiling,
                            vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties,
                            vk::Image& image, vk::DeviceMemory& imageMemory) {
    vk::ImageCreateInfo imageInfo{};
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    imageInfo.usage = usage;
    imageInfo.samples = vk::SampleCountFlagBits::e1;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;

    image = m_device.createImage(imageInfo);

    vk::MemoryRequirements memRequirements = m_device.getImageMemoryRequirements(image);

    vk::MemoryAllocateInfo allocInfo{};
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    imageMemory = m_device.allocateMemory(allocInfo);
    m_device.bindImageMemory(image, imageMemory, 0);
}

vk::ImageView RenderAPI::createImageView(vk::Image image, vk::Format format, vk::ImageAspectFlags aspectFlags) {
    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image = image;
    viewInfo.viewType = vk::ImageViewType::e2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    return m_device.createImageView(viewInfo);
}

void RenderAPI::transitionImageLayout(vk::Image image, vk::Format format, vk::ImageLayout oldLayout, vk::ImageLayout newLayout) {
    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;

    vk::CommandBuffer commandBuffer = m_device.allocateCommandBuffers(allocInfo)[0];

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

    m_graphicsQueue.submit(submitInfo, nullptr);
    m_graphicsQueue.waitIdle();

    m_device.freeCommandBuffers(m_commandPool, 1, &commandBuffer);
}

void RenderAPI::copyBufferToImage(vk::Buffer buffer, vk::Image image, uint32_t width, uint32_t height, uint32_t xOffset, uint32_t yOffset) {
    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;

    vk::CommandBuffer commandBuffer = m_device.allocateCommandBuffers(allocInfo)[0];

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

    m_graphicsQueue.submit(submitInfo, nullptr);
    m_graphicsQueue.waitIdle();

    m_device.freeCommandBuffers(m_commandPool, 1, &commandBuffer);
}

VKAPI_ATTR VkBool32 VKAPI_CALL RenderAPI::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {

    std::ostream* out = nullptr;
    auto errorMask = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    if((pCallbackData->flags & errorMask) > 0) {
        out = &std::cerr;
    } else {
        out = &std::cout;
    }

    *out << "[Vulkan] " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}

void RenderAPI::createAllocator() {
  m_Allocator = ailo::createAllocator(m_instance, m_physicalDevice, m_device);
}

StageBuffer RenderAPI::allocateStageBuffer(uint32_t capacity) {
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
    m_stageBuffers[m_currentCommandBufferIndex].push_back(stageBuffer);
    return stageBuffer;
}

void RenderAPI::destroyStageBuffers(uint32_t index) {
  for(auto& stage : m_stageBuffers[index]) {
    vmaUnmapMemory(m_Allocator, stage.vmaAllocation);
    vmaDestroyBuffer(m_Allocator, stage.buffer, stage.vmaAllocation);
  }
  m_stageBuffers[index].clear();
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
  StageBuffer stageBuffer = allocateStageBuffer(numBytes);

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

void RenderAPI::flushCommandBuffer() {
  m_currentCommandBufferIndex = (m_currentCommandBufferIndex + 1) % m_commandBuffers.size();

  auto& fence = m_inFlightFences[m_currentCommandBufferIndex];

  (void)m_device.waitForFences(1, &fence, VK_TRUE, UINT64_MAX);

  // free resources acquired by command buffer
  destroyStageBuffers(m_currentCommandBufferIndex);

  (void)m_device.resetFences(1, &fence);

  m_currentCommandBuffer = m_commandBuffers[m_currentCommandBufferIndex];
  m_currentCommandBuffer.reset();

  vk::CommandBufferBeginInfo beginInfo{};
  m_currentCommandBuffer.begin(beginInfo);
}

} // namespace ailo
