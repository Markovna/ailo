#include "Model.h"

#include <cstring>
#include <filesystem>
#include <limits>
#include <iostream>
#include <set>
#include <stdexcept>
#include <unordered_map>

#include "MaterialInstance.h"
#include "Texture.h"
#include "render/model/ModelPackage.h"

namespace ailo {

namespace {

VertexInputDescription vertexInput(model::VertexLayout layout) {
    VertexInputDescription input;
    input.bindings.push_back(vk::VertexInputBindingDescription { 0, model::vertexStride(layout), vk::VertexInputRate::eVertex });
    auto addAttr = [&](VertexLocation location, vk::Format format, size_t offset) {
        input.attributes.push_back(vk::VertexInputAttributeDescription {
            static_cast<uint32_t>(std::to_underlying(location)), 0, format, static_cast<uint32_t>(offset) });
    };

    // The shared attributes have the same offsets in both layouts.
    using V = model::StaticVertex;
    addAttr(VertexLocation::Position, vk::Format::eR32G32B32Sfloat,    offsetof(V, position));
    addAttr(VertexLocation::Color,    vk::Format::eR32G32B32Sfloat,    offsetof(V, color));
    addAttr(VertexLocation::TexCoord, vk::Format::eR32G32Sfloat,       offsetof(V, texCoord));
    addAttr(VertexLocation::Normal,   vk::Format::eR32G32B32Sfloat,    offsetof(V, normal));
    addAttr(VertexLocation::Tangent,  vk::Format::eR32G32B32A32Sfloat, offsetof(V, tangent));
    if (layout == model::VertexLayout::Skinned) {
        using S = model::SkinnedVertex;
        addAttr(VertexLocation::BoneIndices, vk::Format::eR32G32B32A32Sint,   offsetof(S, boneIndices));
        addAttr(VertexLocation::BoneWeights, vk::Format::eR32G32B32A32Sfloat, offsetof(S, boneWeights));
    }
    return input;
}

Aabb computeBounds(const model::Mesh& mesh) {
    Aabb bounds { glm::vec3(std::numeric_limits<float>::max()), glm::vec3(std::numeric_limits<float>::lowest()) };
    const uint32_t stride = model::vertexStride(mesh.layout);
    for (uint32_t i = 0; i < mesh.vertexCount; i++) {
        glm::vec3 p;
        std::memcpy(&p, mesh.vertices.data() + i * stride + offsetof(model::StaticVertex, position), sizeof(p));
        bounds.min = glm::min(bounds.min, p);
        bounds.max = glm::max(bounds.max, p);
    }
    return mesh.vertexCount ? bounds : Aabb {};
}

static_assert(offsetof(model::StaticVertex, tangent) == offsetof(model::SkinnedVertex, tangent));

// URIs are relative to the folder of the model's key; normalized so that a texture shared through "../" has one key.
std::string textureKey(const std::string& modelKey, const std::string& uri) {
    auto u8 = [](const std::string& s) { return std::u8string(s.begin(), s.end()); };
    auto path = (std::filesystem::path(u8(modelKey)).parent_path() / std::filesystem::path(u8(uri))).lexically_normal();
    auto key = path.generic_u8string();
    return std::string(key.begin(), key.end());
}

}

void ModelLoader::load(const std::string& key, std::span<const uint8_t> bytes, LoadContext<Model>& ctx) {
    std::string error;
    auto pkg = model::ModelPackage::deserialize(bytes, error);
    if (!pkg) {
        throw std::runtime_error("model '" + key + "': " + error);
    }

    RenderAPI* renderApi = m_renderApi;

    std::vector<AssetPtr<Texture>> embedded;
    embedded.reserve(pkg->textures.size());
    for (const auto& texture : pkg->textures) {
        embedded.push_back(m_storages.textures->emplace(noname_t{}, Texture::fromPackage(renderApi, texture)));
    }

    auto resolveTexture = [&](const model::TextureRef& ref) -> AssetPtr<Texture> {
        switch (ref.source) {
            case model::TextureSource::Uri:      return ctx.load<Texture>(textureKey(key, ref.path));
            case model::TextureSource::Embedded: return embedded[ref.embeddedIndex];
            case model::TextureSource::Key:      return ctx.load<Texture>(ref.path);
        }
        return {};
    };

    // modelc doesn't know the materials' parameters, so the ones a material doesn't declare are skipped.
    std::set<std::pair<std::string, std::string>> skipped;
    std::unordered_map<std::string, AssetPtr<Material>> loadedMaterials;
    std::vector<AssetPtr<MaterialInstance>> materials;
    materials.reserve(pkg->materials.size());

    for (const auto& src : pkg->materials) {
        auto& material = loadedMaterials[src.material];
        if (!material) material = ctx.load<Material>(src.material);
        if (!material) {
            std::cerr << "Failed to load material '" << src.material << "'" << std::endl;
            materials.emplace_back();
            continue;
        }

        auto instance = MaterialInstance::create(*m_storages.materialInstances, *m_server, *renderApi, material);
        for (const auto& p : src.floats) {
            if (!material->findParameter(p.name)) {
                skipped.emplace(src.material, p.name);
                continue;
            }
            const auto& v = p.value;
            switch (p.components) {
                case 1: instance->setParameter(p.name, v[0]); break;
                case 2: instance->setParameter(p.name, glm::vec2(v[0], v[1])); break;
                case 3: instance->setParameter(p.name, glm::vec3(v[0], v[1], v[2])); break;
                case 4: instance->setParameter(p.name, glm::vec4(v[0], v[1], v[2], v[3])); break;
            }
        }
        for (const auto& p : src.textures) {
            if (!material->findParameter(p.name)) {
                skipped.emplace(src.material, p.name);
                continue;
            }
            instance->setParameter(p.name, resolveTexture(p.texture));
        }
        materials.push_back(std::move(instance));
    }

    for (const auto& [material, parameter] : skipped) {
        std::cerr << "Model '" << key << "': material '" << material << "' has no parameter '" << parameter
                  << "', skipped" << std::endl;
    }

    std::vector<AssetPtr<Mesh>> meshes;
    meshes.reserve(pkg->meshes.size());
    for (const auto& src : pkg->meshes) {
        auto mesh = m_storages.meshes->emplace(noname_t{});
        mesh->vertexBuffer = VertexBuffer(renderApi, vertexInput(src.layout), src.vertices.size());
        mesh->vertexBuffer.updateBuffer(renderApi, src.vertices.data(), src.vertices.size());

        const auto indexCount = static_cast<uint32_t>(src.indices.size());
        if (model::fitsUint16Indices(src)) {
            const std::vector<uint16_t> indices(src.indices.begin(), src.indices.end());
            mesh->indexBuffer = IndexBuffer(renderApi, IndexType::UINT16, indexCount);
            mesh->indexBuffer.updateBuffer(renderApi, indices.data(), indexCount);
        } else {
            mesh->indexBuffer = IndexBuffer(renderApi, IndexType::UINT32, indexCount);
            mesh->indexBuffer.updateBuffer(renderApi, src.indices.data(), indexCount);
        }
        mesh->faces.push_back({ 0, indexCount });
        mesh->bounds = computeBounds(src);
        meshes.push_back(std::move(mesh));
    }

    AssetPtr<Skeleton> skeleton;
    if (!pkg->skeleton.empty()) {
        skeleton = m_storages.skeletons->emplace(noname_t{});
        skeleton->nodes.reserve(pkg->skeleton.size());
        for (const auto& src : pkg->skeleton) {
            skeleton->nodeNameToIndex[src.name] = static_cast<uint32_t>(skeleton->nodes.size());
            skeleton->nodes.push_back(NodeInfo {
                .name = src.name,
                .parentIndex = src.parent,
                .localTransform = src.localTransform,
                .inverseBindPose = src.inverseBindPose,
                .worldTransform = glm::mat4(1.0f),
                .boneOutputIndex = src.boneOutputIndex,
            });
        }
    }

    std::vector<AssetPtr<AnimationClip>> clips;
    clips.reserve(pkg->clips.size());
    for (auto& src : pkg->clips) {
        auto clip = m_storages.clips->emplace(noname_t{});
        clip->name = src.name;
        clip->ticksPerSecond = src.ticksPerSecond;
        clip->duration = src.duration;
        clip->channels.reserve(src.channels.size());
        for (auto& channel : src.channels) {
            BoneChannel& bc = clip->channels.emplace_back();
            bc.boneName = std::move(channel.node);
            for (const auto& k : channel.positions) bc.positionKeys.push_back({ k.time, k.value });
            for (const auto& k : channel.rotations) bc.rotationKeys.push_back({ k.time, k.value });
            for (const auto& k : channel.scales) bc.scaleKeys.push_back({ k.time, k.value });
        }
        clips.push_back(std::move(clip));
    }

    Model& model = ctx.construct();
    model.instances.reserve(pkg->instances.size());
    for (const auto& src : pkg->instances) {
        model.instances.push_back(Model::MeshInstance {
            .mesh = meshes[src.mesh],
            .material = materials[src.material],
            .transform = src.transform,
            .skinned = pkg->meshes[src.mesh].layout == model::VertexLayout::Skinned,
        });
    }
    model.skeleton = std::move(skeleton);
    model.clips = std::move(clips);
}

}
