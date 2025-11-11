#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.hpp>

#include <glm/glm.hpp>
#include <vector>
#include <optional>
#include <string>

namespace ailo {

struct BufferHandle {
    vk::Buffer buffer;
    vk::DeviceMemory memory;
    vk::DeviceSize size;
};

struct PipelineHandle {
    vk::Pipeline pipeline;
    vk::PipelineLayout layout;
    vk::RenderPass renderPass;
    vk::DescriptorSetLayout descriptorSetLayout;
};

struct DescriptorSetHandle {
    vk::DescriptorSet descriptorSet;
};

struct VertexInputDescription {
    std::vector<vk::VertexInputBindingDescription> bindings;
    std::vector<vk::VertexInputAttributeDescription> attributes;
};

class RenderAPI {
public:
    RenderAPI() = default;
    ~RenderAPI() = default;

    // Initialization and shutdown
    void init(GLFWwindow* window, uint32_t width, uint32_t height);
    void shutdown();

    // Frame lifecycle
    bool beginFrame(uint32_t& outImageIndex);
    void endFrame(uint32_t imageIndex);
    void waitIdle();

    // Buffer management
    BufferHandle createVertexBuffer(const void* data, vk::DeviceSize size);
    BufferHandle createIndexBuffer(const void* data, vk::DeviceSize size);
    BufferHandle createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties);
    void destroyBuffer(const BufferHandle& handle);
    void updateBuffer(const BufferHandle& handle, const void* data, vk::DeviceSize size);

    // Descriptor set management
    vk::DescriptorSetLayout createDescriptorSetLayout(const std::vector<vk::DescriptorSetLayoutBinding>& bindings);
    void destroyDescriptorSetLayout(vk::DescriptorSetLayout layout);
    DescriptorSetHandle createDescriptorSet(vk::DescriptorSetLayout layout);
    void destroyDescriptorSet(const DescriptorSetHandle& handle);
    void updateDescriptorSetBuffer(const DescriptorSetHandle& descriptorSet, const BufferHandle& buffer, uint32_t binding = 0);
    void bindDescriptorSet(const DescriptorSetHandle& descriptorSet, vk::PipelineLayout pipelineLayout, uint32_t firstSet = 0);

    // Pipeline management
    PipelineHandle createGraphicsPipeline(
        const std::string& vertShaderPath,
        const std::string& fragShaderPath,
        const VertexInputDescription& vertexInput,
        vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList,
        vk::DescriptorSetLayout descriptorSetLayout = nullptr
    );
    void destroyPipeline(const PipelineHandle& handle);

    // Command recording (call between beginFrame and endFrame)
    void beginRenderPass(uint32_t imageIndex, vk::ClearColorValue clearColor = vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}));
    void endRenderPass();
    void bindPipeline(const PipelineHandle& handle);
    void bindVertexBuffer(const BufferHandle& handle);
    void bindIndexBuffer(const BufferHandle& handle, vk::IndexType indexType = vk::IndexType::eUint16);
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0);
    void setViewport(float x, float y, float width, float height);
    void setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height);

    // Swapchain management
    void handleWindowResize();
    vk::Extent2D getSwapchainExtent() const { return m_swapchainExtent; }

private:
    // Internal initialization
    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapchain();
    void createImageViews();
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();
    void createDescriptorPool();

    // Internal cleanup
    void cleanupSwapchain();
    void recreateSwapchain();

    // Helper functions
    bool checkValidationLayerSupport();
    std::vector<const char*> getRequiredExtensions();
    bool isDeviceSuitable(vk::PhysicalDevice device);
    bool checkDeviceExtensionSupport(vk::PhysicalDevice device);

    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;
        bool isComplete() const { return graphicsFamily.has_value() && presentFamily.has_value(); }
    };
    QueueFamilyIndices findQueueFamilies(vk::PhysicalDevice device);

    struct SwapchainSupportDetails {
        vk::SurfaceCapabilitiesKHR capabilities;
        std::vector<vk::SurfaceFormatKHR> formats;
        std::vector<vk::PresentModeKHR> presentModes;
    };
    SwapchainSupportDetails querySwapchainSupport(vk::PhysicalDevice device);

    vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats);
    vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes);
    vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities);

    vk::ShaderModule createShaderModule(const std::vector<char>& code);
    uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);
    void copyBuffer(vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::DeviceSize size);

    // Debug callback
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);

private:
    // Window
    GLFWwindow* m_window = nullptr;
    uint32_t m_windowWidth = 0;
    uint32_t m_windowHeight = 0;
    bool m_framebufferResized = false;

    // Core Vulkan objects
    vk::Instance m_instance;
    vk::DebugUtilsMessengerEXT m_debugMessenger;
    vk::SurfaceKHR m_surface;
    vk::PhysicalDevice m_physicalDevice;
    vk::Device m_device;
    vk::Queue m_graphicsQueue;
    vk::Queue m_presentQueue;

    // Swapchain
    vk::SwapchainKHR m_swapchain;
    std::vector<vk::Image> m_swapchainImages;
    vk::Format m_swapchainImageFormat;
    vk::Extent2D m_swapchainExtent;
    std::vector<vk::ImageView> m_swapchainImageViews;
    std::vector<vk::Framebuffer> m_swapchainFramebuffers;

    // Command buffers
    vk::CommandPool m_commandPool;
    std::vector<vk::CommandBuffer> m_commandBuffers;

    // Descriptor pool
    vk::DescriptorPool m_descriptorPool;

    // Synchronization
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    std::vector<vk::Semaphore> m_imageAvailableSemaphores;
    std::vector<vk::Semaphore> m_renderFinishedSemaphores;
    std::vector<vk::Fence> m_inFlightFences;
    uint32_t m_currentFrame = 0;

    // Validation
    bool m_enableValidationLayers = true;
    std::vector<const char*> m_validationLayers = {"VK_LAYER_KHRONOS_validation"};
    std::vector<const char*> m_deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    // Current render state
    vk::CommandBuffer m_currentCommandBuffer;
    PipelineHandle m_currentPipeline;
};

} // namespace ailo
