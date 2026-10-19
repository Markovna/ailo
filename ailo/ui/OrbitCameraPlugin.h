#pragma once

#include <glm/glm.hpp>

namespace ailo {

class App;

// Orbit controller state and settings. Component; add it next to a TransformComponent (typically a camera entity)
// to have OrbitCameraPlugin move that entity. Can be modified at runtime.
struct OrbitCamera {
    // Pose (spherical coordinates around target, radians)
    glm::vec3 target = glm::vec3(0.0f);
    float distance = 10.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;

    // Controls
    float rotateSpeed = 0.005f;  // radians per pixel
    float panSpeed = 0.001f;     // world units per pixel, scaled by distance
    float zoomSpeed = 0.5f;      // world units per scroll step
    float minDistance = 1.0f;
    float maxDistance = 1000.0f;

    // Fly controls (WASD/QE, RMB look)
    float flySpeed = 5.0f;           // world units per second
    float flyFastMultiplier = 4.0f;  // while Shift is held
    float flySpeedStep = 1.2f;       // flySpeed factor per scroll step
    float minFlySpeed = 0.01f;
    float maxFlySpeed = 1000.0f;

    glm::vec3 position() const;
};

// Requires: PlatformPlugin.
// Input:  Alt + LMB drag rotates, Alt + Ctrl + LMB drag pans, wheel zooms. Ignored while InputCapture::mouse is set.
//         Flies like the Unity scene view: WASD moves, Q/E move down/up, Shift moves faster (keys ignored while
//         InputCapture::keyboard is set). Holding RMB looks around in place and makes the wheel change flySpeed.
// Update: applies the input to every entity with OrbitCamera + TransformComponent and writes its world transform.
struct OrbitCameraPlugin {
    void build(App& app);
};

}
