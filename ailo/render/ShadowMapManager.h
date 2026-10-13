#pragma once

#include <array>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace ailo {

static constexpr uint8_t kMaxShadowCascades = 5;

class ShadowMapManager {
public:
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
};

}
