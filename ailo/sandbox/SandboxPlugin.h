#pragma once

#include <glm/glm.hpp>

#include "input/InputTypes.h"

namespace ailo {

class App;
class InputSystem;

// The demo scene: Sponza + animated character, orbit camera, ImGui console.
// Transitional: camera, ImGui and animation move into their own plugins in the next step.
// Requires: PlatformPlugin, AssetPlugin, RenderPlugin.
struct SandboxPlugin {
    void build(App& app);

private:
    void onMouseButtonPressed(const MouseButtonPressedEvent& e, const InputSystem& input);
    void onMouseButtonReleased(const MouseButtonReleasedEvent& e);
    void onMouseMoved(const MouseMovedEvent& e);
    void onMouseScrolled(const MouseScrolledEvent& e);

    // Orbit camera state
    float m_cameraYaw = 0.0f;
    float m_cameraPitch = 0.0f;
    float m_cameraDistance = 10.0f;
    bool m_isRotating = false;
    bool m_isMoving = false;
    glm::vec3 m_cameraTarget = glm::vec3(0.0f);
    double m_lastMouseX = 0.0;
    double m_lastMouseY = 0.0;
};

}
