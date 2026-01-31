#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>

#include <vector>
#include <optional>

#include "render/vulkan/Resources.h"
#include "VulkanDevice.h"
#include "vulkan/Texture.h"
#include "Program.h"
#include "ResourceContainer.h"
#include "CommandBuffer.h"
#include "FrameBufferCache.h"
#include "PipelineCache.h"
#include "RenderPassCache.h"

namespace ailo {



class SwapChain;

class RenderAPI {
public:
    explicit RenderAPI(GLFWwindow* window);
    ~RenderAPI();

    // Initialization and shutdown
    void shutdown();

    // Frame lifecycle
    bool beginFrame();
    void endFrame();
    void waitIdle();

    // Buffer management
    VertexBufferLayoutHandle createVertexBufferLayout(const VertexInputDescription& description);
    void destroyVertexBufferLayout(VertexBufferLayoutHandle);

    BufferHandle createVertexBuffer(const void* data, uint64_t size);
    BufferHandle createIndexBuffer(const void* data, uint64_t size);
    BufferHandle createBuffer(BufferBinding, uint64_t size);
    void destroyBuffer(const BufferHandle& handle);
    void updateBuffer(const BufferHandle& handle, const void* data, uint64_t size, uint64_t byteOffset = 0);

    // Texture management
    TextureHandle createTexture(TextureType, vk::Format, uint32_t width, uint32_t height, uint8_t levels = 1);
    void destroyTexture(const TextureHandle& handle);
    void updateTextureImage(const TextureHandle& handle, const void* data, size_t dataSize, uint32_t width = 0, uint32_t height = 0, uint32_t xOffset = 0, uint32_t yOffset = 0, uint32_t baseLayer = 0, uint32_t layerCount = 1);
    void generateMipmaps(const TextureHandle& handle);

    // Descriptor set management
    DescriptorSetLayoutHandle createDescriptorSetLayout(const std::vector<DescriptorSetLayoutBinding>& bindings);
    void destroyDescriptorSetLayout(DescriptorSetLayoutHandle& dslh);
    DescriptorSetHandle createDescriptorSet(DescriptorSetLayoutHandle dslh);
    void destroyDescriptorSet(const DescriptorSetHandle& handle);
    void updateDescriptorSetBuffer(const DescriptorSetHandle& descriptorSet, const BufferHandle& buffer, uint32_t binding, uint64_t offset = 0, uint64_t size = std::numeric_limits<decltype(size)>::max());
    void updateDescriptorSetTexture(const DescriptorSetHandle& descriptorSet, const TextureHandle& texture, uint32_t binding = 0);

    // Program management

    ProgramHandle createProgram(const ShaderDescription& description);
    void destroyProgram(const ProgramHandle& handle);

    // Command recording (call between beginFrame and endFrame)
    void beginRenderPass(const RenderPassDescription& description, vk::ClearColorValue clearColor = vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}));
    void endRenderPass();
    void bindPipeline(const PipelineState& state);
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
    using Program = gpu::Program;
    using DescriptorSetLayout = gpu::DescriptorSetLayout;
    using StageBuffer = gpu::StageBuffer;
    using VertexBufferLayout = gpu::VertexBufferLayout;

    static VmaAllocator createAllocator(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device);
    static vk::DescriptorPool createDescriptorPoolS(vk::Device device);

    // Internal initialization
    void createSwapchain();

    // Internal cleanup
    void cleanupSwapchain();
    void recreateSwapchain();

    void cleanupDescriptorSets();

    void createDescriptorSet(DescriptorSet&, DescriptorSetLayoutHandle);
    void allocateBuffer(Buffer& buffer, vk::BufferUsageFlags usageFlags, uint32_t numBytes);
    StageBuffer allocateStageBuffer(uint32_t capacity);
    void destroyStageBuffers();
    void loadFromCpu(vk::CommandBuffer& commandBuffer, const Buffer& bufferHandle, const void* data, uint32_t byteOffset, uint32_t numBytes);
    void copyBufferToImage(vk::CommandBuffer commandBuffer, vk::Buffer buffer, vk::Image image, uint32_t width, uint32_t height, uint32_t xOffset = 0, uint32_t yOffset = 0, uint32_t baseLayer = 0, uint32_t layerCount = 1);

private:
    // Window
    GLFWwindow* m_window = nullptr;
    bool m_framebufferResized = false;

    // Core Vulkan objects
    VulkanDevice m_device;

    friend class SwapChain;

    // Command buffers
    vk::CommandPool m_commandPool;
    CommandsPool m_commands;

    // Descriptor pool
    vk::DescriptorPool m_descriptorPool;

    VmaAllocator m_Allocator = nullptr;

    std::vector<StageBuffer> m_stageBuffers;
    std::vector<DescriptorSet> m_descriptorSetsToDestroy;

    // resources
    ResourceContainer<Buffer> m_buffers;
    ResourceContainer<DescriptorSetLayout> m_descriptorSetLayouts;
    ResourceContainer<DescriptorSet> m_descriptorSets;
    ResourceContainer<Texture> m_textures;
    ResourceContainer<Program> m_programs;
    ResourceContainer<Pipeline> m_graphicsPipelines;
    ResourceContainer<VertexBufferLayout> m_vertexBufferLayouts;

    std::unique_ptr<SwapChain> m_swapChain;
    FrameBufferCache m_framebufferCache;
    RenderPassCache m_renderPassCache;
    PipelineCache m_pipelineCache;

};

} // namespace ailo
