#pragma once
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace ailo {

struct Transform {
    glm::vec3 position { 0.0f };
    glm::quat rotation = glm::identity<glm::quat>();
    glm::vec3 scale { 1.0f };

    glm::mat4 toMatrix() const;
    static Transform fromMatrix(const glm::mat4& m);
};

bool isAffine(const glm::mat4& m);

}
