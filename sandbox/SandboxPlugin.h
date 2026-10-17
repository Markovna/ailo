#pragma once

namespace ailo {

class App;

// The demo scene: Sponza + animated character, and an ImGui console.
// Requires: AssetPlugin, RenderPlugin, ImGuiPlugin.
struct SandboxPlugin {
    void build(App& app);
};

}
