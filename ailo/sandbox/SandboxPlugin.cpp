#include "SandboxPlugin.h"

#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include "app/App.h"
#include "assets/Assets.h"
#include "ecs/SceneLighting.h"
#include "render/Material.h"
#include "render/Mesh.h"
#include "render/RenderAPI.h"
#include "render/Renderable.h"
#include "render/Shader.h"
#include "render/Texture.h"
#include "ui/ImGuiPlugin.h"

namespace ailo {

namespace {

void setupScene(World& world, AssetManager& assets, RenderAPI& api) {
    Scene& scene = world.scene();

    auto skyboxShader = Shader::load(&assets, &api, Shader::getSkyboxShaderDescription());
    auto skyboxMaterial = Material::create(&assets, &api, skyboxShader);
    auto cubemapTex = Texture::loadCubemap(&assets, &api, "assets/textures/yokohama/yokohama.jpg", vk::Format::eR8G8B8A8Srgb);
    skyboxMaterial->setTexture(0, cubemapTex);

    auto skyboxEntity = scene.addEntity();
    Renderable& skybox = scene.addComponent<Renderable>(skyboxEntity);
    skybox.mesh = Mesh::cube(&assets, &api);
    skybox.materials.push_back(skyboxMaterial);

    auto iblPrefilter = Texture::loadCubemap(
        &assets, &api,
        "assets/textures/rogland_clear_night_4k/rogland_clear_night_4k.hdr",
        vk::Format::eR32G32B32A32Sfloat,
        true);

    auto& sceneLighting = scene.addComponent<SceneLighting>(scene.single());
    sceneLighting.prefilteredEnvMap = iblPrefilter;
    sceneLighting.lightDirection = glm::normalize(glm::vec3(0.1, 1.4, 0.1));

    auto characterTransform = glm::scale(glm::mat4(1.0f), glm::vec3(0.01f));
    characterTransform = glm::rotate(characterTransform, glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    MeshReader::instantiate(&assets, &api, scene, "assets/models/sponza/sponza.gltf");
    MeshReader::instantiate(&assets, &api, scene, "assets/models/Roundhouse Kick.fbx", characterTransform);
}

void drawConsole() {
    ImGui::Begin("Console");
    ImGui::Text("FPS: %f", ImGui::GetIO().Framerate);
    ImGui::End();
}

}

void SandboxPlugin::build(App& app) {
    app.addPlugin(ImGuiPlugin {});   // the console needs ImGui; no-op if already added

    app.addSystem(Stage::Startup, setupScene, "Sandbox::setupScene");
    app.addSystem(Stage::Update, drawConsole, "Sandbox::drawConsole");
}

}
