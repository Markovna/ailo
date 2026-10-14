#pragma once

#include <array>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <span>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "common/AABB.h"

namespace ailo {

struct ViewInfo;

// Cascades share one shadow map atlas of kShadowAtlasTiles x kShadowAtlasTiles tiles.
static constexpr uint8_t kMaxShadowCascades = 4;
static constexpr uint32_t kShadowAtlasTiles = 2;
static_assert(kMaxShadowCascades <= kShadowAtlasTiles * kShadowAtlasTiles);

struct ShadowSettings {
    uint8_t cascadeCount = 4;
    // View-space distance covered by the cascades; nothing past it is shadowed.
    float distance = 40.0f;
    // 0 = uniform splits, 1 = logarithmic splits.
    float splitLambda = 0.75f;
    // Fraction of each cascade's depth range cross-faded into the next cascade.
    float blendFraction = 0.1f;
    // How far casters outside a cascade's bounding sphere, toward the light, are still captured.
    float casterDistance = 30.0f;
    bool visualizeCascades = false;
};

struct ShadowCascade {
    glm::mat4 view { 1.0f };
    glm::mat4 projection { 1.0f };
    glm::mat4 viewProjection { 1.0f };
    // View-space distance where this cascade ends.
    float splitFar = 0.0f;
};

class ShadowMapManager {
public:
    ShadowMapManager() = default;

    // lightDirection is the direction the light travels. tileSize is the resolution of one cascade.
    void update(const ViewInfo& camera, const glm::vec3& lightDirection, const ShadowSettings& settings, uint32_t tileSize);

    std::span<const ShadowCascade> cascades() const { return { m_cascades.data(), m_cascadeCount }; }

    static std::array<float, kMaxShadowCascades + 1> computeSplits(uint8_t cascadeCount, float zNear, float zFar, float lambda = 0.75f) {
        cascadeCount = std::clamp(cascadeCount, static_cast<uint8_t>(1), kMaxShadowCascades);

        std::array<float, kMaxShadowCascades + 1> splits {};
        splits[0] = zNear;
        splits[cascadeCount] = zFar;
        for (uint8_t i = 1; i < cascadeCount; ++i) {
            const float p   = static_cast<float>(i) / cascadeCount;
            const float log = zNear * std::pow(zFar / zNear, p);
            const float uni = zNear + (zFar - zNear) * p;
            splits[i] = lambda * log + (1.0f - lambda) * uni;
        }
        return splits;
    }

    static Aabb computeLightFrustumBounds(const glm::mat4& lightView, const Aabb& worldSpaceSceneBounds);
    static Aabb compute2DBounds(const glm::mat4& lightView, const Aabb& volume) noexcept;

private:
    std::array<ShadowCascade, kMaxShadowCascades> m_cascades {};
    uint32_t m_cascadeCount = 0;
};

}
