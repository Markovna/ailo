#pragma once

namespace ailo {

class App;

// Requires: PlatformPlugin, RenderPlugin.
// Resources: ImGuiRenderer (owns the ImGui context and its GPU objects), InputCapture.
// Input:     forwards mouse buttons, position, wheel and key modifiers from InputSystem to ImGui.
// PreUpdate: starts the ImGui frame (after PlatformPlugin has processed input), updates InputCapture.
// Render:    ImGui::Render + draw, as a Renderer overlay pass.
// Last:      ends the ImGui frame if the renderer skipped it (e.g. swapchain out of date).
//
// UI can be drawn from any system that runs between PreUpdate and Render.
struct ImGuiPlugin {
    void build(App& app);
};

}
