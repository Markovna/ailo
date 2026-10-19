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

enum class DragMode { None, Rotate, Pan, Look };

// Input gathered by the event callbacks since the last update; applied to the orbit cameras in updateCameras.
struct OrbitCameraInput {
    DragMode mode = DragMode::None;
    double lastX = 0.0;
    double lastY = 0.0;

    glm::vec2 rotateDelta { 0.0f };
    glm::vec2 panDelta { 0.0f };
    glm::vec2 lookDelta { 0.0f };
    float scroll = 0.0f;
    float flyScroll = 0.0f;
};

bool mouseCaptured(World& world) {
    auto* capture = world.tryResource<InputCapture>();
    return capture && capture->mouse;
}

bool keyboardCaptured(World& world) {
    auto* capture = world.tryResource<InputCapture>();
    return capture && capture->keyboard;
}

void subscribeInput(World& world, InputSystem& input, OrbitCameraInput& state) {
    input.subscribe<MouseButtonPressedEvent>([&world, &input, &state](const MouseButtonPressedEvent& e) {
        if (state.mode != DragMode::None || mouseCaptured(world)) {
            return;
        }
        if (e.button == MouseButton::Left && input.isKeyPressed(KeyCode::LeftAlt)) {
            state.mode = input.isKeyPressed(KeyCode::LeftControl) ? DragMode::Pan : DragMode::Rotate;
        } else if (e.button == MouseButton::Right) {
            state.mode = DragMode::Look;
        } else {
            return;
        }
        state.lastX = e.x;
        state.lastY = e.y;
    });

    input.subscribe<MouseButtonReleasedEvent>([&state](const MouseButtonReleasedEvent& e) {
        const MouseButton held = state.mode == DragMode::Look ? MouseButton::Right : MouseButton::Left;
        if (state.mode != DragMode::None && e.button == held) {
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

        switch (state.mode) {
            case DragMode::Rotate: state.rotateDelta += delta; break;
            case DragMode::Pan: state.panDelta += delta; break;
            case DragMode::Look: state.lookDelta += delta; break;
            case DragMode::None: break;
        }
    });

    input.subscribe<MouseScrolledEvent>([&world, &state](const MouseScrolledEvent& e) {
        if (mouseCaptured(world)) {
            return;
        }
        (state.mode == DragMode::Look ? state.flyScroll : state.scroll) += static_cast<float>(e.yOffset);
    });
}

void rotate(OrbitCamera& orbit, glm::vec2 delta) {
    orbit.yaw += delta.x * orbit.rotateSpeed;
    // Clamp pitch to avoid flipping over the poles
    orbit.pitch = glm::clamp(orbit.pitch + delta.y * orbit.rotateSpeed,
        -glm::half_pi<float>() + 0.1f, glm::half_pi<float>() - 0.1f);
}

// Camera-relative movement direction from WASD/QE, not normalized.
glm::vec3 flyDirection(const InputSystem& input) {
    auto axis = [&input](KeyCode positive, KeyCode negative) {
        return (input.isKeyPressed(positive) ? 1.0f : 0.0f) - (input.isKeyPressed(negative) ? 1.0f : 0.0f);
    };
    return { axis(KeyCode::D, KeyCode::A), axis(KeyCode::E, KeyCode::Q), axis(KeyCode::W, KeyCode::S) };
}

// Looks around the camera position: the target moves, not the camera.
void applyLook(OrbitCamera& orbit, const OrbitCameraInput& input) {
    orbit.flySpeed = glm::clamp(orbit.flySpeed * std::pow(orbit.flySpeedStep, input.flyScroll),
        orbit.minFlySpeed, orbit.maxFlySpeed);

    const glm::vec3 position = orbit.position();
    rotate(orbit, input.lookDelta);
    orbit.target = position + glm::normalize(orbit.target - orbit.position()) * orbit.distance;
}

// Moves the camera and its target by the camera-relative move.
void applyMove(OrbitCamera& orbit, glm::vec3 move, bool fast, float dt) {
    glm::vec3 forward = glm::normalize(orbit.target - orbit.position());
    glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    glm::vec3 up = glm::cross(right, forward);

    float speed = orbit.flySpeed * (fast ? orbit.flyFastMultiplier : 1.0f) * dt;
    orbit.target += glm::normalize(right * move.x + up * move.y + forward * move.z) * speed;
}

void applyInput(OrbitCamera& orbit, const OrbitCameraInput& input) {
    rotate(orbit, input.rotateDelta);

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

void updateCameras(OrbitCameraInput& input, const InputSystem& keys, const Time& time, World& world,
    Query<OrbitCamera, TransformComponent> cameras) {
    auto& registry = world.scene().registry();

    const bool flying = input.mode == DragMode::Look;
    const bool useKeys = !keyboardCaptured(world);
    const glm::vec3 move = useKeys ? flyDirection(keys) : glm::vec3(0.0f);
    const bool fast = useKeys && (keys.isKeyPressed(KeyCode::LeftShift) || keys.isKeyPressed(KeyCode::RightShift));

    for (auto&& [entity, orbit, transform] : cameras.each()) {
        applyInput(orbit, input);
        if (flying) {
            applyLook(orbit, input);
        }
        if (move != glm::vec3(0.0f)) {
            applyMove(orbit, move, fast, time.delta);
        }

        const glm::vec3 position = orbit.position();
        hierarchy::setWorld(registry, entity, Transform {
            .position = position,
            .rotation = glm::quatLookAt(glm::normalize(orbit.target - position), glm::vec3(0.0f, 1.0f, 0.0f)),
        });
    }

    input.rotateDelta = glm::vec2(0.0f);
    input.panDelta = glm::vec2(0.0f);
    input.lookDelta = glm::vec2(0.0f);
    input.scroll = 0.0f;
    input.flyScroll = 0.0f;
}

}

void OrbitCameraPlugin::build(App& app) {
    auto& input = app.insertResource<OrbitCameraInput>();

    subscribeInput(app.world(), app.resource<InputSystem>(), input);

    app.addSystem(Stage::Update, updateCameras, "OrbitCameraPlugin::updateCameras");
}

}
