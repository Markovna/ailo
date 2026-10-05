#pragma once
#include <glm/vec3.hpp>

namespace ailo {

struct PointLight {
    glm::vec3 color { 1.0f };
    float intensity = 1.0f;
    float radius = 3.0f;
};

// Shines along the transform's forward vector (local -Z).
struct SpotLight {
    glm::vec3 color { 1.0f };
    float intensity = 1.0f;
    float radius = 3.0f;
    float innerAngle = 0.5f; // radians, half-angle of the full-intensity cone
    float outerAngle = 0.8f; // radians, half-angle at which the light falls to zero
};

}
