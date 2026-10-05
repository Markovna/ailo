#pragma once
#include <glm/trigonometric.hpp>
#include <glm/mat4x4.hpp>

namespace ailo {

// Perspective camera. Views the scene from the entity's TransformComponent, looking along its forward vector (local -Z).
// The renderer draws from the first entity that has both a Camera and a TransformComponent.
struct Camera {
    float fovY = glm::radians(60.0f);
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

    // Vulkan clip space (Y flipped).
    glm::mat4 projection(float aspect) const;
};

}
