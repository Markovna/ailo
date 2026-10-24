#pragma once

#include <array>

#include "glm/vec3.hpp"
#include "glm/ext/scalar_common.hpp"
#include "glm/mat3x3.hpp"
#include "glm/mat4x4.hpp"
#include "glm/common.hpp"

namespace ailo {

struct Aabb {
    glm::vec3 min;
    glm::vec3 max;

    glm::vec3 center() const noexcept {
        return (max + min) * glm::vec3(0.5f);
    }

    glm::vec3 extent() const noexcept {
        return (max - min) * glm::vec3(0.5f);
    }

    std::array<glm::vec3, 8> corners() const noexcept {
        return {
            glm::vec3 { min.x, min.y, min.z },
            glm::vec3 { max.x, min.y, min.z },
            glm::vec3 { min.x, max.y, min.z },
            glm::vec3 { max.x, max.y, min.z },
            glm::vec3 { min.x, min.y, max.z },
            glm::vec3 { max.x, min.y, max.z },
            glm::vec3 { min.x, max.y, max.z },
            glm::vec3 { max.x, max.y, max.z }
        };
    }

    Aabb transformed(const glm::mat4& m) const noexcept {
        const glm::vec3 c = glm::vec3(m * glm::vec4(center(), 1.0f));
        const glm::vec3 e = glm::mat3(glm::abs(glm::vec3(m[0])), glm::abs(glm::vec3(m[1])), glm::abs(glm::vec3(m[2]))) * extent();
        return { c - e, c + e };
    }
};


}
