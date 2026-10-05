#include "Camera.h"

#include <glm/gtc/matrix_transform.hpp>

namespace ailo {

glm::mat4 Camera::projection(float aspect) const {
    glm::mat4 result = glm::perspective(fovY, aspect, nearPlane, farPlane);
    result[1][1] *= -1; // Flip Y for Vulkan
    return result;
}

}
