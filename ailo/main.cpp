#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <stb_image/stb_image.h>

#include "render/RenderAPI.h"

#include <iostream>
#include <stdexcept>
#include <vector>
#include <array>
#include <chrono>

const uint32_t WIDTH = 800;
const uint32_t HEIGHT = 600;

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;

    static vk::VertexInputBindingDescription getBindingDescription() {
        vk::VertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = vk::VertexInputRate::eVertex;
        return bindingDescription;
    }

    static std::array<vk::VertexInputAttributeDescription, 3> getAttributeDescriptions() {
        std::array<vk::VertexInputAttributeDescription, 3> attributeDescriptions{};

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = vk::Format::eR32G32B32Sfloat;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = vk::Format::eR32G32B32Sfloat;
        attributeDescriptions[1].offset = offsetof(Vertex, color);

        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = vk::Format::eR32G32Sfloat;
        attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

        return attributeDescriptions;
    }
};

const std::vector<Vertex> vertices = {
    // Back face
    {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
    {{0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
    {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
    {{-0.5f, 0.5f, -0.5f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
    // Front face
    {{-0.5f, -0.5f, 0.5f}, {1.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
    {{0.5f, -0.5f, 0.5f}, {0.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
    {{0.5f, 0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
    {{-0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {0.0f, 1.0f}}
};

const std::vector<uint16_t> indices = {
    // Back face
    0, 1, 2, 2, 3, 0,
    // Front face
    5, 4, 7, 7, 6, 5,
    // Left face
    4, 0, 3, 3, 7, 4,
    // Right face
    1, 5, 6, 6, 2, 1,
    // Bottom face
    4, 5, 1, 1, 0, 4,
    // Top face
    3, 2, 6, 6, 7, 3
};

struct UniformBufferObject {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};

class Application {
public:
    void run() {
        initWindow();
        initRender();
        mainLoop();
        cleanup();
    }

private:
    GLFWwindow* m_window = nullptr;
    ailo::RenderAPI m_renderAPI;
    ailo::BufferHandle m_vertexBuffer;
    ailo::BufferHandle m_indexBuffer;
    ailo::PipelineHandle m_pipeline;
    ailo::TextureHandle m_texture;

    ailo::BufferHandle m_uniformBuffer;
    vk::DescriptorSetLayout m_descriptorSetLayout;
    ailo::DescriptorSetHandle m_descriptorSet;

    void initWindow() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        m_window = glfwCreateWindow(WIDTH, HEIGHT, "Ailo", nullptr, nullptr);
        glfwSetWindowUserPointer(m_window, this);
        glfwSetFramebufferSizeCallback(m_window, framebufferResizeCallback);
    }

    static void framebufferResizeCallback(GLFWwindow* window, int width, int height) {
        auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
        app->m_renderAPI.handleWindowResize();
    }

    void initRender() {
        m_renderAPI.init(m_window, WIDTH, HEIGHT);

        m_vertexBuffer = m_renderAPI.createVertexBuffer(vertices.data(), sizeof(vertices[0]) * vertices.size());
        m_indexBuffer = m_renderAPI.createIndexBuffer(indices.data(), sizeof(indices[0]) * indices.size());

        // Load texture
        int texWidth, texHeight, texChannels;
        stbi_uc* pixels = stbi_load("textures/gray.jpg", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        if (!pixels) {
            throw std::runtime_error("failed to load texture image!");
        }
        vk::DeviceSize imageSize = texWidth * texHeight * 4;

        // Create texture
        m_texture = m_renderAPI.createTexture(vk::Format::eR8G8B8A8Srgb, texWidth, texHeight);
        m_renderAPI.updateTextureImage(m_texture, pixels, imageSize);

        stbi_image_free(pixels);

        // Create descriptor set layout for uniform buffer and texture
        vk::DescriptorSetLayoutBinding uboLayoutBinding{};
        uboLayoutBinding.binding = 0;
        uboLayoutBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.stageFlags = vk::ShaderStageFlagBits::eVertex;

        vk::DescriptorSetLayoutBinding samplerLayoutBinding{};
        samplerLayoutBinding.binding = 1;
        samplerLayoutBinding.descriptorCount = 1;
        samplerLayoutBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        samplerLayoutBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

        std::vector<vk::DescriptorSetLayoutBinding> bindings = {uboLayoutBinding, samplerLayoutBinding};
        m_descriptorSetLayout = m_renderAPI.createDescriptorSetLayout(bindings);

        // Create uniform buffer
        m_uniformBuffer = m_renderAPI.createBuffer(sizeof(UniformBufferObject));

        // Create descriptor set
        m_descriptorSet = m_renderAPI.createDescriptorSet(m_descriptorSetLayout);
        m_renderAPI.updateDescriptorSetBuffer(m_descriptorSet, m_uniformBuffer, 0);
        m_renderAPI.updateDescriptorSetTexture(m_descriptorSet, m_texture, 1);

        // Create graphics pipeline
        ailo::VertexInputDescription vertexInput;
        vertexInput.bindings.push_back(Vertex::getBindingDescription());
        auto attributeDescriptions = Vertex::getAttributeDescriptions();
        vertexInput.attributes = std::vector<vk::VertexInputAttributeDescription>(
            attributeDescriptions.begin(),
            attributeDescriptions.end()
        );

        m_pipeline = m_renderAPI.createGraphicsPipeline(
            "shaders/shader.vert.spv",
            "shaders/shader.frag.spv",
            vertexInput,
            vk::PrimitiveTopology::eTriangleList,
            m_descriptorSetLayout
        );
    }

    void mainLoop() {
        while (!glfwWindowShouldClose(m_window)) {
            glfwPollEvents();
            drawFrame();
        }
        m_renderAPI.waitIdle();
    }

    void updateUniformBuffer() {
        UniformBufferObject ubo{};

        // Model matrix
        static auto startTime = std::chrono::high_resolution_clock::now();
        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

        ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f) , glm::vec3(0.0f, 0.0f, 1.0f)) *
            glm::rotate(glm::mat4(1.0f), 1.3f * time * glm::radians(45.0f) , glm::vec3(0.0f, 1.0f, 0.0f)) *
            glm::rotate(glm::mat4(1.0f), glm::radians(45.0f) , glm::vec3(1.0f, 0.0f, 0.0f));

        // View matrix: look at the scene from above
        ubo.view = glm::lookAt(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

        // Projection matrix: perspective projection
        auto extent = m_renderAPI.getSwapchainExtent();
        ubo.proj = glm::perspective(glm::radians(45.0f), extent.width / (float)extent.height, 0.1f, 10.0f);
        ubo.proj[1][1] *= -1; // Flip Y for Vulkan

        // Update the uniform buffer
        m_renderAPI.updateBuffer(m_uniformBuffer, &ubo, sizeof(ubo));
    }

    void drawFrame() {
        uint32_t imageIndex;
        if (!m_renderAPI.beginFrame(imageIndex)) {
            return; // Swapchain was recreated, try again next frame
        }

        // Update uniform buffer for current frame
        updateUniformBuffer();

        // Bind pipeline and begin render pass
        m_renderAPI.bindPipeline(m_pipeline);
        m_renderAPI.beginRenderPass(imageIndex);

        // Bind descriptor set
        m_renderAPI.bindDescriptorSet(m_descriptorSet);

        // Bind buffers and draw
        m_renderAPI.bindVertexBuffer(m_vertexBuffer);
        m_renderAPI.bindIndexBuffer(m_indexBuffer);
        m_renderAPI.drawIndexed(static_cast<uint32_t>(indices.size()));

        // End render pass and frame
        m_renderAPI.endRenderPass();
        m_renderAPI.endFrame(imageIndex);
    }

    void cleanup() {
        // Clean up uniform buffer
        m_renderAPI.destroyBuffer(m_uniformBuffer);

        // Clean up descriptor set
        m_renderAPI.destroyDescriptorSet(m_descriptorSet);

        // Clean up descriptor set layout
        m_renderAPI.destroyDescriptorSetLayout(m_descriptorSetLayout);

        // Clean up texture
        m_renderAPI.destroyTexture(m_texture);

        m_renderAPI.destroyBuffer(m_vertexBuffer);
        m_renderAPI.destroyBuffer(m_indexBuffer);
        m_renderAPI.destroyPipeline(m_pipeline);

        m_renderAPI.shutdown();

        glfwDestroyWindow(m_window);
        glfwTerminate();
    }
};

int main() {
    Application app;

    try {
        app.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
