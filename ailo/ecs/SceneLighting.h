#pragma once
#include "common/AssetPool.h"
#include "render/Texture.h"

namespace ailo {

struct SceneLighting {
    asset_ptr<Texture> prefilteredEnvMap;
    glm::vec3 lightDirection;
};

}
