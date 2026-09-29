#pragma once
#include <vector>
#include <glm/glm.hpp>

#include "Animation.h"
#include "Material.h"
#include "Mesh.h"
#include "Skeleton.h"
#include "assets/Assets.h"

namespace ailo {

struct Model : public Asset {
    struct MeshInstance {
        asset_ptr<Mesh> mesh;
        asset_ptr<Material> material;
        glm::mat4 transform;
        bool skinned = false;
    };

    std::vector<MeshInstance> instances;
    asset_ptr<Skeleton> skeleton;
    std::vector<asset_ptr<AnimationClip>> clips;
};

class ModelImporter : public AssetLoader<Model> {
public:
    ModelImporter(RenderAPI* renderApi) : m_renderApi(renderApi) {}

protected:
    void load(LoadContext<Model>& ctx, const std::string& path) override;

private:
    RenderAPI* m_renderApi;
};

}
