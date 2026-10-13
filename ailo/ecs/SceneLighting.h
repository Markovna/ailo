#pragma once
#include "render/IndirectLight.h"

namespace ailo {

struct SceneLighting {
    AssetPtr<IndirectLight> indirectLight;
    glm::vec3 lightDirection = glm::vec3(0.0f, 1.0f, 0.0f);
    float sunIntensity = 1.0f;
    // Scales the light from indirectLight (diffuse and specular).
    float iblIntensity = 1.0f;
};

}
