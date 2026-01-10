#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

#include <glm/glm.hpp>
#include <vector>
#include <optional>
#include <string>
#include <bitset>

#include "utils/ResourceAllocator.h"
#include "CommandBuffer.h"

namespace ailo {

namespace gpu {

struct Pipeline;
struct Buffer;
struct DescriptorSet;
struct Texture;
struct DescriptorSetLayout;

}

using PipelineHandle = Handle<gpu::Pipeline>;
using BufferHandle = Handle<gpu::Buffer>;
using DescriptorSetHandle = Handle<gpu::DescriptorSet>;
using TextureHandle = Handle<gpu::Texture>;
using DescriptorSetLayoutHandle = Handle<gpu::DescriptorSetLayout>;

enum class BufferBinding : uint8_t {
  UNKNOWN,
  VERTEX,
  INDEX,
  UNIFORM,
};

enum class CullingMode : uint8_t {
  NONE,
  FRONT,
  BACK,
  FRONT_AND_BACK
};

enum class BlendOperation : uint8_t {
  ADD,
  SUBTRACT,
  REVERSE_SUBTRACT,
  MIN,
  MAX
};

enum class BlendFunction : uint8_t {
  ZERO,
  ONE,
  SRC_COLOR,
  ONE_MINUS_SRC_COLOR,
  DST_COLOR,
  ONE_MINUS_DST_COLOR,
  SRC_ALPHA,
  ONE_MINUS_SRC_ALPHA,
  DST_ALPHA,
  ONE_MINUS_DST_ALPHA,
  SRC_ALPHA_SATURATE
};

enum class CompareOp : uint8_t {
  NEVER = 0,
  LESS,
  EQUAL,
  LESS_OR_EQUAL,
  GREATER,
  NOT_EQUAL,
  GREATER_OR_EQUAL,
  ALWAYS
};

class Acquirable {
public:
    void setFence(const std::shared_ptr<FenceStatus>& fence) { m_fenceStatus = fence; }
    bool isAcquired() const { return m_fenceStatus && !m_fenceStatus->isSignaled(); }

private:
    std::shared_ptr<FenceStatus> m_fenceStatus;
};

namespace gpu {

struct Buffer {
    vk::Buffer buffer;
    uint64_t size;
    VmaAllocation vmaAllocation;
    VmaAllocationInfo allocationInfo;
    BufferBinding binding;
};

struct StageBuffer : public Acquirable {
    vk::Buffer buffer;
    uint64_t size;
    VmaAllocation vmaAllocation;
    void* mapping;
};

struct Pipeline {
    vk::Pipeline pipeline;
    vk::PipelineLayout layout;
    std::vector<vk::DescriptorSetLayout> descriptorSetLayouts;
};

struct DescriptorSetLayout {
    using bitmask_t = std::bitset<64>;

    vk::DescriptorSetLayout layout;
    bitmask_t dynamicBindings;
};

struct DescriptorSet {
    vk::DescriptorSet descriptorSet;
    DescriptorSetLayout::bitmask_t boundBindings;
    DescriptorSetLayout::bitmask_t dynamicBindings;
    DescriptorSetLayoutHandle layoutHandle;
    std::shared_ptr<FenceStatus> boundFence;

    bool isBound() const { return boundFence && !boundFence->isSignaled(); }
};

struct Texture {
    vk::Image image;
    vk::DeviceMemory memory;
    vk::ImageView imageView;
    vk::Sampler sampler;
    vk::Format format;
    uint32_t width;
    uint32_t height;
};

struct RenderTarget {
    std::array<TextureHandle, 8> colorAttachments;
    TextureHandle depthAttachment;
};

}

struct VertexInputDescription {
    std::vector<vk::VertexInputBindingDescription> bindings;
    std::vector<vk::VertexInputAttributeDescription> attributes;
};

struct RasterDescription {
    CullingMode cullingMode = CullingMode::FRONT;
    bool inverseFrontFace = false;
    bool blendEnable = false;
    bool depthWriteEnable = true;
    BlendOperation rgbBlendOp;
    BlendOperation alphaBlendOp;
    BlendFunction srcRgbBlendFunc;
    BlendFunction srcAlphaBlendFunc;
    BlendFunction dstRgbBlendFunc;
    BlendFunction dstAlphaBlendFunc;
    CompareOp depthCompareOp = CompareOp::LESS;
};

struct DescriptorSetLayoutBinding {
    uint32_t binding;
    vk::DescriptorType descriptorType;
    vk::ShaderStageFlags stageFlags;
};

struct ShaderDescription {
    using ShaderCode = std::vector<char>;
    using SetLayout = std::vector<DescriptorSetLayoutBinding>;

    ShaderCode vertexShader;
    ShaderCode fragmentShader;
    RasterDescription raster;
    std::vector<SetLayout> layout;
};

struct PipelineDescription {
  ShaderDescription shader;
  VertexInputDescription vertexInput;
};

class SwapChain;

class RenderAPI {
public:
    RenderAPI();
    ~RenderAPI();

    // Initialization and shutdown
    void init(GLFWwindow* window);
    void shutdown();

    // Frame lifecycle
    bool beginFrame();
    void endFrame();
    void waitIdle();

    // Buffer management
    BufferHandle createVertexBuffer(const void* data, uint64_t size);
    BufferHandle createIndexBuffer(const void* data, uint64_t size);
    BufferHandle createBuffer(BufferBinding, uint64_t size);
    void destroyBuffer(const BufferHandle& handle);
    void updateBuffer(const BufferHandle& handle, const void* data, uint64_t size, uint64_t byteOffset = 0);

    // Texture management
    TextureHandle createTexture(vk::Format format, uint32_t width, uint32_t height, vk::Filter filter = vk::Filter::eLinear);
    void destroyTexture(const TextureHandle& handle);
    void updateTextureImage(const TextureHandle& handle, const void* data, size_t dataSize, uint32_t width = 0, uint32_t height = 0, uint32_t xOffset = 0, uint32_t yOffset = 0);

    // Descriptor set management
    DescriptorSetLayoutHandle createDescriptorSetLayout(const std::vector<DescriptorSetLayoutBinding>& bindings);
    void destroyDescriptorSetLayout(DescriptorSetLayoutHandle& dslh);
    DescriptorSetHandle createDescriptorSet(DescriptorSetLayoutHandle dslh);
    void destroyDescriptorSet(const DescriptorSetHandle& handle);
    void updateDescriptorSetBuffer(const DescriptorSetHandle& descriptorSet, const BufferHandle& buffer, uint32_t binding, uint64_t offset = 0, uint64_t size = std::numeric_limits<decltype(size)>::max());
    void updateDescriptorSetTexture(const DescriptorSetHandle& descriptorSet, const TextureHandle& texture, uint32_t binding = 0);

    // Pipeline management
    PipelineHandle createGraphicsPipeline(
        const PipelineDescription& description
    );
    void destroyPipeline(const PipelineHandle& handle);

    // Command recording (call between beginFrame and endFrame)
    void beginRenderPass(vk::ClearColorValue clearColor = vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}));
    void endRenderPass();
    void bindPipeline(const PipelineHandle& handle);
    void bindVertexBuffer(const BufferHandle& handle);
    // FIXME: remove indexType from here, save it on creation instead
    void bindIndexBuffer(const BufferHandle& handle, vk::IndexType indexType = vk::IndexType::eUint16);
    void bindDescriptorSet(const DescriptorSetHandle& descriptorSet, uint32_t setIndex, std::initializer_list<uint32_t> dynamicOffsets = { });
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0, int32_t vertexOffset = 0);
    void setViewport(float x, float y, float width, float height);
    void setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height);

    // Swapchain management
    void handleWindowResize();

private:
    using Buffer = gpu::Buffer;
    using DescriptorSet = gpu::DescriptorSet;
    using Texture = gpu::Texture;
    using Pipeline = gpu::Pipeline;
    using DescriptorSetLayout = gpu::DescriptorSetLayout;
    using StageBuffer = gpu::StageBuffer;

    // Internal initialization
    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapchain();
    void createCommandPool();
    void createCommandBuffers();
    void createDescriptorPool();
    void createAllocator();

    // Internal cleanup
    void cleanupSwapchain();
    void recreateSwapchain();
    void cleanupSwpachainFramebuffer();
    void createSwapchainFramebuffers();

    void cleanupDescriptorSets();

    // Helper functions
    bool checkValidationLayerSupport();
    std::vector<const char*> getRequiredExtensions();
    bool isDeviceSuitable(vk::PhysicalDevice device);
    bool checkDeviceExtensionSupport(vk::PhysicalDevice device);

    static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats);
    static vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes);
    static vk::Extent2D chooseSwapExtent(GLFWwindow*, const vk::SurfaceCapabilitiesKHR& capabilities);

    void createRenderPass();

    void createDescriptorSet(DescriptorSet&, DescriptorSetLayoutHandle);
    vk::ShaderModule createShaderModule(const std::vector<char>& code);
    uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);
    void allocateBuffer(Buffer& buffer, vk::BufferUsageFlags usageFlags, uint32_t numBytes);
    StageBuffer allocateStageBuffer(uint32_t capacity);
    void destroyStageBuffers();
    void loadFromCpu(vk::CommandBuffer& commandBuffer, const Buffer& bufferHandle, const void* data, uint32_t byteOffset, uint32_t numBytes);
    void copyBuffer(vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::DeviceSize size);
    vk::Format findDepthFormat();
    void createImage(uint32_t width, uint32_t height, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties, vk::Image& image, vk::DeviceMemory& imageMemory);
    vk::ImageView createImageView(vk::Image image, vk::Format format, vk::ImageAspectFlags aspectFlags);
    void transitionImageLayout(vk::Image image, vk::Format format, vk::ImageLayout oldLayout, vk::ImageLayout newLayout);
    void copyBufferToImage(vk::Buffer buffer, vk::Image image, uint32_t width, uint32_t height, uint32_t xOffset = 0, uint32_t yOffset = 0);

    // Debug callback
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);

private:
    // Window
    GLFWwindow* m_window = nullptr;
    bool m_framebufferResized = false;

    // Core Vulkan objects
    vk::Instance m_instance;
    vk::DebugUtilsMessengerEXT m_debugMessenger;
    vk::SurfaceKHR m_surface;
    vk::PhysicalDevice m_physicalDevice;
    vk::Device m_device;
    vk::Queue m_graphicsQueue;
    vk::Queue m_presentQueue;

    friend class SwapChain;
    std::vector<vk::Framebuffer> m_swapchainFramebuffers;

    vk::RenderPass m_renderPass;

    // Depth buffering
    vk::Image m_depthImage;
    vk::DeviceMemory m_depthImageMemory;
    vk::ImageView m_depthImageView;

    // Command buffers
    vk::CommandPool m_commandPool;
    CommandsPool m_commands;

    // Descriptor pool
    vk::DescriptorPool m_descriptorPool;

    VmaAllocator m_Allocator = nullptr;

    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;

    // Validation
    bool m_enableValidationLayers = true;
    std::vector<const char*> m_validationLayers = {"VK_LAYER_KHRONOS_validation"};
    std::vector<const char*> m_deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    // Current render state
    PipelineHandle m_currentPipeline;
    std::vector<StageBuffer> m_stageBuffers;

    std::vector<DescriptorSet> m_descriptorSetsToDestroy;
    // resources
    ResourceAllocator<Pipeline> m_pipelines;
    ResourceAllocator<Buffer> m_buffers;
    ResourceAllocator<DescriptorSetLayout> m_descriptorSetLayouts;
    ResourceAllocator<DescriptorSet> m_descriptorSets;
    ResourceAllocator<Texture> m_textures;

    std::unique_ptr<SwapChain> m_swapChain;
};

} // namespace ailo
