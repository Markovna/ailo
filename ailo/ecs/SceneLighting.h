#pragma once
#include "render/Texture.h"

namespace ailo {

struct SceneLighting {
    AssetPtr<Texture> prefilteredEnvMap;
    glm::vec3 lightDirection;
    float sunIntensity = 1.0f;
    // Scales the light from prefilteredEnvMap (diffuse and specular).
    float iblIntensity = 1.0f;
};

}
