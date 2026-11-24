#pragma once

#include <functional>
#include <vector>

#include "Platform.h"

namespace ailo {

class App;

class Window {
public:
    Window(Platform& platform, const WindowDesc& desc);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    Platform::WindowHandle handle() const { return m_handle; }
    bool shouldClose() const;

    Extent size() const;
    Extent framebufferSize() const;
    float aspect() const;

    NativeWindow nativeWindow() const;
    bool isRenderable() const;

    void onSurfaceLost(std::function<void()> listener);

private:
    Platform& m_platform;
    Platform::WindowHandle m_handle = nullptr;
    std::vector<std::function<void()>> m_surfaceLostListeners;
};

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
