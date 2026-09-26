#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace ailo {

class App;

// Orbit camera state and settings. Resource inserted by OrbitCameraPlugin; can be modified at runtime.
struct OrbitCamera {
    // Pose (spherical coordinates around target, radians)
    glm::vec3 target = glm::vec3(0.0f);
    float distance = 10.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;

    // Projection
    float fovY = glm::radians(60.0f);
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

    // Controls
    float rotateSpeed = 0.005f;  // radians per pixel
    float panSpeed = 0.001f;     // world units per pixel, scaled by distance
    float zoomSpeed = 0.5f;      // world units per scroll step
    float minDistance = 1.0f;
    float maxDistance = 1000.0f;

    glm::vec3 position() const;
};

// Requires: PlatformPlugin, RenderPlugin (Camera).
// Resources: OrbitCamera.
// Input:  Alt + LMB drag rotates, Alt + Ctrl + LMB drag pans, wheel zooms. Ignored while InputCapture::mouse is set.
// Update: writes Camera view/projection from OrbitCamera and the window aspect ratio.
struct OrbitCameraPlugin {
    OrbitCamera camera;

    void build(App& app);
};

}
