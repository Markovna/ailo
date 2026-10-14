#pragma once

#include <array>

#include "glm/vec3.hpp"
#include "glm/ext/scalar_common.hpp"

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

    float contains(glm::vec3 p) const noexcept {
        float d = min.x - p.x;
        d = glm::max(d, min.y - p.y);
        d = glm::max(d, min.z - p.z);
        d = glm::max(d, p.x - max.x);
        d = glm::max(d, p.y - max.y);
        d = glm::max(d, p.z - max.z);
        return d;
    }
};


}
