#pragma once
#include "GLFW/glfw3.h"
#include "input/InputTypes.h"

namespace ailo {
class InputSystem;

class Platform {
public:
    using WindowHandle = void*;
    // Initializes / terminates the windowing system (GLFW).
    Platform();
    ~Platform();

    Platform(const Platform&) = delete;
    Platform& operator=(const Platform&) = delete;

    WindowHandle createWindow(const char* title, int width, int height);
    bool windowShouldClose(WindowHandle window_handle);
    void destroyWindow(WindowHandle handle);
    void getFramebufferSize(WindowHandle handle, int& width, int& height);
    void getWindowSize(WindowHandle handle, int& width, int& height);

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
