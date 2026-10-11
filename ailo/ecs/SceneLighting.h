#pragma once
#include "render/IndirectLight.h"

namespace ailo {

struct SceneLighting {
    AssetPtr<IndirectLight> indirectLight;
    glm::vec3 lightDirection;
    float sunIntensity = 1.0f;
    // Scales the light from indirectLight (diffuse and specular).
    float iblIntensity = 1.0f;
};

}
