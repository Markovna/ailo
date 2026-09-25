#pragma once

#include "Renderer.h"

namespace ailo {

class App;

// Requires: PlatformPlugin (Window), AssetPlugin (AssetManager).
// Resources: RenderAPI, Renderer, Camera. Registers the Texture asset loader.
// Render:   Renderer::render(scene, camera).
// Shutdown: waits for the GPU, clears the scene, releases the renderer's GPU objects and frees all assets.
//           The device itself is destroyed by ~RenderAPI when the World tears down resources (reverse insertion order),
//           i.e. after resources inserted later (Renderer, plugin-owned GPU objects) are destroyed.
//           Plugins should own GPU objects through resources inserted after the RenderAPI, so the World destroys
//           them after this shutdown (GPU idle) and before the device. Releasing GPU objects in a Shutdown system
//           instead requires RenderAPI::waitIdle() first, since plugins added later shut down before this one.
struct RenderPlugin {
    RendererSettings settings;

    void build(App& app);
};

}
