#include "RenderPlugin.h"

#include "RenderAPI.h"
#include "Texture.h"
#include "app/App.h"
#include "assets/Assets.h"
#include "platform/PlatformPlugin.h"

namespace ailo {

namespace {

void render(Renderer& renderer, World& world, const Camera& camera) {
    renderer.render(world.scene(), camera);
}

void shutdown(World& world, RenderAPI& api, Renderer& renderer, AssetManager& assets) {
    api.waitIdle();

    // Entities hold asset_ptrs and descriptor sets; release them while the renderer is alive.
    world.scene().clear();
    renderer.onSceneDestroyed(world.scene());

    // Releases the renderer's GPU objects and asset references; the Renderer itself is destroyed later with the World.
    renderer.terminate();

    // Frees every asset while the RenderAPI is still alive; anything still referenced is reported as a leak.
    assets.shutdown();

    // The RenderAPI shuts the device down in its destructor, after every resource inserted later is destroyed.
}

}

void RenderPlugin::build(App& app) {
    auto& window = app.resource<Window>();
    auto& assets = app.resource<AssetManager>();

    auto& api = app.insertResource<RenderAPI>(window.handle());
    assets.registerLoader<Texture>(std::make_unique<TextureLoader>(&api));

    auto& renderer = app.insertResource<Renderer>(&api, &assets, settings);
    renderer.onSceneCreated(app.world().scene());

    app.insertResource<Camera>();

    app.addSystem(Stage::Render, render, "RenderPlugin::render");
    app.addSystem(Stage::Shutdown, shutdown, "RenderPlugin::shutdown");
}

}
