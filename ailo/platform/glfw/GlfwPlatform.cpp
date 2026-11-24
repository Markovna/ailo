#include "platform/Platform.h"

#include "input/InputSystem.h"
#include "input/InputTypes.h"

#include <cstdint>
#include <stdexcept>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#else
#define GLFW_EXPOSE_NATIVE_X11
#endif
#include <GLFW/glfw3native.h>

// Xlib.h defines it as a macro.
#undef None

namespace ailo {

struct Platform::Impl {
    InputSystem* input = nullptr;
};

namespace {

GLFWwindow* toGlfw(Platform::WindowHandle handle) {
    return static_cast<GLFWwindow*>(handle);
}

// A template because Platform::Impl is private.
template<typename Impl>
InputSystem* getInput(GLFWwindow* window) {
    return static_cast<Impl*>(glfwGetWindowUserPointer(window))->input;
}

ModifierKey toModifierKey(int glfwMods) {
    ModifierKey mods = ModifierKey::None;

    if (glfwMods & GLFW_MOD_SHIFT)     mods |= ModifierKey::Shift;
    if (glfwMods & GLFW_MOD_CONTROL)   mods |= ModifierKey::Control;
    if (glfwMods & GLFW_MOD_ALT)       mods |= ModifierKey::Alt;
    if (glfwMods & GLFW_MOD_SUPER)     mods |= ModifierKey::Super;
    if (glfwMods & GLFW_MOD_CAPS_LOCK) mods |= ModifierKey::CapsLock;
    if (glfwMods & GLFW_MOD_NUM_LOCK)  mods |= ModifierKey::NumLock;
    return mods;
}

void onKey(InputSystem& input, int key, int action, int mods) {
    const auto keyCode = static_cast<KeyCode>(key);
    const auto modifiers = toModifierKey(mods);

    if (action == GLFW_PRESS) {
        KeyPressedEvent event;
        event.keyCode = keyCode;
        event.modifiers = modifiers;
        input.pushEvent(event);
    } else if (action == GLFW_RELEASE) {
        KeyReleasedEvent event;
        event.keyCode = keyCode;
        event.modifiers = modifiers;
        input.pushEvent(event);
    } else if (action == GLFW_REPEAT) {
        KeyRepeatedEvent event;
        event.keyCode = keyCode;
        event.modifiers = modifiers;
        input.pushEvent(event);
    }
}

void onMouseButton(InputSystem& input, GLFWwindow* window, int button, int action, int mods) {
    const auto mouseButton = static_cast<MouseButton>(button);
    const auto modifiers = toModifierKey(mods);

    double mouseX, mouseY;
    glfwGetCursorPos(window, &mouseX, &mouseY);

    if (action == GLFW_PRESS) {
        MouseButtonPressedEvent event;
        event.button = mouseButton;
        event.modifiers = modifiers;
        event.x = mouseX;
        event.y = mouseY;
        input.pushEvent(event);
    } else if (action == GLFW_RELEASE) {
        MouseButtonReleasedEvent event;
        event.button = mouseButton;
        event.modifiers = modifiers;
        event.x = mouseX;
        event.y = mouseY;
        input.pushEvent(event);
    }
}

}

Platform::Platform() {
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }
    m_impl = std::make_unique<Impl>();
}

Platform::~Platform() {
    glfwTerminate();
}

Platform::WindowHandle Platform::createWindow(const WindowDesc& desc) {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    auto* window = glfwCreateWindow(desc.width, desc.height, desc.title.c_str(), nullptr, nullptr);
    if (!window) {
        throw std::runtime_error("Failed to create window");
    }
    glfwSetWindowUserPointer(window, m_impl.get());

    glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int, int action, int mods) {
        if (auto* input = getInput<Impl>(w)) {
            onKey(*input, key, action, mods);
        }
    });
    glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int button, int action, int mods) {
        if (auto* input = getInput<Impl>(w)) {
            onMouseButton(*input, w, button, action, mods);
        }
    });
    glfwSetCursorPosCallback(window, [](GLFWwindow* w, double x, double y) {
        if (auto* input = getInput<Impl>(w)) {
            MouseMovedEvent event;
            event.x = x;
            event.y = y;
            input->pushEvent(event);
        }
    });
    glfwSetScrollCallback(window, [](GLFWwindow* w, double xOffset, double yOffset) {
        if (auto* input = getInput<Impl>(w)) {
            MouseScrolledEvent event;
            event.xOffset = xOffset;
            event.yOffset = yOffset;
            input->pushEvent(event);
        }
    });

    return window;
}

void Platform::destroyWindow(WindowHandle handle) {
    glfwDestroyWindow(toGlfw(handle));
}

bool Platform::shouldClose(WindowHandle handle) const {
    return glfwWindowShouldClose(toGlfw(handle));
}

Extent Platform::windowSize(WindowHandle handle) const {
    Extent extent;
    glfwGetWindowSize(toGlfw(handle), &extent.width, &extent.height);
    return extent;
}

Extent Platform::framebufferSize(WindowHandle handle) const {
    Extent extent;
    glfwGetFramebufferSize(toGlfw(handle), &extent.width, &extent.height);
    return extent;
}

NativeWindow Platform::nativeWindow(WindowHandle handle) const {
    auto* window = toGlfw(handle);
#if defined(_WIN32)
    return { .window = glfwGetWin32Window(window) };
#elif defined(__APPLE__)
    return { .window = glfwGetCocoaWindow(window) };
#else
    return { .display = glfwGetX11Display(),
             .window = reinterpret_cast<void*>(static_cast<std::uintptr_t>(glfwGetX11Window(window))) };
#endif
}

// A desktop window keeps its native window until it is destroyed.
void Platform::setSurfaceLostCallback(WindowHandle, std::function<void()>) {
}

void Platform::setCursorCaptured(WindowHandle handle, bool captured) {
    auto* window = toGlfw(handle);
    glfwSetInputMode(window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (glfwRawMouseMotionSupported()) {
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, captured ? GLFW_TRUE : GLFW_FALSE);
    }
}

void Platform::pumpEvents(InputSystem& input) {
    m_impl->input = &input;
    glfwPollEvents();
}

void Platform::waitEvents(InputSystem& input) {
    m_impl->input = &input;
    glfwWaitEvents();
}

double Platform::time() const {
    return glfwGetTime();
}

}
