#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

#include "render/RenderAPI.h"

#include <iostream>
#include <stdexcept>
#include <vector>
#include <array>

const uint32_t WIDTH = 800;
const uint32_t HEIGHT = 600;

struct Vertex {
    glm::vec2 pos;
    glm::vec3 color;

    static vk::VertexInputBindingDescription getBindingDescription() {
        vk::VertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = vk::VertexInputRate::eVertex;
        return bindingDescription;
    }

    static std::array<vk::VertexInputAttributeDescription, 2> getAttributeDescriptions() {
        std::array<vk::VertexInputAttributeDescription, 2> attributeDescriptions{};

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = vk::Format::eR32G32Sfloat;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = vk::Format::eR32G32B32Sfloat;
        attributeDescriptions[1].offset = offsetof(Vertex, color);

        return attributeDescriptions;
    }
};

const std::vector<Vertex> vertices = {
    {{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},
    {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
    {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}
};

const std::vector<uint16_t> indices = {
    0, 1, 2
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
    bool m_framebufferResized = false;

    void initWindow() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        m_window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan Window", nullptr, nullptr);
        glfwSetWindowUserPointer(m_window, this);
        glfwSetFramebufferSizeCallback(m_window, framebufferResizeCallback);
    }

    static void framebufferResizeCallback(GLFWwindow* window, int width, int height) {
        auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
        app->m_framebufferResized = true;
        app->m_renderAPI.handleWindowResize();
    }

    void initRender() {
        // Initialize render API
        m_renderAPI.init(m_window, WIDTH, HEIGHT);

        // Create vertex buffer
        m_vertexBuffer = m_renderAPI.createVertexBuffer(vertices.data(), sizeof(vertices[0]) * vertices.size());

        // Create index buffer
        m_indexBuffer = m_renderAPI.createIndexBuffer(indices.data(), sizeof(indices[0]) * indices.size());

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
            vertexInput
        );
    }

    void mainLoop() {
        while (!glfwWindowShouldClose(m_window)) {
            glfwPollEvents();
            drawFrame();
        }
        m_renderAPI.waitIdle();
    }

    void drawFrame() {
        uint32_t imageIndex;
        if (!m_renderAPI.beginFrame(imageIndex)) {
            return; // Swapchain was recreated, try again next frame
        }

        // Bind pipeline and begin render pass
        m_renderAPI.bindPipeline(m_pipeline);
        m_renderAPI.beginRenderPass(imageIndex);

        // Bind buffers and draw
        m_renderAPI.bindVertexBuffer(m_vertexBuffer);
        m_renderAPI.bindIndexBuffer(m_indexBuffer);
        m_renderAPI.drawIndexed(static_cast<uint32_t>(indices.size()));

        // End render pass and frame
        m_renderAPI.endRenderPass();
        m_renderAPI.endFrame(imageIndex);
    }

    void cleanup() {
        // Destroy resources
        m_renderAPI.destroyBuffer(m_vertexBuffer);
        m_renderAPI.destroyBuffer(m_indexBuffer);
        m_renderAPI.destroyPipeline(m_pipeline);

        // Shutdown render API
        m_renderAPI.shutdown();

        // Cleanup GLFW
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
