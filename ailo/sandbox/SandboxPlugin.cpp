#include "SandboxPlugin.h"

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include "app/App.h"
#include "assets/Assets.h"
#include "ecs/SceneLighting.h"
#include "ecs/Spawn.h"
#include "physics/PhysicsWorld.h"
#include "render/Model.h"
#include "render/RenderAPI.h"
#include "render/Skybox.h"
#include "render/Texture.h"
#include "ui/ImGuiPlugin.h"

namespace ailo {

namespace {

void setupScene(World& world, AssetManager& assets, RenderAPI& api) {
    Scene& scene = world.scene();

    scene.addComponent<Skybox>(scene.single(), Skybox {
        .cubemap = Texture::loadCubemap(&assets, &api, "assets/textures/yokohama/yokohama.jpg", vk::Format::eR8G8B8A8Srgb),
    });

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

    scene::spawn(scene, assets.load<Model>("assets/models/sponza/sponza.gltf"));
    scene::spawn(scene, assets.load<Model>("assets/models/Roundhouse Kick.fbx"), characterTransform);

    // Physics playground next to the character: a static slab (top face at y = 0) and a few falling cubes.
    scene::spawnCube(scene, assets, api,
        { .position = { 3.0f, -0.1f, 0.0f }, .scale = { 4.0f, 0.2f, 4.0f } },
        MotionType::Static
    );
    for (int i = 0; i < 6; i++) {
        scene::spawnCube(scene, assets, api, {
            .position = { 3.0f + 0.15f * (i % 2), 1.0f + 0.8f * i, 0.1f * (i % 3) },
            .rotation = glm::angleAxis(0.3f * i, glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f))),
            .scale = glm::vec3(0.25f),
        });
    }
}

void drawConsole(World& world, AssetManager& assets, RenderAPI& api, const PhysicsWorld* physics) {
    ImGui::Begin("Console");
    ImGui::Text("FPS: %f", ImGui::GetIO().Framerate);
    if (physics) {
        const PhysicsStats& stats = physics->stats();
        ImGui::Text("Physics: %u bodies (%u active), step %.3f ms", stats.numBodies, stats.numActiveBodies, stats.stepMs);
        if (ImGui::Button("Drop cube")) {
            static int dropped = 0;
            dropped++;
            scene::spawnCube(world.scene(), assets, api, {
                .position = { 3.0f + 0.3f * std::sin(dropped * 1.7f), 4.0f, 0.3f * std::cos(dropped * 1.3f) },
                .rotation = glm::angleAxis(0.7f * dropped, glm::normalize(glm::vec3(1.0f, 0.5f, 0.2f))),
                .scale = glm::vec3(0.25f),
            });
        }
    }
    ImGui::End();
}

}

void SandboxPlugin::build(App& app) {
    app.addPlugin(ImGuiPlugin {});   // the console needs ImGui; no-op if already added

    app.addSystem(Stage::Startup, setupScene, "Sandbox::setupScene");
    app.addSystem(Stage::Update, drawConsole, "Sandbox::drawConsole");
}

}
