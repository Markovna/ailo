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

    world.scene().clear();

    renderer.releaseAssets();

    assets.shutdown();
}

}

void RenderPlugin::build(App& app) {
    auto& window = app.resource<Window>();
    auto& assets = app.resource<AssetManager>();

    auto& api = app.insertResource<RenderAPI>(window.handle());
    assets.registerLoader<Texture>(std::make_unique<TextureLoader>(&api));

    app.insertResource<Renderer>(&api, &assets, settings);

    app.insertResource<Camera>();

    app.addSystem(Stage::Render, render, "RenderPlugin::render");
    app.addSystem(Stage::Shutdown, shutdown, "RenderPlugin::shutdown");
}

}
