#pragma once
#include "render/Texture.h"

namespace ailo {

struct SceneLighting {
    AssetPtr<Texture> prefilteredEnvMap;
    glm::vec3 lightDirection;
};

}
