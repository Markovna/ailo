#pragma once
#include <glm/trigonometric.hpp>
#include <glm/mat4x4.hpp>

namespace ailo {

struct Camera {
    float fovY = glm::radians(60.0f);
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    float exposure = 1.0f;

    glm::mat4 projection(float aspect) const;
};

}
