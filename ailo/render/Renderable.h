#pragma once
#include "Material.h"
#include "Mesh.h"

namespace ailo {

struct Renderable {
    asset_ptr<Mesh> mesh;
    std::vector<asset_ptr<Material>> materials;

    DescriptorSetHandle descriptorSet;
};

}
