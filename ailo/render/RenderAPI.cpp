#include "RenderAPI.h"
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
    createCommandPool();
    createCommandBuffers();
    createSyncObjects();
}

void RenderAPI::shutdown() {
    m_device.waitIdle();

    cleanupSwapchain();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_device.destroySemaphore(m_renderFinishedSemaphores[i]);
        m_device.destroySemaphore(m_imageAvailableSemaphores[i]);
        m_device.destroyFence(m_inFlightFences[i]);
    }

    m_device.destroyCommandPool(m_commandPool);
    m_device.destroy();

    if (enableValidationLayers) {
        DestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
    }

    m_instance.destroySurfaceKHR(m_surface);
    m_instance.destroy();
}

// Frame lifecycle

bool RenderAPI::beginFrame(uint32_t& outImageIndex) {
    (void)m_device.waitForFences(1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    auto result = m_device.acquireNextImageKHR(m_swapchain, UINT64_MAX,
                                                m_imageAvailableSemaphores[m_currentFrame],
                                                nullptr);

    if (result.result == vk::Result::eErrorOutOfDateKHR) {
        recreateSwapchain();
        return false;
    } else if (result.result != vk::Result::eSuccess && result.result != vk::Result::eSuboptimalKHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    outImageIndex = result.value;
    (void)m_device.resetFences(1, &m_inFlightFences[m_currentFrame]);

    m_currentCommandBuffer = m_commandBuffers[m_currentFrame];
    m_currentCommandBuffer.reset();

    vk::CommandBufferBeginInfo beginInfo{};
    m_currentCommandBuffer.begin(beginInfo);

    return true;
}

void RenderAPI::endFrame(uint32_t imageIndex) {
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

    m_graphicsQueue.submit(submitInfo, m_inFlightFences[m_currentFrame]);

    vk::PresentInfoKHR presentInfo{};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    vk::SwapchainKHR swapchains[] = {m_swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    auto result = m_presentQueue.presentKHR(presentInfo);

    if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR || m_framebufferResized) {
        m_framebufferResized = false;
        recreateSwapchain();
    } else if (result != vk::Result::eSuccess) {
        throw std::runtime_error("failed to present swap chain image!");
    }

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void RenderAPI::waitIdle() {
    m_device.waitIdle();
}

// Buffer management

BufferHandle RenderAPI::createVertexBuffer(const void* data, vk::DeviceSize size) {
    // Create staging buffer
    auto stagingBuffer = createBuffer(size,
                                      vk::BufferUsageFlagBits::eTransferSrc,
                                      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    // Copy data to staging buffer
    void* mappedData = m_device.mapMemory(stagingBuffer.memory, 0, size);
    memcpy(mappedData, data, (size_t)size);
    m_device.unmapMemory(stagingBuffer.memory);

    // Create device local buffer
    auto vertexBuffer = createBuffer(size,
                                     vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
                                     vk::MemoryPropertyFlagBits::eDeviceLocal);

    // Copy from staging to device local
    copyBuffer(stagingBuffer.buffer, vertexBuffer.buffer, size);

    // Cleanup staging buffer
    destroyBuffer(stagingBuffer);

    return vertexBuffer;
}

BufferHandle RenderAPI::createIndexBuffer(const void* data, vk::DeviceSize size) {
    // Create staging buffer
    auto stagingBuffer = createBuffer(size,
                                      vk::BufferUsageFlagBits::eTransferSrc,
                                      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    // Copy data to staging buffer
    void* mappedData = m_device.mapMemory(stagingBuffer.memory, 0, size);
    memcpy(mappedData, data, (size_t)size);
    m_device.unmapMemory(stagingBuffer.memory);

    // Create device local buffer
    auto indexBuffer = createBuffer(size,
                                    vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
                                    vk::MemoryPropertyFlagBits::eDeviceLocal);

    // Copy from staging to device local
    copyBuffer(stagingBuffer.buffer, indexBuffer.buffer, size);

    // Cleanup staging buffer
    destroyBuffer(stagingBuffer);

    return indexBuffer;
}

BufferHandle RenderAPI::createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties) {
    BufferHandle handle;
    handle.size = size;

    vk::BufferCreateInfo bufferInfo{};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;

    handle.buffer = m_device.createBuffer(bufferInfo);

    vk::MemoryRequirements memRequirements = m_device.getBufferMemoryRequirements(handle.buffer);

    vk::MemoryAllocateInfo allocInfo{};
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    handle.memory = m_device.allocateMemory(allocInfo);
    m_device.bindBufferMemory(handle.buffer, handle.memory, 0);

    return handle;
}

void RenderAPI::destroyBuffer(const BufferHandle& handle) {
    m_device.destroyBuffer(handle.buffer);
    m_device.freeMemory(handle.memory);
}

void RenderAPI::updateBuffer(const BufferHandle& handle, const void* data, vk::DeviceSize size) {
    void* mappedData = m_device.mapMemory(handle.memory, 0, size);
    memcpy(mappedData, data, (size_t)size);
    m_device.unmapMemory(handle.memory);
}

// Pipeline management

PipelineHandle RenderAPI::createGraphicsPipeline(
    const std::string& vertShaderPath,
    const std::string& fragShaderPath,
    const VertexInputDescription& vertexInput,
    vk::PrimitiveTopology topology) {

    PipelineHandle handle;

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

    vk::AttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::SubpassDescription subpass{};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    vk::SubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.srcAccessMask = {};
    dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

    vk::RenderPassCreateInfo renderPassInfo{};
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    handle.renderPass = m_device.createRenderPass(renderPassInfo);

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
    inputAssembly.topology = topology;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport state
    vk::PipelineViewportStateCreateInfo viewportState{};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterization
    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = vk::PolygonMode::eFill;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = vk::CullModeFlagBits::eBack;
    rasterizer.frontFace = vk::FrontFace::eClockwise;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

    // Color blending
    vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    colorBlendAttachment.blendEnable = VK_FALSE;

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

    // Pipeline layout
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
    handle.layout = m_device.createPipelineLayout(pipelineLayoutInfo);

    // Create graphics pipeline
    vk::GraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = handle.layout;
    pipelineInfo.renderPass = handle.renderPass;
    pipelineInfo.subpass = 0;

    auto result = m_device.createGraphicsPipeline(nullptr, pipelineInfo);
    if (result.result != vk::Result::eSuccess) {
        throw std::runtime_error("failed to create graphics pipeline!");
    }
    handle.pipeline = result.value;

    // Cleanup shader modules
    m_device.destroyShaderModule(vertShaderModule);
    m_device.destroyShaderModule(fragShaderModule);

    // Create framebuffers for this render pass
    m_swapchainFramebuffers.resize(m_swapchainImageViews.size());
    for (size_t i = 0; i < m_swapchainImageViews.size(); i++) {
        vk::ImageView attachments[] = {m_swapchainImageViews[i]};

        vk::FramebufferCreateInfo framebufferInfo{};
        framebufferInfo.renderPass = handle.renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = m_swapchainExtent.width;
        framebufferInfo.height = m_swapchainExtent.height;
        framebufferInfo.layers = 1;

        m_swapchainFramebuffers[i] = m_device.createFramebuffer(framebufferInfo);
    }

    return handle;
}

void RenderAPI::destroyPipeline(const PipelineHandle& handle) {
    // Clean up framebuffers first
    for (auto framebuffer : m_swapchainFramebuffers) {
        m_device.destroyFramebuffer(framebuffer);
    }
    m_swapchainFramebuffers.clear();

    m_device.destroyPipeline(handle.pipeline);
    m_device.destroyPipelineLayout(handle.layout);
    m_device.destroyRenderPass(handle.renderPass);
}

// Command recording

void RenderAPI::beginRenderPass(uint32_t imageIndex, vk::ClearColorValue clearColor) {
    vk::RenderPassBeginInfo renderPassInfo{};
    renderPassInfo.renderPass = m_currentPipeline.renderPass;
    renderPassInfo.framebuffer = m_swapchainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
    renderPassInfo.renderArea.extent = m_swapchainExtent;

    vk::ClearValue clearValue;
    clearValue.color = clearColor;
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

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
    m_currentPipeline = handle;
    m_currentCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, handle.pipeline);
}

void RenderAPI::bindVertexBuffer(const BufferHandle& handle) {
    vk::Buffer vertexBuffers[] = {handle.buffer};
    vk::DeviceSize offsets[] = {0};
    m_currentCommandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);
}

void RenderAPI::bindIndexBuffer(const BufferHandle& handle, vk::IndexType indexType) {
    m_currentCommandBuffer.bindIndexBuffer(handle.buffer, 0, indexType);
}

void RenderAPI::drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex) {
    m_currentCommandBuffer.drawIndexed(indexCount, instanceCount, firstIndex, 0, 0);
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

void RenderAPI::createCommandPool() {
    QueueFamilyIndices queueFamilyIndices = findQueueFamilies(m_physicalDevice);

    vk::CommandPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

    m_commandPool = m_device.createCommandPool(poolInfo);
}

void RenderAPI::createCommandBuffers() {
    m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

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

void RenderAPI::cleanupSwapchain() {
    for (auto framebuffer : m_swapchainFramebuffers) {
        m_device.destroyFramebuffer(framebuffer);
    }
    m_swapchainFramebuffers.clear();

    for (auto imageView : m_swapchainImageViews) {
        m_device.destroyImageView(imageView);
    }

    m_device.destroySwapchainKHR(m_swapchain);
}

void RenderAPI::recreateSwapchain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(m_window, &width, &height);
        glfwWaitEvents();
    }

    m_device.waitIdle();

    cleanupSwapchain();

    createSwapchain();
    createImageViews();

    // Recreate framebuffers if we have a current pipeline
    if (m_currentPipeline.renderPass) {
        m_swapchainFramebuffers.resize(m_swapchainImageViews.size());
        for (size_t i = 0; i < m_swapchainImageViews.size(); i++) {
            vk::ImageView attachments[] = {m_swapchainImageViews[i]};

            vk::FramebufferCreateInfo framebufferInfo{};
            framebufferInfo.renderPass = m_currentPipeline.renderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = attachments;
            framebufferInfo.width = m_swapchainExtent.width;
            framebufferInfo.height = m_swapchainExtent.height;
            framebufferInfo.layers = 1;

            m_swapchainFramebuffers[i] = m_device.createFramebuffer(framebufferInfo);
        }
    }
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
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

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

    return indices.isComplete() && extensionsSupported && swapchainAdequate;
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

VKAPI_ATTR VkBool32 VKAPI_CALL RenderAPI::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {

    std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}

} // namespace ailo
