#pragma once
#include "Mesh.h"
#include "common/AssetPool.h"

namespace ailo {

struct Renderable {
    asset_ptr<Mesh> mesh;
};

}
