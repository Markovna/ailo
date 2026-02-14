#include "Application.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <stb_image/stb_image.h>

#include "Engine.h"
#include "render/RenderAPI.h"
#include "render/Renderer.h"
#include "render/RenderPrimitive.h"
#include "input/InputTypes.h"
#include "input/InputSystem.h"

#include <memory>
#include <vector>
#include <iostream>

#include "OS.h"
#include "ecs/Transform.h"
#include "render/Mesh.h"
#include "resources/ResourcePtr.h"

// Helper functions for GLFW to platform-agnostic conversion
static ailo::KeyCode glfwKeyToKeyCode(int glfwKey);
static ailo::MouseButton glfwButtonToMouseButton(int glfwButton);
static ailo::ModifierKey glfwModsToModifierKey(int glfwMods);

const uint32_t WIDTH = 2400;
const uint32_t HEIGHT = 1400;

void Application::run() {

  init();
  mainLoop();
  cleanup();
}

std::tuple<ImVec2, ImVec2> ImGui_ImplGlfw_GetWindowSizeAndFramebufferScale(GLFWwindow* window) {
  int w, h;
  int display_w, display_h;
  glfwGetWindowSize(window, &w, &h);
  glfwGetFramebufferSize(window, &display_w, &display_h);
  float fb_scale_x = (w > 0) ? (float)display_w / (float)w : 1.0f;
  float fb_scale_y = (h > 0) ? (float)display_h / (float)h : 1.0f;
#if GLFW_HAS_X11_OR_WAYLAND
  ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData(window);
  if (!bd->IsWayland)
    fb_scale_x = fb_scale_y = 1.0f;
#endif
  return std::make_tuple(ImVec2(w, h), ImVec2(fb_scale_x, fb_scale_y));
}

void Application::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
  auto app = static_cast<Application*>(glfwGetWindowUserPointer(window));
  app->m_engine->getRenderAPI()->handleWindowResize();
}

void Application::init() {
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

  m_engine = std::make_unique<ailo::Engine>(m_window);
  auto* renderAPI = m_engine->getRenderAPI();

  m_scene = m_engine->createScene();

  ImGui::CreateContext();

  m_imguiProcessor = std::make_unique<ailo::ImGuiProcessor>(renderAPI);
  m_imguiProcessor->init();

  m_camera = std::make_unique<ailo::Camera>();

  auto skybox = m_scene->addEntity();
  ailo::Mesh& skyboxMesh = m_scene->addComponent<ailo::Mesh>(skybox, ailo::MeshReader::createCubeMesh(*m_engine));

  auto skyboxShader = m_engine->loadShader(ailo::Shader::getSkyboxShaderDescription());
  auto skyboxMaterial = ailo::make_resource<ailo::Material>(*m_engine, *m_engine, skyboxShader);
  skyboxMesh.primitives[0].setMaterial(skyboxMaterial);

  auto loadCubemapTex = [](ailo::Engine& engine, const std::array<std::string, 6>& path) {
    //bool isHdr = stbi_is_hdr(path[0].c_str());
    constexpr int MAX_MIP_LEVELS = 4;
    std::shared_ptr<ailo::Texture> tex;

    assert(path.size() == 6);
    for (size_t face = 0; face < 6; face++) {
      int texChannels;
      int texWidth, texHeight;
      int desiredChannels = STBI_rgb_alpha;

      float* pixels = stbi_loadf(path[face].c_str(), &texWidth, &texHeight, &texChannels, desiredChannels);
      if (!pixels) {
        std::cerr << "failed to load texture image at '" << path[face] << "'! Reason " << stbi_failure_reason() << std::endl;
        throw std::runtime_error("failed to load texture image!");
      }

      if (!tex) {
        tex = ailo::make_resource<ailo::Texture>(engine, engine, ailo::TextureType::TEXTURE_CUBEMAP, vk::Format::eR32G32B32A32Sfloat, texWidth, texHeight, MAX_MIP_LEVELS);
      }

      tex->updateImage(engine, pixels, texWidth * texHeight * desiredChannels * sizeof(float), texWidth, texHeight, 0, 0, face, 1);

      stbi_image_free(pixels);
    }
    tex->generateMipmaps(engine);
    return tex;
  };

  static auto cubemapTex = loadCubemapTex(
      *m_engine,
      {
        "assets/textures/yokohama/yokohama_posx.jpg",
        "assets/textures/yokohama/yokohama_negx.jpg",
        "assets/textures/yokohama/yokohama_posy.jpg",
        "assets/textures/yokohama/yokohama_negy.jpg",
        "assets/textures/yokohama/yokohama_posz.jpg",
        "assets/textures/yokohama/yokohama_negz.jpg"
      });
  skyboxMaterial->setTexture(0, cubemapTex.get());

  static auto iblIrradiance = loadCubemapTex(
    *m_engine,
    {
      "assets/textures/rogland_clear_night_4k/rogland_clear_night_4k_px.hdr",
      "assets/textures/rogland_clear_night_4k/rogland_clear_night_4k_nx.hdr",
      "assets/textures/rogland_clear_night_4k/rogland_clear_night_4k_py.hdr",
      "assets/textures/rogland_clear_night_4k/rogland_clear_night_4k_ny.hdr",
      "assets/textures/rogland_clear_night_4k/rogland_clear_night_4k_pz.hdr",
      "assets/textures/rogland_clear_night_4k/rogland_clear_night_4k_nz.hdr"
    });

  m_scene->setIblTexture(iblIrradiance);

  auto meshes = ailo::MeshReader::instantiate(*m_engine, *m_scene, "assets/models/sponza/sponza.gltf");
  // auto meshes = reader.read(*m_engine, *m_scene, "assets/models/camera/GAP_CAM_lowpoly_4.fbx");
  // auto meshes = reader.read(*m_engine, *m_scene, "assets/models/helmet/helmet.obj");
}

void Application::mainLoop() {
  while (!glfwWindowShouldClose(m_window)) {
    const auto now = static_cast<float>(glfwGetTime());
    m_deltaTime = now - m_time;
    m_time = now;

    glfwPollEvents();
    m_engine->getInputSystem()->processEvents();
    handleInput();

    drawFrame();
  }
  m_engine->getRenderAPI()->waitIdle();
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
  auto* inputSystem = m_engine->getInputSystem();

  ailo::Event event;
  while (inputSystem->pollEvent(event)) {
    handleImGuiEvent(event);

    // Handle camera rotation on mouse drag
    if (auto mousePressedEvent = std::get_if<ailo::MouseButtonPressedEvent>(&event)) {
      if (mousePressedEvent->button == ailo::MouseButton::Left) {
        if (inputSystem->isKeyPressed(ailo::KeyCode::LeftAlt)) {
          bool controlPressed = inputSystem->isKeyPressed(ailo::KeyCode::LeftControl);
          m_isRotating = controlPressed == false;
          m_isMoving = controlPressed;
          m_lastMouseX = mousePressedEvent->x;
          m_lastMouseY = mousePressedEvent->y;
        }
      }
    }
    else if (auto mouseReleasedEvent = std::get_if<ailo::MouseButtonReleasedEvent>(&event)) {
      if (mouseReleasedEvent->button == ailo::MouseButton::Left) {
        m_isRotating = false;
        m_isMoving = false;
      }
    }
    else if (auto mouseMovedEvent = std::get_if<ailo::MouseMovedEvent>(&event)) {
      if (m_isRotating) {
        double deltaX = mouseMovedEvent->x - m_lastMouseX;
        double deltaY = mouseMovedEvent->y - m_lastMouseY;

        // Update camera rotation
        m_cameraYaw += static_cast<float>(deltaX) * 0.005f;
        m_cameraPitch += static_cast<float>(deltaY) * 0.005f;

        // Clamp pitch to avoid gimbal lock
        m_cameraPitch = glm::clamp(m_cameraPitch, -glm::pi<float>() / 2.0f + 0.1f, glm::pi<float>() / 2.0f - 0.1f);

        m_lastMouseX = mouseMovedEvent->x;
        m_lastMouseY = mouseMovedEvent->y;
      }
      else if (m_isMoving) {
        double deltaX = mouseMovedEvent->x - m_lastMouseX;
        double deltaY = mouseMovedEvent->y - m_lastMouseY;

        // Calculate camera direction vectors
        float camX = m_cameraDistance * cos(m_cameraPitch) * cos(m_cameraYaw);
        float camY = m_cameraDistance * sin(m_cameraPitch);
        float camZ = m_cameraDistance * cos(m_cameraPitch) * sin(m_cameraYaw);

        glm::vec3 cameraPos = m_cameraTarget + glm::vec3(camX, camY, camZ);
        glm::vec3 forward = glm::normalize(m_cameraTarget - cameraPos);
        glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
        glm::vec3 up = glm::normalize(glm::cross(right, forward));

        // Pan speed based on distance from target
        float panSpeed = m_cameraDistance * 0.001f;

        // Move camera target based on mouse delta
        m_cameraTarget -= right * static_cast<float>(deltaX) * panSpeed;
        m_cameraTarget += up * static_cast<float>(deltaY) * panSpeed;

        m_lastMouseX = mouseMovedEvent->x;
        m_lastMouseY = mouseMovedEvent->y;
      }
    }
    // Handle camera zoom on scroll
    else if (auto scrollEvent = std::get_if<ailo::MouseScrolledEvent>(&event)) {
      m_cameraDistance -= static_cast<float>(scrollEvent->yOffset) * 0.5f;
      // Clamp distance to reasonable values
      m_cameraDistance = glm::clamp(m_cameraDistance, 1.0f, 1000.0f);
    }
  }
}

void Application::updateTransforms() {
  // Calculate camera position using spherical coordinates
  float camX = m_cameraDistance * cos(m_cameraPitch) * cos(m_cameraYaw);
  float camY = m_cameraDistance * sin(m_cameraPitch);
  float camZ = m_cameraDistance * cos(m_cameraPitch) * sin(m_cameraYaw);

  glm::vec3 cameraPos(camX, camY, camZ);
  glm::vec3 up(0.0f, 1.0f, 0.0f);

  m_camera->view = glm::lookAt(m_cameraTarget + cameraPos, m_cameraTarget, up);

  m_camera->projection = glm::perspective(glm::radians(70.0f), WIDTH / (float) HEIGHT, 0.1f, 1000.0f);
  m_camera->projection[1][1] *= -1; // Flip Y for Vulkan
}

void Application::drawFrame() {
  updateTransforms();

  ImGuiIO& io = ImGui::GetIO();
  io.DeltaTime = m_deltaTime;

  auto [size, scale] = ImGui_ImplGlfw_GetWindowSizeAndFramebufferScale(m_window);
  io.DisplaySize = size;
  io.DisplayFramebufferScale = scale;

  ImGui::NewFrame();

  ImGui::Begin("Console");

  ImGui::Text("FPS: %f", io.Framerate);

  ImGui::End();

  ImGui::Render();

  auto renderer = m_engine->getRenderer();
  if (!renderer->beginFrame(*m_engine)) {
    return;
  }

  renderer->colorPass(*m_engine, *m_scene, *m_camera);

  drawImGui();

  renderer->endFrame(*m_engine);
}

void Application::drawImGui() {
  m_imguiProcessor->processImGuiCommands(ImGui::GetDrawData(), ImGui::GetIO());
}

void Application::cleanup() {
  m_imguiProcessor.reset();

  m_scene.reset();
  if (m_texture) {
    m_texture->destroy(*m_engine);
  }

  if (m_normalMapTexture) {
    m_normalMapTexture->destroy(*m_engine);
  }

  m_engine.reset();

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
  auto inputSystem = app->m_engine->getInputSystem();

  ailo::KeyCode keyCode = glfwKeyToKeyCode(key);
  ailo::ModifierKey modifiers = glfwModsToModifierKey(mods);

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
  auto* inputSystem = app->m_engine->getInputSystem();

  ailo::MouseButton mouseButton = glfwButtonToMouseButton(button);
  ailo::ModifierKey modifiers = glfwModsToModifierKey(mods);

  double mouseX, mouseY;
  glfwGetCursorPos(window, &mouseX, &mouseY);

  ImGuiIO& io = ImGui::GetIO();
  if (button >= 0 && button < ImGuiMouseButton_COUNT)
    io.AddMouseButtonEvent(button, action == GLFW_PRESS);

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
  auto* inputSystem = app->m_engine->getInputSystem();

  ImGuiIO& io = ImGui::GetIO();
  io.AddMousePosEvent((float)xpos, (float)ypos);

  ailo::MouseMovedEvent event;
  event.x = xpos;
  event.y = ypos;
  inputSystem->pushEvent(event);
}

void Application::scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
  auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
  auto* inputSystem = app->m_engine->getInputSystem();

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