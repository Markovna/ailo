#include "Transform.h"

#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>

namespace ailo {

glm::mat4 Transform::toMatrix() const {
    const glm::mat3 r = glm::mat3_cast(rotation);
    glm::mat4 m;
    m[0] = glm::vec4(r[0] * scale.x, 0.0f);
    m[1] = glm::vec4(r[1] * scale.y, 0.0f);
    m[2] = glm::vec4(r[2] * scale.z, 0.0f);
    m[3] = glm::vec4(position, 1.0f);
    return m;
}

Transform Transform::fromMatrix(const glm::mat4& m) {
    constexpr float kEpsilon = 1e-8f;

    Transform t;
    t.position = glm::vec3(m[3]);

    glm::vec3 x(m[0]);
    glm::vec3 y(m[1]);
    glm::vec3 z(m[2]);
    t.scale = { glm::length(x), glm::length(y), glm::length(z) };

    // A mirrored basis can't be a rotation: fold the reflection into scale.x.
    if (glm::dot(glm::cross(x, y), z) < 0.0f) {
        t.scale.x = -t.scale.x;
        x = -x;
    }

    // Gram-Schmidt. Degenerate (zero-scale) axes fall back to the identity basis.
    x = t.scale.x != 0.0f ? x / glm::abs(t.scale.x) : glm::vec3(1, 0, 0);
    y -= glm::dot(y, x) * x;
    const float yLength = glm::length(y);
    if (yLength > kEpsilon) {
        y /= yLength;
    } else {
        y = glm::abs(x.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        y = glm::normalize(y - glm::dot(y, x) * x);
    }
    z = glm::cross(x, y);

    t.rotation = glm::normalize(glm::quat_cast(glm::mat3(x, y, z)));
    return t;
}

bool isAffine(const glm::mat4& m) {
    return m[0][3] == 0.0f && m[1][3] == 0.0f && m[2][3] == 0.0f && m[3][3] == 1.0f;
}

}
