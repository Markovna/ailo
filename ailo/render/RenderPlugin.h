#pragma once

#include "Renderer.h"

namespace ailo {

class App;

// Requires: PlatformPlugin (Window), AssetPlugin (AssetServer).
// Resources: RenderAPI, AssetStorage<T> for Texture, Material, Mesh, Skeleton, AnimationClip, MaterialInstance and Model,
//            Renderer. Registers the Texture, Material (.matpack) and Model (ModelImporter) asset loaders.
// Render:   Renderer::render(scene) from the first entity with Camera + TransformComponent (identity view if none).
// Shutdown: waits for the GPU, clears the scene and releases the renderer's asset references, which frees the assets.
//           The device itself is destroyed by ~RenderAPI when the World tears down resources (reverse insertion order),
//           i.e. after resources inserted later (Renderer, plugin-owned GPU objects) are destroyed.
//           GPU objects are owned through Unique handles, whose destruction the RenderAPI defers until the GPU is done
//           with them, so they can be released at any point, as long as it is before the RenderAPI is destroyed:
//           own them through resources inserted after the RenderAPI (or release them in a Shutdown system).
struct RenderPlugin {
    RendererSettings settings;

    void build(App& app);
};

}
