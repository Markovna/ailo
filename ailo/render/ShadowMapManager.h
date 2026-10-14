#pragma once

#include <array>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>

#include "glm/fwd.hpp"

namespace ailo {

struct ViewInfo;
struct SceneLighting;

static constexpr uint8_t kMaxShadowCascades = 5;

class ShadowMap {

};

class ShadowMapManager {
public:
    ShadowMapManager() = default;

    void updateCascadeShadows(const ViewInfo& camera, const SceneLighting* sceneLighting);

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

    static void updateProjection(glm::mat4* projection, float nearPlane, float farPlane);

private:
    std::vector<ShadowMap> m_cascadeShadows;
};

}
