#include "Application.h"

#include <GLFW/glfw3.h>

#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

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
#include "ecs/SceneLighting.h"
#include "ecs/Transform.h"
#include "ecs/AnimatorComponent.h"
#include "render/Mesh.h"
#include "render/Renderable.h"
#include "render/Skin.h"

const uint32_t WIDTH = 2400;
const uint32_t HEIGHT = 1400;

void Application::run() {
  init();
  mainLoop();
  cleanup();
}

std::tuple<ImVec2, ImVec2> ImGui_GetWindowSizeAndFramebufferScale(ailo::Platform::WindowHandle window) {
  int w, h;
  int display_w, display_h;
  auto win = static_cast<GLFWwindow*>(window);
  glfwGetWindowSize(win, &w, &h);
  glfwGetFramebufferSize(win, &display_w, &display_h);
  float fb_scale_x = (w > 0) ? (float)display_w / (float)w : 1.0f;
  float fb_scale_y = (h > 0) ? (float)display_h / (float)h : 1.0f;
#if GLFW_HAS_X11_OR_WAYLAND
  ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData(window);
  if (!bd->IsWayland)
    fb_scale_x = fb_scale_y = 1.0f;
#endif
  return std::make_tuple(ImVec2(w, h), ImVec2(fb_scale_x, fb_scale_y));
}

void Application::init() {
  m_platform = std::make_unique<ailo::Platform>();
  m_platform->init();

  m_window = m_platform->createWindow("Ailo", WIDTH, HEIGHT);

  m_engine = std::make_unique<ailo::Engine>(m_window);
  auto* renderAPI = m_engine->getRenderAPI();

  m_scene = m_engine->createScene();

  ImGui::CreateContext();

  m_imguiProcessor = std::make_unique<ailo::ImGuiProcessor>(renderAPI);
  m_imguiProcessor->init();

  m_camera = std::make_unique<ailo::Camera>();

  auto skyboxShader = ailo::Shader::load(*m_engine, ailo::Shader::getSkyboxShaderDescription());
  auto skyboxMaterial = ailo::Material::create(*m_engine, skyboxShader);
  auto cubemapTex = ailo::Texture::loadCubemap(
      *m_engine,
      "assets/textures/yokohama/yokohama.jpg",
      vk::Format::eR8G8B8A8Srgb);
  skyboxMaterial->setTexture(0, cubemapTex);
  auto skyboxEntity = m_scene->addEntity();
  ailo::Renderable& skybox = m_scene->addComponent<ailo::Renderable>(skyboxEntity);
  skybox.mesh = ailo::Mesh::cube(*m_engine);
  skybox.materials.push_back(skyboxMaterial);

  auto iblPrefilter = ailo::Texture::loadCubemap(
    *m_engine,
    "assets/textures/rogland_clear_night_4k/rogland_clear_night_4k.hdr",
    vk::Format::eR32G32B32A32Sfloat,
    true
  );

  auto& sceneLighting = m_scene->addComponent<ailo::SceneLighting>(m_scene->single());
  sceneLighting.prefilteredEnvMap = iblPrefilter;
  sceneLighting.lightDirection = normalize(glm::vec3(0.1, 1.4, 0.1));

  auto scale = glm::scale(glm::mat4(1.0f), glm::vec3(0.01f));
  // scale = glm::translate(scale, glm::vec3(200.0f, 0.0f, 0.0f));
  scale = glm::rotate(scale, glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));

  ailo::MeshReader::instantiate(*m_engine, *m_scene, "assets/models/sponza/sponza.gltf");
  ailo::MeshReader::instantiate(*m_engine, *m_scene, "assets/models/Roundhouse Kick.fbx", scale);
}

void Application::mainLoop() {
  while (!m_platform->windowShouldClose(m_window)) {
    const auto now = m_platform->getTime();
    m_deltaTime = now - m_time;
    m_time = now;

    m_platform->pumpEvents(m_window, m_engine->getInputSystem());
    m_engine->getInputSystem()->processEvents();
    handleInput();

    drawFrame();

    m_engine->gc();
  }
  m_engine->getRenderAPI()->waitIdle();
}

void Application::handleImGuiEvent(ailo::Event& event) {
  if(auto keyPressed = event.as<ailo::KeyPressedEvent>()) {
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
    if (auto mousePressed = event.as<ailo::MouseButtonPressedEvent>()) {
      ImGuiIO& io = ImGui::GetIO();
      io.AddMouseButtonEvent(static_cast<int>(mousePressed->button), true);

      if (mousePressed->button == ailo::MouseButton::Left) {
        if (inputSystem->isKeyPressed(ailo::KeyCode::LeftAlt)) {
          bool controlPressed = inputSystem->isKeyPressed(ailo::KeyCode::LeftControl);
          m_isRotating = controlPressed == false;
          m_isMoving = controlPressed;
          m_lastMouseX = mousePressed->x;
          m_lastMouseY = mousePressed->y;
        }
      }
    }
    else if (auto mouseReleased = event.as<ailo::MouseButtonReleasedEvent>()) {
      if (mouseReleased->button == ailo::MouseButton::Left) {
        m_isRotating = false;
        m_isMoving = false;
      }

      ImGuiIO& io = ImGui::GetIO();
      io.AddMouseButtonEvent(static_cast<int>(mouseReleased->button), false);
    }
    else if (auto mouseMoved = event.as<ailo::MouseMovedEvent>()) {
      ImGuiIO& io = ImGui::GetIO();
      io.AddMousePosEvent(static_cast<float>(mouseMoved->x), static_cast<float>(mouseMoved->y));

      if (m_isRotating) {
        double deltaX = mouseMoved->x - m_lastMouseX;
        double deltaY = mouseMoved->y - m_lastMouseY;

        // Update camera rotation
        m_cameraYaw += static_cast<float>(deltaX) * 0.005f;
        m_cameraPitch += static_cast<float>(deltaY) * 0.005f;

        // Clamp pitch to avoid gimbal lock
        m_cameraPitch = glm::clamp(m_cameraPitch, -glm::pi<float>() / 2.0f + 0.1f, glm::pi<float>() / 2.0f - 0.1f);

        m_lastMouseX = mouseMoved->x;
        m_lastMouseY = mouseMoved->y;
      }
      else if (m_isMoving) {
        double deltaX = mouseMoved->x - m_lastMouseX;
        double deltaY = mouseMoved->y - m_lastMouseY;

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

        m_lastMouseX = mouseMoved->x;
        m_lastMouseY = mouseMoved->y;
      }
    }
    // Handle camera zoom on scroll
    else if (auto scroll = event.as<ailo::MouseScrolledEvent>()) {
      m_cameraDistance -= static_cast<float>(scroll->yOffset) * 0.5f;
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

  auto fov = glm::radians(60.0f);
  m_camera->projection = glm::perspective(fov, WIDTH / (float) HEIGHT, 0.1f, 1000.0f);
  m_camera->projection[1][1] *= -1; // Flip Y for Vulkan
}

void Application::drawFrame() {
  updateTransforms();

  ImGuiIO& io = ImGui::GetIO();
  io.DeltaTime = m_deltaTime;

  auto [size, scale] = ImGui_GetWindowSizeAndFramebufferScale(m_window);
  io.DisplaySize = size;
  io.DisplayFramebufferScale = scale;

  auto renderer = m_engine->getRenderer();

  ImGui::NewFrame();

  ImGui::Begin("Console");

  ImGui::Text("FPS: %f", io.Framerate);

  ImGui::End();

  ImGui::Render();

  ailo::BonesUniform bonesData{};
  // Advance and apply skeletal animations
  for (auto [entity, animator] : m_scene->view<ailo::AnimatorComponent>().each()) {
    if (animator.playing && !animator.clips.empty()) {
      auto& clip = animator.clips[animator.currentClip];
      animator.currentTime += m_deltaTime;
      if (animator.looping)
        animator.currentTime = std::fmod(animator.currentTime, clip.duration);

      animator.skeleton->updateBoneTransforms(animator.currentTime, clip, bonesData);
      animator.boneBuffer->updateBuffer(*m_engine, &bonesData, sizeof(bonesData));
    }
  }

  if (!renderer->beginFrame(*m_engine)) {
    return;
  }

  renderer->shadowPass(*m_engine, *m_scene);
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

  m_engine.reset();

  m_platform->destroyWindow(m_window);
  m_platform.reset();
}