#include "SandboxPlugin.h"

#include <cmath>
#include <unordered_map>

#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include "app/App.h"
#include "assets/AssetServer.h"
#include "ecs/Camera.h"
#include "ecs/Lights.h"
#include "ecs/SceneLighting.h"
#include "ecs/Spawn.h"
#include "physics/PhysicsWorld.h"
#include "render/MaterialInstance.h"
#include "render/Model.h"
#include "render/RenderAPI.h"
#include "render/Renderable.h"
#include "render/Skybox.h"
#include "render/Texture.h"
#include "ui/ImGuiPlugin.h"
#include "ui/OrbitCameraPlugin.h"

namespace ailo {

namespace {

constexpr auto kToonMatpack = "assets/materials/toon.matpack";

// Replaces every renderable's material instances with instances of `material`, keeping the values of the
// parameters both materials share (e.g. the imported base color textures). Shared instances stay shared.
void useMaterial(Scene& scene, RenderAPI& api, AssetStorage<MaterialInstance>& materialInstances, AssetServer& server,
                 const AssetPtr<Material>& material) {
    std::unordered_map<const MaterialInstance*, AssetPtr<MaterialInstance>> replacements;
    for (auto&& [entity, renderable] : scene.view<Renderable>().each()) {
        for (auto& instance : renderable.materials) {
            if (&instance->getMaterial() == material.get()) continue;

            auto& replacement = replacements[instance.get()];
            if (!replacement) {
                replacement = MaterialInstance::create(materialInstances, server, api, material);
                replacement->copyParametersFrom(*instance);
            }
            instance = replacement;
        }
    }
}

void setupScene(Scene& scene, AssetServer& server, AssetStorage<Texture>& textures, AssetStorage<Mesh>& meshes,
                AssetStorage<MaterialInstance>& materialInstances, RenderAPI& api) {
    auto iblPrefilter = Texture::loadCubemap(
        textures, &api,
        "assets/textures/rogland_clear_night_4k/rogland_clear_night_4k.hdr",
        vk::Format::eR32G32B32A32Sfloat,
        true);

    scene::spawn(scene,
        TransformComponent { },
        Camera { },
        OrbitCamera { .distance = 10.0f },
        Skybox {
            .cubemap = server.load<Texture>("assets/textures/yokohama/yokohama.tex"),
        },
        SceneLighting {
            .prefilteredEnvMap = iblPrefilter,
            .lightDirection = glm::normalize(glm::vec3(0.1, 1.4, 0.1)),
        });

    auto pointLight = scene.addEntity();
    scene.addComponent<TransformComponent>(pointLight, Transform { .position = { 3.0f, 1.5f, 0.5f } });
    scene.addComponent<PointLight>(pointLight, PointLight { .color = { 1.0f, 1.0f, 0.0f } });

    auto spotLight = scene.addEntity();
    scene.addComponent<TransformComponent>(spotLight, Transform {
        .position = { 0.0f, 1.5f, 2.5f },
        .rotation = glm::quatLookAt(glm::normalize(glm::vec3(0.0f, 1.0f, 0.5f)), glm::vec3(0.0f, 1.0f, 0.0f)),
    });
    scene.addComponent<SpotLight>(spotLight, SpotLight {
        .color = { 1.0f, 0.0f, 0.0f },
        .innerAngle = glm::radians(42.0f),
        .outerAngle = glm::radians(66.0f),
    });

    auto characterTransform =glm::scale(glm::mat4(1.0f), glm::vec3(0.01f));
    characterTransform = glm::rotate(characterTransform, glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    scene::spawnPrefab(scene, server.load<Model>("assets/models/sponza/sponza.gltf"));
    scene::spawnPrefab(scene, server.load<Model>("assets/models/Roundhouse Kick.fbx"), characterTransform);

    // Physics playground next to the character: a static slab (top face at y = 0) and a few falling cubes.
    scene::spawnCube(scene, api, materialInstances, server,
        { .position = { 3.0f, -0.1f, 0.0f }, .scale = { 4.0f, 0.2f, 4.0f } },
        MotionType::Static
    );
    for (int i = 0; i < 6; i++) {
        scene::spawnCube(scene, api, materialInstances, server, {
            .position = { 3.0f + 0.15f * (i % 2), 1.0f + 0.8f * i, 0.1f * (i % 3) },
            .rotation = glm::angleAxis(0.3f * i, glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f))),
            .scale = glm::vec3(0.25f),
        }, MotionType::Dynamic);
    }

    // useMaterial(scene, api, materialInstances, server, server.load<Material>(kToonMatpack));
}

void drawConsole(Scene& scene, AssetServer& server, AssetStorage<MaterialInstance>& materialInstances,
                 RenderAPI& api, const PhysicsWorld* physics) {
    ImGui::Begin("Console");
    ImGui::Text("FPS: %f", ImGui::GetIO().Framerate);
    if (physics) {
        const PhysicsStats& stats = physics->stats();
        ImGui::Text("Physics: %u bodies (%u active), step %.3f ms", stats.numBodies, stats.numActiveBodies, stats.stepMs);
        if (ImGui::Button("Drop cube")) {
            static int dropped = 0;
            dropped++;
            scene::spawnCube(scene, api, materialInstances, server, {
                .position = { 3.0f + 0.3f * std::sin(dropped * 1.7f), 4.0f, 0.3f * std::cos(dropped * 1.3f) },
                .rotation = glm::angleAxis(0.7f * dropped, glm::normalize(glm::vec3(1.0f, 0.5f, 0.2f))),
                .scale = glm::vec3(0.25f),
            }, MotionType::Dynamic);
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
