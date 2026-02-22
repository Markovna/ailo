#pragma once
#include "common/AssetPool.h"
#include "render/Texture.h"

namespace ailo {

struct ImageBasedLighting {
    asset_ptr<Texture> irradianceMap;
};

}
