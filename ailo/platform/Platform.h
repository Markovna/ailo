#pragma once
#include "input/InputSystem.h"

namespace ailo {

class Platform {
public:
    using WindowHandle = void*;
    Platform() = default;
    ~Platform() = default;

    void init();
    void shutdown();

    WindowHandle createWindow(const char* title, int width, int height);
    bool windowShouldClose(WindowHandle window_handle);
    void destroyWindow(WindowHandle handle);

    void pumpEvents(WindowHandle, InputSystem*);
    float getTime();

private:
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);

    static ModifierKey glfwModsToModifierKey(int glfwMods);
};

}
