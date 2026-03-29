#pragma once
#include "Mesh.h"

namespace ailo {

struct Renderable {
    asset_ptr<Mesh> mesh;
    std::vector<asset_ptr<Material>> materials;

    DescriptorSetHandle descriptorSet;
};

}
