#pragma once

#include "Texture.h"

namespace ailo {

struct Skybox {
    asset_ptr<Texture> cubemap;
};

}
