#pragma once

#include "GLFW/glfw3.h"
#include "Engine.h"
#include "render/RenderAPI.h"
#include "render/RenderPrimitive.h"
#include "render/Shader.h"
#include "render/ImGuiProcessor.h"
#include "input/InputTypes.h"
#include "ecs/Scene.h"
#include "render/Renderer.h"
#include "render/Texture.h"


class Application {
  public:
    void run();

  private:
    GLFWwindow* m_window = nullptr;
    std::unique_ptr<ailo::Engine> m_engine;
    std::unique_ptr<ailo::Scene> m_scene;
    std::unique_ptr<ailo::ImGuiProcessor> m_imguiProcessor;
    std::unique_ptr<ailo::Camera> m_camera;
    std::unique_ptr<ailo::Texture> m_texture;
    ailo::Entity m_cubeEntity {};

    float m_time = 0;
    float m_deltaTime = 0;

    // Camera control state
    float m_cameraYaw = 0.0f;
    float m_cameraPitch = 0.0f;
    float m_cameraDistance = 10.0f;
    bool m_isRotating = false;
    bool m_isMoving = false;
    glm::vec3 m_cameraTarget = glm::vec3(0.0f, 0.0f, 0.0f);
    double m_lastMouseX = 0.0;
    double m_lastMouseY = 0.0;

    // GLFW callback functions
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

    void init();
    void mainLoop();
    void handleInput();
    void updateTransforms();
    void drawFrame();
    void cleanup();
    void drawImGui();

    void handleImGuiEvent(ailo::Event&);
};
