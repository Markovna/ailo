#include "RenderPlugin.h"

#include "Material.h"
#include "Model.h"
#include "RenderAPI.h"
#include "Texture.h"
#include "app/App.h"
#include "assets/Assets.h"
#include "ecs/Camera.h"
#include "ecs/Transform.h"
#include "platform/PlatformPlugin.h"

namespace ailo {

namespace {

void render(Renderer& renderer, World& world, Query<Renderable> renderables, Query<Camera, TransformComponent> cameras,
            const Window& window) {
    ViewProjection viewProjection;
    for (auto&& [entity, camera, transform] : cameras.each()) {
        viewProjection.view = glm::inverse(transform.world().toMatrix());
        if (float aspect = window.aspect(); aspect > 0.0f) {
            viewProjection.projection = camera.projection(aspect);
        }
        break;
    }
    renderer.render(world.scene(), renderables, viewProjection);
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
    assets.registerLoader<Material>(std::make_unique<MaterialLoader>(&api));
    assets.registerLoader<Model>(std::make_unique<ModelImporter>(&api));

    app.insertResource<Renderer>(&api, &assets, settings);

    app.addSystem(Stage::Render, render, "RenderPlugin::render");
    app.addSystem(Stage::Shutdown, shutdown, "RenderPlugin::shutdown");
}

}
