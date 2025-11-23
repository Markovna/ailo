#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <stb_image/stb_image.h>

#include "Engine.h"
#include "render/RenderAPI.h"
#include "input/InputTypes.h"
#include "input/InputSystem.h"

#include <iostream>
#include <stdexcept>
#include <vector>
#include <array>
#include <chrono>

// Helper functions for GLFW to platform-agnostic conversion
static ailo::KeyCode glfwKeyToKeyCode(int glfwKey);
static ailo::MouseButton glfwButtonToMouseButton(int glfwButton);
static ailo::ModifierKey glfwModsToModifierKey(int glfwMods);

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
    ailo::Engine m_engine;
    ailo::BufferHandle m_vertexBuffer;
    ailo::BufferHandle m_indexBuffer;
    ailo::PipelineHandle m_pipeline;
    ailo::TextureHandle m_texture;

    ailo::BufferHandle m_uniformBuffer;
    vk::DescriptorSetLayout m_descriptorSetLayout;
    ailo::DescriptorSetHandle m_descriptorSet;

    // GLFW callback functions
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);

    void initWindow() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        m_window = glfwCreateWindow(WIDTH, HEIGHT, "Ailo", nullptr, nullptr);

        glfwSetWindowUserPointer(m_window, this);

        // Register GLFW callbacks
        glfwSetFramebufferSizeCallback(m_window, framebufferResizeCallback);
        glfwSetKeyCallback(m_window, keyCallback);
        glfwSetMouseButtonCallback(m_window, mouseButtonCallback);
        glfwSetCursorPosCallback(m_window, cursorPosCallback);
        glfwSetScrollCallback(m_window, scrollCallback);
    }

    static void framebufferResizeCallback(GLFWwindow* window, int width, int height) {
        auto app = static_cast<Application*>(glfwGetWindowUserPointer(window));
        app->m_engine.getRenderAPI()->handleWindowResize();
    }

    void initRender() {
        auto* renderAPI = m_engine.getRenderAPI();
        renderAPI->init(m_window, WIDTH, HEIGHT);

        m_vertexBuffer = renderAPI->createVertexBuffer(vertices.data(), sizeof(vertices[0]) * vertices.size());
        m_indexBuffer = renderAPI->createIndexBuffer(indices.data(), sizeof(indices[0]) * indices.size());

        // Load texture
        int texWidth, texHeight, texChannels;
        stbi_uc* pixels = stbi_load("textures/gray.jpg", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        if (!pixels) {
            throw std::runtime_error("failed to load texture image!");
        }
        vk::DeviceSize imageSize = texWidth * texHeight * 4;

        // Create texture
        m_texture = renderAPI->createTexture(vk::Format::eR8G8B8A8Srgb, texWidth, texHeight);
        renderAPI->updateTextureImage(m_texture, pixels, imageSize);

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
        m_descriptorSetLayout = renderAPI->createDescriptorSetLayout(bindings);

        // Create uniform buffer
        m_uniformBuffer = renderAPI->createBuffer(sizeof(UniformBufferObject));

        // Create descriptor set
        m_descriptorSet = renderAPI->createDescriptorSet(m_descriptorSetLayout);
        renderAPI->updateDescriptorSetBuffer(m_descriptorSet, m_uniformBuffer, 0);
        renderAPI->updateDescriptorSetTexture(m_descriptorSet, m_texture, 1);

        // Create graphics pipeline
        ailo::VertexInputDescription vertexInput;
        vertexInput.bindings.push_back(Vertex::getBindingDescription());
        auto attributeDescriptions = Vertex::getAttributeDescriptions();
        vertexInput.attributes = std::vector<vk::VertexInputAttributeDescription>(
            attributeDescriptions.begin(),
            attributeDescriptions.end()
        );

        m_pipeline = renderAPI->createGraphicsPipeline(
            "shaders/shader.vert.spv",
            "shaders/shader.frag.spv",
            vertexInput,
            m_descriptorSetLayout
        );
    }

    void mainLoop() {
        while (!glfwWindowShouldClose(m_window)) {
            glfwPollEvents();
            m_engine.getInputSystem()->processEvents();
            handleInput();
            drawFrame();
        }
        m_engine.getRenderAPI()->waitIdle();
    }

    void handleInput() {
        auto* inputSystem = m_engine.getInputSystem();
        // Process all input events from the queue
        ailo::Event event;
        while (inputSystem->pollEvent(event)) {
            std::visit([this](auto&& e) {
                using T = std::decay_t<decltype(e)>;

                if constexpr (std::is_same_v<T, ailo::KeyPressedEvent>) {
                    // Example: Close window on Escape key
                    if (e.keyCode == ailo::KeyCode::Escape) {
                        glfwSetWindowShouldClose(m_window, GLFW_TRUE);
                        std::cout << "Escape pressed - closing window\n";
                    }
                    // Example: Print other key presses
                    else {
                        std::cout << "Key pressed: " << static_cast<int>(e.keyCode) << "\n";
                    }
                }
                else if constexpr (std::is_same_v<T, ailo::KeyReleasedEvent>) {
                    std::cout << "Key released: " << static_cast<int>(e.keyCode) << "\n";
                }
                else if constexpr (std::is_same_v<T, ailo::MouseButtonPressedEvent>) {
                    std::cout << "Mouse button " << static_cast<int>(e.button)
                              << " pressed at (" << e.x << ", " << e.y << ")\n";
                }
                else if constexpr (std::is_same_v<T, ailo::MouseButtonReleasedEvent>) {
                    std::cout << "Mouse button " << static_cast<int>(e.button) << " released\n";
                }
                else if constexpr (std::is_same_v<T, ailo::MouseMovedEvent>) {
                    // Mouse moved events are very frequent, so we don't print them by default
                    // Uncomment to see mouse movement:
                    // std::cout << "Mouse moved to (" << e.x << ", " << e.y << ")\n";
                }
                else if constexpr (std::is_same_v<T, ailo::MouseScrolledEvent>) {
                    std::cout << "Mouse scrolled: (" << e.xOffset << ", " << e.yOffset << ")\n";
                }
            }, event);
        }
    }

    void updateUniformBuffer() {
        UniformBufferObject ubo{};

        auto* renderAPI = m_engine.getRenderAPI();
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
        auto extent = renderAPI->getSwapchainExtent();
        ubo.proj = glm::perspective(glm::radians(45.0f), extent.width / (float)extent.height, 0.1f, 10.0f);
        ubo.proj[1][1] *= -1; // Flip Y for Vulkan

        // Update the uniform buffer
        renderAPI->updateBuffer(m_uniformBuffer, &ubo, sizeof(ubo));
    }

    void drawFrame() {
        auto* renderAPI = m_engine.getRenderAPI();
        uint32_t imageIndex;
        if (!renderAPI->beginFrame(imageIndex)) {
            return; // Swapchain was recreated, try again next frame
        }

        // Update uniform buffer for current frame
        updateUniformBuffer();

        // Bind pipeline and begin render pass
        renderAPI->bindPipeline(m_pipeline);
        renderAPI->beginRenderPass(imageIndex);

        // Bind descriptor set
        renderAPI->bindDescriptorSet(m_descriptorSet);

        // Bind buffers and draw
        renderAPI->bindVertexBuffer(m_vertexBuffer);
        renderAPI->bindIndexBuffer(m_indexBuffer);
        renderAPI->drawIndexed(static_cast<uint32_t>(indices.size()));

        // End render pass and frame
        renderAPI->endRenderPass();
        renderAPI->endFrame(imageIndex);
    }

    void cleanup() {
        auto* renderAPI = m_engine.getRenderAPI();
        // Clean up uniform buffer
        renderAPI->destroyBuffer(m_uniformBuffer);

        // Clean up descriptor set
        renderAPI->destroyDescriptorSet(m_descriptorSet);

        // Clean up descriptor set layout
        renderAPI->destroyDescriptorSetLayout(m_descriptorSetLayout);

        // Clean up texture
        renderAPI->destroyTexture(m_texture);

        renderAPI->destroyBuffer(m_vertexBuffer);
        renderAPI->destroyBuffer(m_indexBuffer);
        renderAPI->destroyPipeline(m_pipeline);

        renderAPI->shutdown();

        glfwSetKeyCallback(m_window, nullptr);
        glfwSetMouseButtonCallback(m_window, nullptr);
        glfwSetCursorPosCallback(m_window, nullptr);
        glfwSetScrollCallback(m_window, nullptr);
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

// Window Callbacks

void Application::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
  auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
  auto inputSystem = app->m_engine.getInputSystem();

  ailo::KeyCode keyCode = glfwKeyToKeyCode(key);
  ailo::ModifierKey modifiers = glfwModsToModifierKey(mods);

  // Create and push event
  if (action == GLFW_PRESS) {
    ailo::KeyPressedEvent event;
    event.keyCode = keyCode;
    event.modifiers = modifiers;
    inputSystem->pushEvent(event);
  } else if (action == GLFW_RELEASE) {
    ailo::KeyReleasedEvent event;
    event.keyCode = keyCode;
    event.modifiers = modifiers;
    inputSystem->pushEvent(event);
  } else if (action == GLFW_REPEAT) {
    ailo::KeyRepeatedEvent event;
    event.keyCode = keyCode;
    event.modifiers = modifiers;
    inputSystem->pushEvent(event);
  }
}

void Application::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
  auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
  auto* inputSystem = app->m_engine.getInputSystem();

  ailo::MouseButton mouseButton = glfwButtonToMouseButton(button);
  ailo::ModifierKey modifiers = glfwModsToModifierKey(mods);

  double mouseX, mouseY;
  glfwGetCursorPos(window, &mouseX, &mouseY);

  // Create and push event
  if (action == GLFW_PRESS) {
    ailo::MouseButtonPressedEvent event;
    event.button = mouseButton;
    event.modifiers = modifiers;
    event.x = mouseX;
    event.y = mouseY;
    inputSystem->pushEvent(event);
  } else if (action == GLFW_RELEASE) {
    ailo::MouseButtonReleasedEvent event;
    event.button = mouseButton;
    event.modifiers = modifiers;
    event.x = mouseX;
    event.y = mouseY;
    inputSystem->pushEvent(event);
  }
}

void Application::cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
  auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
  auto* inputSystem = app->m_engine.getInputSystem();

  // Create and push event
  ailo::MouseMovedEvent event;
  event.x = xpos;
  event.y = ypos;
  inputSystem->pushEvent(event);
}

void Application::scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
  auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
  auto* inputSystem = app->m_engine.getInputSystem();

  // Create and push event
  ailo::MouseScrolledEvent event;
  event.xOffset = xoffset;
  event.yOffset = yoffset;
  inputSystem->pushEvent(event);
}

// Conversion functions

ailo::KeyCode glfwKeyToKeyCode(int glfwKey) {
  // GLFW key codes are designed to match our KeyCode enum values
  // so we can do a direct cast for most keys
  if (glfwKey == GLFW_KEY_UNKNOWN) {
    return ailo::KeyCode::Unknown;
  }
  return static_cast<ailo::KeyCode>(glfwKey);
}

ailo::MouseButton glfwButtonToMouseButton(int glfwButton) {
  // Direct mapping
  switch (glfwButton) {
    case GLFW_MOUSE_BUTTON_LEFT:   return ailo::MouseButton::Left;
    case GLFW_MOUSE_BUTTON_RIGHT:  return ailo::MouseButton::Right;
    case GLFW_MOUSE_BUTTON_MIDDLE: return ailo::MouseButton::Middle;
    case GLFW_MOUSE_BUTTON_4:      return ailo::MouseButton::Button4;
    case GLFW_MOUSE_BUTTON_5:      return ailo::MouseButton::Button5;
    case GLFW_MOUSE_BUTTON_6:      return ailo::MouseButton::Button6;
    case GLFW_MOUSE_BUTTON_7:      return ailo::MouseButton::Button7;
    case GLFW_MOUSE_BUTTON_8:      return ailo::MouseButton::Button8;
    default:                        return ailo::MouseButton::Left;
  }
}

ailo::ModifierKey glfwModsToModifierKey(int glfwMods) {
  ailo::ModifierKey mods = ailo::ModifierKey::None;

  if (glfwMods & GLFW_MOD_SHIFT)     mods |= ailo::ModifierKey::Shift;
  if (glfwMods & GLFW_MOD_CONTROL)   mods |= ailo::ModifierKey::Control;
  if (glfwMods & GLFW_MOD_ALT)       mods |= ailo::ModifierKey::Alt;
  if (glfwMods & GLFW_MOD_SUPER)     mods |= ailo::ModifierKey::Super;
  if (glfwMods & GLFW_MOD_CAPS_LOCK) mods |= ailo::ModifierKey::CapsLock;
  if (glfwMods & GLFW_MOD_NUM_LOCK)  mods |= ailo::ModifierKey::NumLock;

  return mods;
}
