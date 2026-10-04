#pragma once
#include "MaterialInstance.h"
#include "Mesh.h"

namespace ailo {

struct Renderable {
    asset_ptr<Mesh> mesh;
    // One per mesh face.
    std::vector<asset_ptr<MaterialInstance>> materials;

    bool castShadows = true;
    bool receiveShadows = true;

    // Per-object set for skinned meshes (their bone buffer differs per entity); created lazily by the Renderer.
    Unique<gpu::DescriptorSet> descriptorSet;
    // Bone buffer `descriptorSet` was written with; the set is rebuilt when the animator's buffer changes.
    BufferHandle descriptorSetBones;
};

}
