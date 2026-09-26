#include "OrbitCameraPlugin.h"

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "app/App.h"
#include "input/InputSystem.h"
#include "platform/PlatformPlugin.h"
#include "render/Renderer.h"

namespace ailo {

glm::vec3 OrbitCamera::position() const {
    return target + distance * glm::vec3(
        std::cos(pitch) * std::cos(yaw),
        std::sin(pitch),
        std::cos(pitch) * std::sin(yaw));
}

namespace {

enum class DragMode { None, Rotate, Pan };

struct OrbitCameraDrag {
    DragMode mode = DragMode::None;
    double lastX = 0.0;
    double lastY = 0.0;
};

bool mouseCaptured(World& world) {
    auto* capture = world.tryResource<InputCapture>();
    return capture && capture->mouse;
}

void subscribeInput(World& world, InputSystem& input, OrbitCamera& orbit, OrbitCameraDrag& drag) {
    input.subscribe<MouseButtonPressedEvent>([&world, &input, &drag](const MouseButtonPressedEvent& e) {
        if (e.button != MouseButton::Left || !input.isKeyPressed(KeyCode::LeftAlt) || mouseCaptured(world)) {
            return;
        }
        drag.mode = input.isKeyPressed(KeyCode::LeftControl) ? DragMode::Pan : DragMode::Rotate;
        drag.lastX = e.x;
        drag.lastY = e.y;
    });

    input.subscribe<MouseButtonReleasedEvent>([&drag](const MouseButtonReleasedEvent& e) {
        if (e.button == MouseButton::Left) {
            drag.mode = DragMode::None;
        }
    });

    input.subscribe<MouseMovedEvent>([&orbit, &drag](const MouseMovedEvent& e) {
        if (drag.mode == DragMode::None) {
            return;
        }

        const auto deltaX = static_cast<float>(e.x - drag.lastX);
        const auto deltaY = static_cast<float>(e.y - drag.lastY);
        drag.lastX = e.x;
        drag.lastY = e.y;

        if (drag.mode == DragMode::Rotate) {
            orbit.yaw += deltaX * orbit.rotateSpeed;
            // Clamp pitch to avoid flipping over the poles
            orbit.pitch = glm::clamp(orbit.pitch + deltaY * orbit.rotateSpeed,
                -glm::half_pi<float>() + 0.1f, glm::half_pi<float>() - 0.1f);
        } else {
            glm::vec3 forward = glm::normalize(orbit.target - orbit.position());
            glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
            glm::vec3 up = glm::normalize(glm::cross(right, forward));

            float panSpeed = orbit.distance * orbit.panSpeed;
            orbit.target -= right * deltaX * panSpeed;
            orbit.target += up * deltaY * panSpeed;
        }
    });

    input.subscribe<MouseScrolledEvent>([&world, &orbit](const MouseScrolledEvent& e) {
        if (mouseCaptured(world)) {
            return;
        }
        orbit.distance = glm::clamp(orbit.distance - static_cast<float>(e.yOffset) * orbit.zoomSpeed,
            orbit.minDistance, orbit.maxDistance);
    });
}

void updateCamera(const OrbitCamera& orbit, Camera& camera, const Window& window) {
    camera.view = glm::lookAt(orbit.position(), orbit.target, glm::vec3(0.0f, 1.0f, 0.0f));

    if (float aspect = window.aspect(); aspect > 0.0f) {
        camera.projection = glm::perspective(orbit.fovY, aspect, orbit.nearPlane, orbit.farPlane);
        camera.projection[1][1] *= -1; // Flip Y for Vulkan
    }
}

}

void OrbitCameraPlugin::build(App& app) {
    auto& orbit = app.insertResource<OrbitCamera>(camera);
    auto& drag = app.insertResource<OrbitCameraDrag>();

    subscribeInput(app.world(), app.resource<InputSystem>(), orbit, drag);

    app.addSystem(Stage::Update, updateCamera, "OrbitCameraPlugin::updateCamera");
}

}
