#pragma once

#include "Renderer.h"

namespace ailo {

class App;

// Requires: PlatformPlugin (Window), AssetPlugin (AssetServer).
// Resources: RenderAPI, AssetStorage<T> for Texture, Material, Mesh, Skeleton, AnimationClip, MaterialInstance and Model,
//            DefaultAssets, Renderer. Registers the Texture, Material (.matpack) and Model (ModelImporter) asset loaders.
// Render:   Renderer::render(scene) from the first entity with Camera + TransformComponent (identity view if none).
// Teardown: the World clears the scene, then destroys resources in reverse insertion order: the Renderer and the
//           DefaultAssets (dropping their asset references), the asset storages and finally ~RenderAPI, which waits
//           for the GPU and destroys the device.
//           GPU objects are owned through Unique handles, whose destruction the RenderAPI defers until the GPU is done
//           with them, so they can be released at any point, as long as it is before the RenderAPI is destroyed:
//           own them through resources inserted after the RenderAPI (or release them in a Shutdown system).
struct RenderPlugin {
    RendererSettings settings;

    void build(App& app);
};

}
