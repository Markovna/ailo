#pragma once
#include "MaterialInstance.h"
#include "Mesh.h"
#include "RenderPrimitive.h"
#include "Skin.h"
#include "ecs/Scene.h"

namespace ailo {

struct Renderable {
    AssetPtr<Mesh> mesh;
    std::vector<AssetPtr<MaterialInstance>> materials;

    bool castShadows = true;
    bool receiveShadows = true;

    AssetPtr<Skin> skin;
    Entity skeleton = entt::null;

    // For skinned meshes; created lazily by RenderPlugin and the Renderer.
    BufferObject boneBuffer;
    Unique<gpu::DescriptorSet> descriptorSet;
};

}
