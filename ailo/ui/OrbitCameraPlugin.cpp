#include "OrbitCameraPlugin.h"

#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "app/App.h"
#include "ecs/Hierarchy.h"
#include "input/InputSystem.h"
#include "platform/PlatformPlugin.h"

namespace ailo {

glm::vec3 OrbitCamera::position() const {
    return target + distance * glm::vec3(
        std::cos(pitch) * std::cos(yaw),
        std::sin(pitch),
        std::cos(pitch) * std::sin(yaw));
}

namespace {

enum class DragMode { None, Rotate, Pan };

// Input gathered by the event callbacks since the last update; applied to the orbit cameras in updateCameras.
struct OrbitCameraInput {
    DragMode mode = DragMode::None;
    double lastX = 0.0;
    double lastY = 0.0;

    glm::vec2 rotateDelta { 0.0f };
    glm::vec2 panDelta { 0.0f };
    float scroll = 0.0f;
};

bool mouseCaptured(World& world) {
    auto* capture = world.tryResource<InputCapture>();
    return capture && capture->mouse;
}

void subscribeInput(World& world, InputSystem& input, OrbitCameraInput& state) {
    input.subscribe<MouseButtonPressedEvent>([&world, &input, &state](const MouseButtonPressedEvent& e) {
        if (e.button != MouseButton::Left || !input.isKeyPressed(KeyCode::LeftAlt) || mouseCaptured(world)) {
            return;
        }
        state.mode = input.isKeyPressed(KeyCode::LeftControl) ? DragMode::Pan : DragMode::Rotate;
        state.lastX = e.x;
        state.lastY = e.y;
    });

    input.subscribe<MouseButtonReleasedEvent>([&state](const MouseButtonReleasedEvent& e) {
        if (e.button == MouseButton::Left) {
            state.mode = DragMode::None;
        }
    });

    input.subscribe<MouseMovedEvent>([&state](const MouseMovedEvent& e) {
        if (state.mode == DragMode::None) {
            return;
        }

        const glm::vec2 delta(static_cast<float>(e.x - state.lastX), static_cast<float>(e.y - state.lastY));
        state.lastX = e.x;
        state.lastY = e.y;

        (state.mode == DragMode::Rotate ? state.rotateDelta : state.panDelta) += delta;
    });

    input.subscribe<MouseScrolledEvent>([&world, &state](const MouseScrolledEvent& e) {
        if (mouseCaptured(world)) {
            return;
        }
        state.scroll += static_cast<float>(e.yOffset);
    });
}

void applyInput(OrbitCamera& orbit, const OrbitCameraInput& input) {
    orbit.yaw += input.rotateDelta.x * orbit.rotateSpeed;
    // Clamp pitch to avoid flipping over the poles
    orbit.pitch = glm::clamp(orbit.pitch + input.rotateDelta.y * orbit.rotateSpeed,
        -glm::half_pi<float>() + 0.1f, glm::half_pi<float>() - 0.1f);

    if (input.panDelta != glm::vec2(0.0f)) {
        glm::vec3 forward = glm::normalize(orbit.target - orbit.position());
        glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
        glm::vec3 up = glm::normalize(glm::cross(right, forward));

        float panSpeed = orbit.distance * orbit.panSpeed;
        orbit.target -= right * input.panDelta.x * panSpeed;
        orbit.target += up * input.panDelta.y * panSpeed;
    }

    orbit.distance = glm::clamp(orbit.distance - input.scroll * orbit.zoomSpeed, orbit.minDistance, orbit.maxDistance);
}

void updateCameras(OrbitCameraInput& input, World& world, Query<OrbitCamera, TransformComponent> cameras) {
    auto& registry = world.scene().registry();

    for (auto&& [entity, orbit, transform] : cameras.each()) {
        applyInput(orbit, input);

        const glm::vec3 position = orbit.position();
        hierarchy::setWorld(registry, entity, Transform {
            .position = position,
            .rotation = glm::quatLookAt(glm::normalize(orbit.target - position), glm::vec3(0.0f, 1.0f, 0.0f)),
        });
    }

    input.rotateDelta = glm::vec2(0.0f);
    input.panDelta = glm::vec2(0.0f);
    input.scroll = 0.0f;
}

}

void OrbitCameraPlugin::build(App& app) {
    auto& input = app.insertResource<OrbitCameraInput>();

    subscribeInput(app.world(), app.resource<InputSystem>(), input);

    app.addSystem(Stage::Update, updateCameras, "OrbitCameraPlugin::updateCameras");
}

}
