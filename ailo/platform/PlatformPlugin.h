#pragma once

#include <string>

#include "Platform.h"

namespace ailo {

class App;

struct WindowDesc {
    std::string title = "Ailo";
    int width = 1280;
    int height = 720;
};

struct Extent {
    int width = 0;
    int height = 0;
};

// Owns an OS window. Resource inserted by PlatformPlugin.
class Window {
public:
    Window(Platform& platform, const WindowDesc& desc);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    Platform::WindowHandle handle() const { return m_handle; }
    bool shouldClose() const;

    // Size in screen coordinates (what input events report).
    Extent size() const;
    // Size in pixels (what the swapchain renders to).
    Extent framebufferSize() const;
    // Framebuffer aspect ratio, or 0 when minimized.
    float aspect() const;

private:
    Platform& m_platform;
    Platform::WindowHandle m_handle = nullptr;
};

// Frame timing, in seconds. Updated at the start of every frame.
struct Time {
    float delta = 0.0f;
    float elapsed = 0.0f;
};

// Resources: Platform, Window, InputSystem, Time.
// First:     pumps OS events into InputSystem, updates Time, requests exit when the window closes.
// PreUpdate: InputSystem::processEvents (updates input state, notifies subscribers).
struct PlatformPlugin {
    WindowDesc window;

    void build(App& app);
};

}
