#include "PlatformPlugin.h"

#include "app/App.h"
#include "input/InputSystem.h"

namespace ailo {

Window::Window(Platform& platform, const WindowDesc& desc)
    : m_platform(platform),
      m_handle(platform.createWindow(desc)) {
    platform.setSurfaceLostCallback(m_handle, [this] {
        for (auto& listener : m_surfaceLostListeners) {
            listener();
        }
    });
}

Window::~Window() {
    m_platform.destroyWindow(m_handle);
}

bool Window::shouldClose() const {
    return m_platform.shouldClose(m_handle);
}

Extent Window::size() const {
    return m_platform.windowSize(m_handle);
}

Extent Window::framebufferSize() const {
    return m_platform.framebufferSize(m_handle);
}

float Window::aspect() const {
    auto [width, height] = framebufferSize();
    return height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 0.0f;
}

NativeWindow Window::nativeWindow() const {
    return m_platform.nativeWindow(m_handle);
}

bool Window::isRenderable() const {
    const auto [width, height] = framebufferSize();
    return width > 0 && height > 0 && nativeWindow().window != nullptr;
}

void Window::onSurfaceLost(std::function<void()> listener) {
    m_surfaceLostListeners.push_back(std::move(listener));
}

namespace {

void pumpEvents(Platform& platform, Window& window, InputSystem& input, Time& time, FixedTime& fixed,
                AppControl& control) {
    platform.pumpEvents(input);

    while (!window.isRenderable() && !window.shouldClose()) {
        platform.waitEvents(input);
    }

    const float now = static_cast<float>(platform.time());
    time.delta = now - time.elapsed;
    time.elapsed = now;
    fixed.accumulate(time.delta);

    if (window.shouldClose()) {
        control.requestExit();
    }
}

void processInput(InputSystem& input) {
    input.processEvents();
}

}

void PlatformPlugin::build(App& app) {
    auto& platform = app.insertResource<Platform>();
    app.insertResource<Window>(platform, window);
    app.insertResource<InputSystem>();
    app.insertResource<Time>(Time { .delta = 0.0f, .elapsed = static_cast<float>(platform.time()) });

    app.addSystem(Stage::First, pumpEvents, "PlatformPlugin::pumpEvents");
    app.addSystem(Stage::PreUpdate, processInput, "PlatformPlugin::processInput");
}

}
