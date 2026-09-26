#include "ImGuiPlugin.h"

#include <imgui.h>

#include "app/App.h"
#include "input/InputSystem.h"
#include "platform/PlatformPlugin.h"
#include "render/ImGuiRenderer.h"
#include "render/Renderer.h"

namespace ailo {

namespace {

void subscribeInput(InputSystem& input) {
    input.subscribe<KeyPressedEvent>([](const KeyPressedEvent& e) {
        ImGuiIO& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Ctrl, (e.modifiers & ModifierKey::Control) != ModifierKey::None);
        io.AddKeyEvent(ImGuiMod_Shift, (e.modifiers & ModifierKey::Shift) != ModifierKey::None);
        io.AddKeyEvent(ImGuiMod_Alt, (e.modifiers & ModifierKey::Alt) != ModifierKey::None);
        io.AddKeyEvent(ImGuiMod_Super, (e.modifiers & ModifierKey::Super) != ModifierKey::None);
        //TODO: map ailo keys to imgui keys
    });

    input.subscribe<MouseButtonPressedEvent>([](const MouseButtonPressedEvent& e) {
        ImGui::GetIO().AddMouseButtonEvent(static_cast<int>(e.button), true);
    });

    input.subscribe<MouseButtonReleasedEvent>([](const MouseButtonReleasedEvent& e) {
        ImGui::GetIO().AddMouseButtonEvent(static_cast<int>(e.button), false);
    });

    input.subscribe<MouseMovedEvent>([](const MouseMovedEvent& e) {
        ImGui::GetIO().AddMousePosEvent(static_cast<float>(e.x), static_cast<float>(e.y));
    });

    input.subscribe<MouseScrolledEvent>([](const MouseScrolledEvent& e) {
        ImGui::GetIO().AddMouseWheelEvent(static_cast<float>(e.xOffset), static_cast<float>(e.yOffset));
    });
}

void beginFrame(const Window& window, const Time& time, InputCapture& capture) {
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = time.delta > 0.0f ? time.delta : 1.0f / 60.0f;

    auto size = window.size();
    auto fbSize = window.framebufferSize();
    io.DisplaySize = ImVec2(static_cast<float>(size.width), static_cast<float>(size.height));
    io.DisplayFramebufferScale = ImVec2(
        size.width > 0 ? static_cast<float>(fbSize.width) / static_cast<float>(size.width) : 1.0f,
        size.height > 0 ? static_cast<float>(fbSize.height) / static_cast<float>(size.height) : 1.0f);

    ImGui::NewFrame();

    capture.mouse = io.WantCaptureMouse;
    capture.keyboard = io.WantCaptureKeyboard;
}

// NewFrame asserts that the previous frame was ended. The overlay pass ends it via ImGui::Render(),
// but it doesn't run when the renderer skips a frame. EndFrame() is a no-op if the frame already ended.
void endFrame() {
    ImGui::EndFrame();
}

}

void ImGuiPlugin::build(App& app) {
    // Inserted after the RenderAPI, so the World destroys it before the device, after RenderPlugin's
    // shutdown has waited for the GPU.
    auto& imgui = app.insertResource<ImGuiRenderer>(&app.resource<RenderAPI>());
    app.insertResource<InputCapture>();

    app.resource<Renderer>().addOverlayPass([&imgui] {
        ImGui::Render();
        imgui.processImGuiCommands(ImGui::GetDrawData(), ImGui::GetIO());
    });

    subscribeInput(app.resource<InputSystem>());

    app.addSystem(Stage::PreUpdate, beginFrame, "ImGuiPlugin::beginFrame");
    app.addSystem(Stage::Last, endFrame, "ImGuiPlugin::endFrame");
}

}
