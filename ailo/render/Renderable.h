#pragma once
#include "Material.h"
#include "Mesh.h"

namespace ailo {

struct Renderable {
    asset_ptr<Mesh> mesh;
    std::vector<asset_ptr<Material>> materials;

    // Per-object set for skinned meshes (their bone buffer differs per entity); created lazily by the Renderer.
    Unique<gpu::DescriptorSet> descriptorSet;
};

}
