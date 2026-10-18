#pragma once
#include <array>
#include <vector>
#include <glm/glm.hpp>

#include "Animation.h"
#include "MaterialInstance.h"
#include "Mesh.h"
#include "Skeleton.h"
#include "assets/AssetServer.h"

namespace ailo {

struct Model {
    struct MeshInstance {
        AssetPtr<Mesh> mesh;
        AssetPtr<MaterialInstance> material;
        glm::mat4 transform;
        bool skinned = false;
    };

    std::vector<MeshInstance> instances;
    AssetPtr<Skeleton> skeleton;
    std::vector<AssetPtr<AnimationClip>> clips;
};

class ModelLoader : public AssetLoader<Model> {
public:
    struct Storages {
        AssetStorage<Texture>* textures;
        AssetStorage<Mesh>* meshes;
        AssetStorage<MaterialInstance>* materialInstances;
        AssetStorage<Skeleton>* skeletons;
        AssetStorage<AnimationClip>* clips;
    };

    ModelLoader(RenderAPI* renderApi, AssetServer* server, const Storages& storages)
        : m_renderApi(renderApi), m_server(server), m_storages(storages) {}

    constexpr std::span<const std::string_view> extensions() const override { return kExtensions; }

protected:
    void load(const std::string& key, LoadContext<Model>& ctx) override;

private:
    static constexpr std::array<std::string_view, 1> kExtensions = { ".modelpack" };

    RenderAPI* m_renderApi;
    AssetServer* m_server;
    Storages m_storages;
};

}
