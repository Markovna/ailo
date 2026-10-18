#include "RenderPlugin.h"

#include "DefaultAssetFactory.h"
#include "DefaultAssets.h"
#include "IndirectLight.h"
#include "Material.h"
#include "Model.h"
#include "RenderAPI.h"
#include "Skybox.h"
#include "Texture.h"
#include "app/App.h"
#include "assets/AssetPlugin.h"
#include "ecs/Camera.h"
#include "ecs/SceneLighting.h"
#include "ecs/Transform.h"
#include "platform/PlatformPlugin.h"

namespace ailo {

namespace {

void render(Renderer& renderer, World& world, Query<Renderable> renderables, Query<Camera, TransformComponent> cameras,
            const Window& window) {
    ViewInfo viewProjection;
    for (auto&& [entity, camera, transform] : cameras.each()) {
        viewProjection.view = glm::inverse(transform.world().toMatrix());
        viewProjection.nearPlane = camera.nearPlane;
        viewProjection.farPlane = camera.farPlane;
        viewProjection.exposure = camera.exposure;
        if (float aspect = window.aspect(); aspect > 0.0f) {
            viewProjection.projection = camera.projection(aspect);
        }
        viewProjection.skybox = world.scene().tryGet<Skybox>(entity);
        viewProjection.lighting = world.scene().tryGet<SceneLighting>(entity);
        break;
    }
    renderer.render(world.scene(), renderables, viewProjection);
}

}

void RenderPlugin::build(App& app) {
    auto& window = app.resource<Window>();
    auto& server = app.resource<AssetServer>();

    auto& api = app.insertResource<RenderAPI>(window.handle());

    // Inserted after the RenderAPI and in dependency order: storages are destroyed in reverse, dependents first.
    auto& textureStorage = addAssetType<Texture>(app);
    addAssetType<Material>(app);
    auto& meshStorage = addAssetType<Mesh>(app);
    auto& skeletons = addAssetType<Skeleton>(app);
    auto& clips = addAssetType<AnimationClip>(app);
    auto& materialInstances = addAssetType<MaterialInstance>(app);
    addAssetType<Model>(app);
    auto& indirectLights = addAssetType<IndirectLight>(app);

    server.registerLoader<Texture>(std::make_unique<TexturePackageLoader>(&api));
    server.registerLoader<Texture>(std::make_unique<TextureRawLoader>(&api));
    server.registerLoader<Material>(std::make_unique<MaterialLoader>(&api));
    server.registerLoader<IndirectLight>(std::make_unique<IndirectLightLoader>(&api, &textureStorage));
    server.registerLoader<Model>(std::make_unique<ModelLoader>(&api, &server, ModelLoader::Storages {
        .textures = &textureStorage,
        .meshes = &meshStorage,
        .materialInstances = &materialInstances,
        .skeletons = &skeletons,
        .clips = &clips,
    }));

    auto& defaults = app.insertResource<DefaultAssets>();
    textures::createDefaultAssets(defaults, textureStorage, api);
    meshes::createDefaultAssets(defaults, meshStorage, api);

    app.insertResource<Renderer>(&api, server, materialInstances, settings);

    app.addSystem(Stage::Render, render, "RenderPlugin::render");
}

}
