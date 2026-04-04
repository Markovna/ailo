#include "Platform.h"
#include <GLFW/glfw3.h>

namespace ailo {

void Platform::init() {
    glfwInit();
}

void Platform::shutdown() {
    glfwTerminate();
}

Platform::WindowHandle Platform::createWindow(const char* title, int width, int height) {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    auto handle = glfwCreateWindow(width, height, title, nullptr, nullptr);
    glfwSetKeyCallback(handle, keyCallback);
    glfwSetMouseButtonCallback(handle, mouseButtonCallback);
    glfwSetCursorPosCallback(handle, cursorPosCallback);
    glfwSetScrollCallback(handle, scrollCallback);

    return handle;
}

bool Platform::windowShouldClose(WindowHandle window_handle) {
    return glfwWindowShouldClose(static_cast<GLFWwindow*>(window_handle));
}

void Platform::destroyWindow(WindowHandle handle) {
    glfwDestroyWindow(static_cast<GLFWwindow*>(handle));
}

void Platform::pumpEvents(WindowHandle handle, InputSystem* inputSystem) {
    glfwSetWindowUserPointer(static_cast<GLFWwindow*>(handle), inputSystem);

    glfwPollEvents();
}

float Platform::getTime() {
    return static_cast<float>(glfwGetTime());
}

void Platform::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    auto* inputSystem = static_cast<InputSystem*>(glfwGetWindowUserPointer(window));
    auto keyCode = static_cast<KeyCode>(key);
    auto modifiers = glfwModsToModifierKey(mods);

    if (action == GLFW_PRESS) {
        KeyPressedEvent event;
        event.keyCode = keyCode;
        event.modifiers = modifiers;
        inputSystem->pushEvent(event);
    } else if (action == GLFW_RELEASE) {
        KeyReleasedEvent event;
        event.keyCode = keyCode;
        event.modifiers = modifiers;
        inputSystem->pushEvent(event);
    } else if (action == GLFW_REPEAT) {
        KeyRepeatedEvent event;
        event.keyCode = keyCode;
        event.modifiers = modifiers;
        inputSystem->pushEvent(event);
    }
}

void Platform::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    auto* inputSystem = static_cast<InputSystem*>(glfwGetWindowUserPointer(window));
    auto mouseButton = static_cast<MouseButton>(button);
    auto modifiers = glfwModsToModifierKey(mods);

    double mouseX, mouseY;
    glfwGetCursorPos(window, &mouseX, &mouseY);

    if (action == GLFW_PRESS) {
        MouseButtonPressedEvent event;
        event.button = mouseButton;
        event.modifiers = modifiers;
        event.x = mouseX;
        event.y = mouseY;
        inputSystem->pushEvent(event);
    } else if (action == GLFW_RELEASE) {
        MouseButtonReleasedEvent event;
        event.button = mouseButton;
        event.modifiers = modifiers;
        event.x = mouseX;
        event.y = mouseY;
        inputSystem->pushEvent(event);
    }
}

void Platform::cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    auto* inputSystem = static_cast<InputSystem*>(glfwGetWindowUserPointer(window));

    MouseMovedEvent event;
    event.x = xpos;
    event.y = ypos;
    inputSystem->pushEvent(event);
}

void Platform::scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    auto* inputSystem = static_cast<InputSystem*>(glfwGetWindowUserPointer(window));
    MouseScrolledEvent event;
    event.xOffset = xoffset;
    event.yOffset = yoffset;
    inputSystem->pushEvent(event);
}

ModifierKey Platform::glfwModsToModifierKey(int glfwMods) {
    ModifierKey mods = ModifierKey::None;

    if (glfwMods & GLFW_MOD_SHIFT)     mods |= ModifierKey::Shift;
    if (glfwMods & GLFW_MOD_CONTROL)   mods |= ModifierKey::Control;
    if (glfwMods & GLFW_MOD_ALT)       mods |= ModifierKey::Alt;
    if (glfwMods & GLFW_MOD_SUPER)     mods |= ModifierKey::Super;
    if (glfwMods & GLFW_MOD_CAPS_LOCK) mods |= ModifierKey::CapsLock;
    if (glfwMods & GLFW_MOD_NUM_LOCK)  mods |= ModifierKey::NumLock;
    return mods;
}
}
