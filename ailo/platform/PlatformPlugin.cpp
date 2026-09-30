#include "PlatformPlugin.h"

#include "app/App.h"
#include "input/InputSystem.h"

namespace ailo {

Window::Window(Platform& platform, const WindowDesc& desc)
    : m_platform(platform),
      m_handle(platform.createWindow(desc.title.c_str(), desc.width, desc.height)) {
}

Window::~Window() {
    m_platform.destroyWindow(m_handle);
}

bool Window::shouldClose() const {
    return m_platform.windowShouldClose(m_handle);
}

Extent Window::size() const {
    Extent extent;
    m_platform.getWindowSize(m_handle, extent.width, extent.height);
    return extent;
}

Extent Window::framebufferSize() const {
    Extent extent;
    m_platform.getFramebufferSize(m_handle, extent.width, extent.height);
    return extent;
}

float Window::aspect() const {
    auto [width, height] = framebufferSize();
    return height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 0.0f;
}

namespace {

void pumpEvents(Platform& platform, Window& window, InputSystem& input, Time& time, FixedTime& fixed, AppControl& control) {
    platform.pumpEvents(window.handle(), &input);

    while (window.aspect() == 0.0f && !window.shouldClose()) {
        platform.waitEvents(window.handle(), &input);
    }

    const float now = platform.getTime();
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
    app.insertResource<Time>(Time { .delta = 0.0f, .elapsed = platform.getTime() });

    app.addSystem(Stage::First, pumpEvents, "PlatformPlugin::pumpEvents");
    app.addSystem(Stage::PreUpdate, processInput, "PlatformPlugin::processInput");
}

}
