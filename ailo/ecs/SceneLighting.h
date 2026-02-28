#pragma once
#include "common/AssetPool.h"
#include "render/Texture.h"

namespace ailo {

struct SceneLighting {
    asset_ptr<Texture> irradianceMap;
    glm::vec3 lightDirection;
};

}
