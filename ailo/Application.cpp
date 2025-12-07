#include "Application.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <stb_image/stb_image.h>

#include "Engine.h"
#include "render/RenderAPI.h"
#include "render/Renderer.h"
#include "render/RenderPrimitive.h"
#include "input/InputTypes.h"
#include "input/InputSystem.h"

#include <iostream>
#include <memory>
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

void Application::run() {
  initWindow();
  initRender();
  mainLoop();
  cleanup();
}

void Application::initWindow() {
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

void Application::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
  auto app = static_cast<Application*>(glfwGetWindowUserPointer(window));
  app->m_engine.getRenderAPI()->handleWindowResize();
}

void Application::initRender() {
  auto* renderAPI = m_engine.getRenderAPI();
  renderAPI->init(m_window, WIDTH, HEIGHT);

  m_scene = std::make_unique<ailo::Scene>();

  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(WIDTH, HEIGHT);
  io.DisplayFramebufferScale.x = 2;
  io.DisplayFramebufferScale.y = 2;

  m_imguiProcessor = std::make_unique<ailo::ImGuiProcessor>(renderAPI);
  m_imguiProcessor->init();

  /*
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
  */

  // Create descriptor set layout for uniform buffer and texture
  ailo::DescriptorSetLayoutBinding perViewLayoutBinding{};
  perViewLayoutBinding.binding = 0;
  perViewLayoutBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
  perViewLayoutBinding.stageFlags = vk::ShaderStageFlagBits::eVertex;

  ailo::DescriptorSetLayoutBinding perObjectLayoutBinding{};
  perObjectLayoutBinding.binding = 0;
  perObjectLayoutBinding.descriptorType = vk::DescriptorType::eUniformBufferDynamic;
  perObjectLayoutBinding.stageFlags = vk::ShaderStageFlagBits::eVertex;

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
      ailo::PipelineDescription {
        .raster = ailo::RasterDescription {
            .cullingMode = ailo::CullingMode::FRONT,
            .inverseFrontFace = false,
            .depthWriteEnable = true,
            .depthCompareOp = ailo::CompareOp::LESS
        },
        .layout = {
            .sets = {
                { perViewLayoutBinding },
                { perObjectLayoutBinding }
            }
        }
      },
      vertexInput
  );

  m_indexBuffer = std::make_unique<ailo::BufferObject>(m_engine, ailo::BufferBinding::INDEX, sizeof(uint16_t) * indices.size());
  m_indexBuffer->updateBuffer(m_engine, indices.data(), sizeof(uint16_t) * indices.size());

  m_vertexBuffer = std::make_unique<ailo::BufferObject>(m_engine, ailo::BufferBinding::VERTEX, sizeof(Vertex) * vertices.size());
  m_vertexBuffer->updateBuffer(m_engine, vertices.data(), sizeof(Vertex) * vertices.size());

  m_cubeEntity = m_scene->getRegistry().create();
  auto& renderPrimitive = m_scene->getRegistry().add<ailo::RenderPrimitive>(m_cubeEntity, m_vertexBuffer.get(), m_indexBuffer.get(), 0, indices.size());
  renderPrimitive.setPipeline(m_pipeline);

  m_camera = std::make_unique<ailo::Camera>();
}

void Application::mainLoop() {
  while (!glfwWindowShouldClose(m_window)) {
    double now = glfwGetTime();
    m_deltaTime = m_time > 0 ? (float) ((double) (now - m_time)) :
                           (float) (1.0f / 60.0f);
    m_time = now;

    glfwPollEvents();
    m_engine.getInputSystem()->processEvents();
    handleInput();

    drawFrame();
    //drawImGui();
  }
  m_engine.getRenderAPI()->waitIdle();
}

void Application::handleImGuiEvent(ailo::Event& event) {
  if(auto keyPressed = std::get_if<ailo::KeyPressedEvent>(&event)) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiKey::ImGuiMod_Ctrl, (keyPressed->modifiers | ailo::ModifierKey::Control) != ailo::ModifierKey::None);
    io.AddKeyEvent(ImGuiKey::ImGuiMod_Shift, (keyPressed->modifiers | ailo::ModifierKey::Shift) != ailo::ModifierKey::None);
    io.AddKeyEvent(ImGuiKey::ImGuiMod_Alt, (keyPressed->modifiers | ailo::ModifierKey::Alt) != ailo::ModifierKey::None);
    io.AddKeyEvent(ImGuiKey::ImGuiMod_Super, (keyPressed->modifiers | ailo::ModifierKey::Super) != ailo::ModifierKey::None);

    //TODO: map ailo keys to imgui keys
    return;
  }
}

void Application::handleInput() {
  auto* inputSystem = m_engine.getInputSystem();
  // Process all input events from the queue
  ailo::Event event;
  while (inputSystem->pollEvent(event)) {
    handleImGuiEvent(event);

    std::visit([this](auto&& e) {
      using T = std::decay_t<decltype(e)>;

      if constexpr (std::is_same_v<T, ailo::KeyPressedEvent>) {
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

void Application::updateUniformBuffer() {

  auto model = glm::rotate(glm::mat4(1.0f), m_time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)) *
      glm::rotate(glm::mat4(1.0f), 1.3f * m_time * glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f)) *
      glm::rotate(glm::mat4(1.0f), glm::radians(45.0f) , glm::vec3(1.0f, 0.0f, 0.0f));

  auto& rp = m_scene->getRegistry().get<ailo::RenderPrimitive>(m_cubeEntity);
  rp.setTransform(model);

  // View matrix: look at the scene from above
  m_camera->view = glm::lookAt(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

  m_camera->projection = glm::perspective(glm::radians(45.0f), WIDTH / (float) HEIGHT, 0.1f, 10.0f);
  m_camera->projection[1][1] *= -1; // Flip Y for Vulkan
}

void Application::drawFrame() {
  updateUniformBuffer();

  auto renderer = m_engine.getRenderer();
  renderer->render(m_engine, *m_scene, *m_camera);

}

void Application::drawImGui() {

  ImGuiIO& io = ImGui::GetIO();
  io.DeltaTime = m_deltaTime;

  ImGui::NewFrame();

  ImGui::ShowDemoWindow();

  ImGui::Render();

  m_imguiProcessor->processImGuiCommands(ImGui::GetDrawData(), ImGui::GetIO());
}

void Application::cleanup() {
  m_imguiProcessor.reset();

  m_vertexBuffer->destroy(m_engine);
  m_indexBuffer->destroy(m_engine);

  auto renderAPI = m_engine.getRenderAPI();
  renderAPI->destroyPipeline(m_pipeline);
  renderAPI->shutdown();

  glfwSetKeyCallback(m_window, nullptr);
  glfwSetFramebufferSizeCallback(m_window, nullptr);
  glfwSetMouseButtonCallback(m_window, nullptr);
  glfwSetCursorPosCallback(m_window, nullptr);
  glfwSetScrollCallback(m_window, nullptr);
  glfwDestroyWindow(m_window);
  glfwTerminate();
}

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


  ImGuiIO& io = ImGui::GetIO();
  if (button >= 0 && button < ImGuiMouseButton_COUNT)
    io.AddMouseButtonEvent(button, action == GLFW_PRESS);

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

  ImGuiIO& io = ImGui::GetIO();
  io.AddMousePosEvent((float)xpos, (float)ypos);

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