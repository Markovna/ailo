#pragma once

#include "GLFW/glfw3.h"
#include "Engine.h"
#include "render/RenderAPI.h"


class Application {
  public:
    void run();

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
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

    void initWindow();
    void initRender();
    void mainLoop();
    void handleInput();
    void updateUniformBuffer();
    void drawFrame();
    void cleanup();
};
