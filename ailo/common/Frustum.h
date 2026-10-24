#pragma once

#include <array>

#include "glm/mat4x4.hpp"
#include "glm/vec4.hpp"
#include "glm/geometric.hpp"

#include "common/AABB.h"

namespace ailo {

// Planes point inward: a point p is inside when dot(plane.xyz, p) + plane.w >= 0 for every plane.
struct Frustum {
    std::array<glm::vec4, 6> planes {};

    Frustum() = default;

    // Gribb-Hartmann extraction for a [0, 1] clip depth (GLM_FORCE_DEPTH_ZERO_TO_ONE).
    explicit Frustum(const glm::mat4& clipFromWorld) noexcept {
        const auto row = [&](int i) {
            return glm::vec4(clipFromWorld[0][i], clipFromWorld[1][i], clipFromWorld[2][i], clipFromWorld[3][i]);
        };
        const glm::vec4 x = row(0), y = row(1), z = row(2), w = row(3);
        planes = { w + x, w - x, w + y, w - y, z, w - z };
        for (glm::vec4& plane : planes) {
            plane /= glm::length(glm::vec3(plane));
        }
    }

    // Conservative: a box near a frustum corner may pass while being outside.
    bool intersects(const Aabb& box) const noexcept {
        const glm::vec3 center = box.center();
        const glm::vec3 extent = box.extent();
        for (const glm::vec4& plane : planes) {
            const glm::vec3 normal(plane);
            const float radius = glm::dot(extent, glm::abs(normal));
            if (glm::dot(normal, center) + plane.w < -radius) {
                return false;
            }
        }
        return true;
    }
};

}
