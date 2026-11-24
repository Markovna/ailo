#pragma once

#include <functional>
#include <memory>
#include <string>

#include "NativeWindow.h"

namespace ailo {

class InputSystem;

struct WindowDesc {
    std::string title = "Ailo";
    int width = 1280;
    int height = 720;
};

struct Extent {
    int width = 0;
    int height = 0;

    bool operator==(const Extent&) const = default;
};

class Platform {
public:
    using WindowHandle = void*;

    Platform();
    ~Platform();

    Platform(const Platform&) = delete;
    Platform& operator=(const Platform&) = delete;

    WindowHandle createWindow(const WindowDesc& desc);
    void destroyWindow(WindowHandle handle);
    bool shouldClose(WindowHandle handle) const;

    Extent windowSize(WindowHandle handle) const;
    Extent framebufferSize(WindowHandle handle) const;
    NativeWindow nativeWindow(WindowHandle handle) const;

    // Called while events are pumped, right before the native window goes away.
    void setSurfaceLostCallback(WindowHandle handle, std::function<void()> callback);

    void setCursorCaptured(WindowHandle handle, bool captured);

    void pumpEvents(InputSystem& input);
    void waitEvents(InputSystem& input);

    // In seconds, monotonic.
    double time() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}
