#include "SandboxPlugin.h"

#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include "app/App.h"
#include "assets/Assets.h"
#include "ecs/AnimatorComponent.h"
#include "ecs/SceneLighting.h"
#include "input/InputSystem.h"
#include "platform/PlatformPlugin.h"
#include "render/ImGuiRenderer.h"
#include "render/Material.h"
#include "render/Mesh.h"
#include "render/RenderAPI.h"
#include "render/Renderable.h"
#include "render/Renderer.h"
#include "render/Shader.h"
#include "render/Texture.h"

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

void beginImGuiFrame(const Window& window, const Time& time) {
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = time.delta > 0.0f ? time.delta : 1.0f / 60.0f;

    auto size = window.size();
    auto fbSize = window.framebufferSize();
    io.DisplaySize = ImVec2(static_cast<float>(size.width), static_cast<float>(size.height));
    io.DisplayFramebufferScale = ImVec2(
        size.width > 0 ? static_cast<float>(fbSize.width) / static_cast<float>(size.width) : 1.0f,
        size.height > 0 ? static_cast<float>(fbSize.height) / static_cast<float>(size.height) : 1.0f);

    ImGui::NewFrame();
}

void drawConsole() {
    ImGui::Begin("Console");
    ImGui::Text("FPS: %f", ImGui::GetIO().Framerate);
    ImGui::End();
}

void endImGuiFrame() {
    ImGui::Render();
}

void animate(Query<AnimatorComponent> animators, const Time& time, RenderAPI& api) {
    BonesUniform bonesData {};
    for (auto [entity, animator] : animators.each()) {
        if (!animator.playing || animator.clips.empty()) {
            continue;
        }

        auto& clip = animator.clips[animator.currentClip];
        animator.currentTime += time.delta;
        if (animator.looping) {
            animator.currentTime = std::fmod(animator.currentTime, clip.duration);
        }

        animator.skeleton->updateBoneTransforms(animator.currentTime, clip, bonesData);
        animator.boneBuffer->updateBuffer(&api, &bonesData, sizeof(bonesData));
    }
}

}

void SandboxPlugin::build(App& app) {
    auto& api = app.resource<RenderAPI>();
    auto& input = app.resource<InputSystem>();

    // ImGui. Inserted after the RenderAPI, so the World destroys it (and the ImGui context) first,
    // after RenderPlugin's shutdown has waited for the GPU.
    auto& imgui = app.insertResource<ImGuiRenderer>(&api);
    app.resource<Renderer>().addOverlayPass([&imgui] {
        imgui.processImGuiCommands(ImGui::GetDrawData(), ImGui::GetIO());
    });

    input.subscribe<KeyPressedEvent>([](const KeyPressedEvent& e) {
        ImGuiIO& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Ctrl, (e.modifiers & ModifierKey::Control) != ModifierKey::None);
        io.AddKeyEvent(ImGuiMod_Shift, (e.modifiers & ModifierKey::Shift) != ModifierKey::None);
        io.AddKeyEvent(ImGuiMod_Alt, (e.modifiers & ModifierKey::Alt) != ModifierKey::None);
        io.AddKeyEvent(ImGuiMod_Super, (e.modifiers & ModifierKey::Super) != ModifierKey::None);
        //TODO: map ailo keys to imgui keys
    });

    // Orbit camera input. The plugin outlives all systems and subscriptions, so capturing `this` is safe.
    input.subscribe<MouseButtonPressedEvent>([this, &input](const MouseButtonPressedEvent& e) { onMouseButtonPressed(e, input); });
    input.subscribe<MouseButtonReleasedEvent>([this](const MouseButtonReleasedEvent& e) { onMouseButtonReleased(e); });
    input.subscribe<MouseMovedEvent>([this](const MouseMovedEvent& e) { onMouseMoved(e); });
    input.subscribe<MouseScrolledEvent>([this](const MouseScrolledEvent& e) { onMouseScrolled(e); });

    app.addSystem(Stage::Startup, setupScene, "Sandbox::setupScene");

    app.addSystem(Stage::Update, beginImGuiFrame, "Sandbox::beginImGuiFrame");
    app.addSystem(Stage::Update, [this](Camera& camera, const Window& window) {
        glm::vec3 offset = m_cameraDistance * glm::vec3(
            std::cos(m_cameraPitch) * std::cos(m_cameraYaw),
            std::sin(m_cameraPitch),
            std::cos(m_cameraPitch) * std::sin(m_cameraYaw));
        camera.view = glm::lookAt(m_cameraTarget + offset, m_cameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));

        if (float aspect = window.aspect(); aspect > 0.0f) {
            camera.projection = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 1000.0f);
            camera.projection[1][1] *= -1; // Flip Y for Vulkan
        }
    }, "Sandbox::updateCamera");
    app.addSystem(Stage::Update, drawConsole, "Sandbox::drawConsole");

    app.addSystem(Stage::PostUpdate, endImGuiFrame, "Sandbox::endImGuiFrame");
    app.addSystem(Stage::PostUpdate, animate, "Sandbox::animate");
}

void SandboxPlugin::onMouseButtonPressed(const MouseButtonPressedEvent& e, const InputSystem& input) {
    ImGui::GetIO().AddMouseButtonEvent(static_cast<int>(e.button), true);

    if (e.button == MouseButton::Left && input.isKeyPressed(KeyCode::LeftAlt)) {
        bool controlPressed = input.isKeyPressed(KeyCode::LeftControl);
        m_isRotating = !controlPressed;
        m_isMoving = controlPressed;
        m_lastMouseX = e.x;
        m_lastMouseY = e.y;
    }
}

void SandboxPlugin::onMouseButtonReleased(const MouseButtonReleasedEvent& e) {
    if (e.button == MouseButton::Left) {
        m_isRotating = false;
        m_isMoving = false;
    }

    ImGui::GetIO().AddMouseButtonEvent(static_cast<int>(e.button), false);
}

void SandboxPlugin::onMouseMoved(const MouseMovedEvent& e) {
    ImGui::GetIO().AddMousePosEvent(static_cast<float>(e.x), static_cast<float>(e.y));

    const double deltaX = e.x - m_lastMouseX;
    const double deltaY = e.y - m_lastMouseY;

    if (m_isRotating) {
        m_cameraYaw += static_cast<float>(deltaX) * 0.005f;
        m_cameraPitch += static_cast<float>(deltaY) * 0.005f;
        // Clamp pitch to avoid gimbal lock
        m_cameraPitch = glm::clamp(m_cameraPitch, -glm::half_pi<float>() + 0.1f, glm::half_pi<float>() - 0.1f);
    } else if (m_isMoving) {
        glm::vec3 cameraPos = m_cameraTarget + m_cameraDistance * glm::vec3(
            std::cos(m_cameraPitch) * std::cos(m_cameraYaw),
            std::sin(m_cameraPitch),
            std::cos(m_cameraPitch) * std::sin(m_cameraYaw));
        glm::vec3 forward = glm::normalize(m_cameraTarget - cameraPos);
        glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
        glm::vec3 up = glm::normalize(glm::cross(right, forward));

        // Pan speed based on distance from target
        float panSpeed = m_cameraDistance * 0.001f;
        m_cameraTarget -= right * static_cast<float>(deltaX) * panSpeed;
        m_cameraTarget += up * static_cast<float>(deltaY) * panSpeed;
    } else {
        return;
    }

    m_lastMouseX = e.x;
    m_lastMouseY = e.y;
}

void SandboxPlugin::onMouseScrolled(const MouseScrolledEvent& e) {
    m_cameraDistance = glm::clamp(m_cameraDistance - static_cast<float>(e.yOffset) * 0.5f, 1.0f, 1000.0f);
}

}
