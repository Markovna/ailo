#pragma once

#include "GLFW/glfw3.h"
#include "Engine.h"
#include "render/RenderAPI.h"
#include "render/RenderPrimitive.h"
#include "render/ImGuiProcessor.h"
#include "input/InputTypes.h"
#include "ecs/Scene.h"
#include "render/Renderer.h"

class Application {
  public:
    void run();

  private:
    GLFWwindow* m_window = nullptr;
    std::unique_ptr<ailo::Engine> m_engine;
    std::unique_ptr<ailo::Scene> m_scene;
    std::unique_ptr<ailo::ImGuiProcessor> m_imguiProcessor;
    std::unique_ptr<ailo::BufferObject> m_vertexBuffer;
    std::unique_ptr<ailo::BufferObject> m_indexBuffer;
    std::unique_ptr<ailo::Camera> m_camera;
    ailo::PipelineHandle m_pipeline;
    ailo::Entity m_cubeEntity;
//    ailo::TextureHandle m_texture;
    float m_time;
    float m_deltaTime;

    // GLFW callback functions
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

    void init();
    void mainLoop();
    void handleInput();
    void updateUniformBuffer();
    void drawFrame();
    void cleanup();
    void drawImGui();

    void handleImGuiEvent(ailo::Event&);
};
