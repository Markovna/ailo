#include "Mesh.h"
#include <Engine.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>
#include <stdexcept>
#include <iostream>
#include <vector>
#include <unordered_set>
#include <filesystem>
#include <functional>

#include "Renderable.h"
#include "Skeleton.h"
#include "Skin.h"
#include "ecs/AnimatorComponent.h"
#include "ecs/Scene.h"
#include "ecs/Transform.h"

namespace ailo {

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;
    glm::vec3 normal;
    glm::vec4 tangent;
};

struct SkinnedVertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;
    glm::vec3 normal;
    glm::vec4 tangent;
    glm::ivec4 boneIndices;  // location 5
    glm::vec4  boneWeights;  // location 6
};

static glm::mat4 aiMatrixToGlm(const aiMatrix4x4& m) {
    return glm::mat4(
        m.a1, m.b1, m.c1, m.d1,
        m.a2, m.b2, m.c2, m.d2,
        m.a3, m.b3, m.c3, m.d3,
        m.a4, m.b4, m.c4, m.d4
    );
}

struct MeshData {
    glm::mat4 transform;
    uint32_t meshIndex;
    uint32_t materialIndex;
};

static asset_ptr<Texture> loadTexture(Engine& engine, const std::string& texturePath, const std::string& modelDirectory, vk::Format format = vk::Format::eR8G8B8A8Srgb) {
    std::filesystem::path fullPath;
    if (std::filesystem::path(texturePath).is_absolute()) {
        fullPath = texturePath;
    } else {
        fullPath = std::filesystem::path(modelDirectory) / texturePath;
    }
    return Texture::load(engine, fullPath.string(), format, true);
}

static void processNode(
    const aiNode* node,
    const aiScene* aiscene,
    const glm::mat4& parentTransform,
    std::vector<MeshData>& meshDataList
) {
    glm::mat4 worldTransform = parentTransform * aiMatrixToGlm(node->mTransformation);
    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        auto meshIdx = node->mMeshes[i];
        meshDataList.push_back({ worldTransform, meshIdx, aiscene->mMeshes[meshIdx]->mMaterialIndex });
    }
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        processNode(node->mChildren[i], aiscene, worldTransform, meshDataList);
    }
}

static constexpr glm::vec3 sCubeVertices[] = {
    {-10.0f,  10.0f, -10.0f}, {-10.0f, -10.0f, -10.0f}, { 10.0f, -10.0f, -10.0f},
    { 10.0f, -10.0f, -10.0f}, { 10.0f,  10.0f, -10.0f}, {-10.0f,  10.0f, -10.0f},
    {-10.0f, -10.0f,  10.0f}, {-10.0f, -10.0f, -10.0f}, {-10.0f,  10.0f, -10.0f},
    {-10.0f,  10.0f, -10.0f}, {-10.0f,  10.0f,  10.0f}, {-10.0f, -10.0f,  10.0f},
    { 10.0f, -10.0f, -10.0f}, { 10.0f, -10.0f,  10.0f}, { 10.0f,  10.0f,  10.0f},
    { 10.0f,  10.0f,  10.0f}, { 10.0f,  10.0f, -10.0f}, { 10.0f, -10.0f, -10.0f},
    {-10.0f, -10.0f,  10.0f}, {-10.0f,  10.0f,  10.0f}, { 10.0f,  10.0f,  10.0f},
    { 10.0f,  10.0f,  10.0f}, { 10.0f, -10.0f,  10.0f}, {-10.0f, -10.0f,  10.0f},
    {-10.0f,  10.0f, -10.0f}, { 10.0f,  10.0f, -10.0f}, { 10.0f,  10.0f,  10.0f},
    { 10.0f,  10.0f,  10.0f}, {-10.0f,  10.0f,  10.0f}, {-10.0f,  10.0f, -10.0f},
    {-10.0f, -10.0f, -10.0f}, {-10.0f, -10.0f,  10.0f}, { 10.0f, -10.0f, -10.0f},
    { 10.0f, -10.0f, -10.0f}, {-10.0f, -10.0f,  10.0f}, { 10.0f, -10.0f,  10.0f},
};
static constexpr uint16_t sCubeIndices[] = {
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35
};

asset_ptr<Mesh> Mesh::cube(Engine& engine) {
    auto mesh = engine.getAssetManager()->get<Mesh>("builtin://meshes/cube");
    if (mesh) return mesh;

    mesh = engine.getAssetManager()->emplace<Mesh>("builtin://meshes/cube");

    vk::VertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(glm::vec3);
    binding.inputRate = vk::VertexInputRate::eVertex;

    vk::VertexInputAttributeDescription posAttr{};
    posAttr.binding = 0;
    posAttr.location = 0;
    posAttr.format = vk::Format::eR32G32B32Sfloat;
    posAttr.offset = 0;

    auto vb = std::make_shared<VertexBuffer>(engine,
        VertexInputDescription{ .bindings = {binding}, .attributes = {posAttr} },
        sizeof(sCubeVertices));
    vb->updateBuffer(engine, sCubeVertices, sizeof(sCubeVertices));

    auto ib = std::make_shared<BufferObject>(engine, BufferBinding::INDEX, sizeof(sCubeIndices));
    ib->updateBuffer(engine, sCubeIndices, sizeof(sCubeIndices));

    mesh->vertexBuffer = vb;
    mesh->indexBuffer = ib;
    mesh->faces.emplace_back(0, 36);
    return mesh;
}

asset_ptr<Texture> load(Engine& engine, const aiScene* scene, const aiMaterial* material, aiTextureType textureType, vk::Format format, const std::string& modelDirectory) {
    if (material->GetTextureCount(textureType) <= 0) return {};
    aiString texturePath;
    if (material->GetTexture(textureType, 0, &texturePath) != AI_SUCCESS) return {};

    auto embedded = scene->GetEmbeddedTexture(texturePath.C_Str());
    if (embedded) {
        if (embedded->mHeight > 0)
            return Texture::fromEmbedded(engine, embedded->pcData, embedded->mWidth * embedded->mHeight * sizeof(aiTexel), format, embedded->mWidth, embedded->mHeight);
        return Texture::fromEmbeddedCompressed(engine, embedded->pcData, embedded->mWidth, format);
    }
    return loadTexture(engine, texturePath.C_Str(), modelDirectory, format);
}

std::vector<Entity> MeshReader::instantiate(
    Engine& engine, Scene& scene, const std::string& path, const glm::mat4& transform) {
    Assimp::Importer importer;

    const aiScene* aiscene = importer.ReadFile(path,
        aiProcess_Triangulate |
        aiProcess_OptimizeMeshes |
        aiProcess_FlipUVs |
        aiProcess_GenNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_LimitBoneWeights);

    if (!aiscene || aiscene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !aiscene->mRootNode)
        throw std::runtime_error("Failed to load mesh: " + std::string(importer.GetErrorString()));

    auto assetManager = engine.getAssetManager();
    std::filesystem::path modelPath(path);
    std::string modelDirectory = modelPath.parent_path().string();

    // -------------------------------------------------------------------------
    // Skinning: collect bone names and build skeleton
    // -------------------------------------------------------------------------

    // Step 1: collect all bone names
    std::unordered_set<std::string> boneNameSet;
    for (unsigned int i = 0; i < aiscene->mNumMeshes; i++) {
        aiMesh* m = aiscene->mMeshes[i];
        for (unsigned int j = 0; j < m->mNumBones; j++)
            boneNameSet.insert(m->mBones[j]->mName.C_Str());
    }

    const bool hasAnySkinning = !boneNameSet.empty();

    // Step 2: mark nodes to include — bones AND their non-bone ancestors.
    // We need ALL ancestors so that intermediate node transforms (e.g. "Armature")
    // are accumulated into bone world transforms. Without this, inverseBindPose
    // (which was computed from the full hierarchy) won't cancel correctly at bind pose.
    std::unordered_set<std::string> includedNodeNames;
    if (hasAnySkinning) {
        std::function<bool(const aiNode*)> markAncestors = [&](const aiNode* node) -> bool {
            bool anyChildIncluded = false;
            for (unsigned int i = 0; i < node->mNumChildren; i++)
                if (markAncestors(node->mChildren[i])) anyChildIncluded = true;
            std::string name = node->mName.C_Str();
            if (boneNameSet.count(name) || anyChildIncluded) {
                includedNodeNames.insert(name);
                return true;
            }
            return false;
        };
        markAncestors(aiscene->mRootNode);
    }

    // Step 3: build skeleton — all included nodes in parent-first DFS order.
    //   globalBoneRegistry: bone name → boneOutputIndex (index in BonesUniform::bones[])
    //   non-bone nodes get boneOutputIndex = -1 and don't write to BonesUniform.
    auto skeleton = std::make_shared<Skeleton>();
    std::unordered_map<std::string, uint32_t> globalBoneRegistry; // bone name → boneOutputIndex

    if (hasAnySkinning) {
        uint32_t nextBoneOutputIndex = 0;
        std::function<void(const aiNode*, int)> buildSkeleton = [&](const aiNode* node, int parentNodeIdx) {
            std::string name = node->mName.C_Str();
            if (!includedNodeNames.count(name)) return;

            uint32_t nodeIdx = static_cast<uint32_t>(skeleton->nodes.size());
            skeleton->nodeNameToIndex[name] = nodeIdx;

            NodeInfo ni;
            ni.name = name;
            ni.parentIndex = parentNodeIdx;
            ni.localTransform = aiMatrixToGlm(node->mTransformation);
            ni.inverseBindPose = glm::mat4(1.0f);
            if (boneNameSet.count(name)) {
                ni.boneOutputIndex = static_cast<int>(nextBoneOutputIndex);
                globalBoneRegistry[name] = nextBoneOutputIndex++;
            } else {
                ni.boneOutputIndex = -1;
            }
            skeleton->nodes.push_back(ni);

            for (unsigned int i = 0; i < node->mNumChildren; i++)
                buildSkeleton(node->mChildren[i], static_cast<int>(nodeIdx));
        };
        buildSkeleton(aiscene->mRootNode, -1);

        // Step 4: fill inverseBindPose from mesh bone data
        for (unsigned int i = 0; i < aiscene->mNumMeshes; i++) {
            aiMesh* m = aiscene->mMeshes[i];
            for (unsigned int j = 0; j < m->mNumBones; j++) {
                aiBone* bone = m->mBones[j];
                auto it = skeleton->nodeNameToIndex.find(bone->mName.C_Str());
                if (it != skeleton->nodeNameToIndex.end())
                    skeleton->nodes[it->second].inverseBindPose = aiMatrixToGlm(bone->mOffsetMatrix);
            }
        }
    }

    // Which material indices are used by skinned meshes?
    std::unordered_set<uint32_t> skinnedMaterialIndices;
    for (unsigned int i = 0; i < aiscene->mNumMeshes; i++) {
        if (aiscene->mMeshes[i]->mNumBones > 0)
            skinnedMaterialIndices.insert(aiscene->mMeshes[i]->mMaterialIndex);
    }

    // -------------------------------------------------------------------------
    // Shaders
    // -------------------------------------------------------------------------
    auto shader = Shader::load(engine, Shader::getDefaultShaderDescription());
    asset_ptr<Shader> skinnedShader;
    if (hasAnySkinning)
        skinnedShader = Shader::load(engine, Shader::getSkinnedShaderDescription());

    // -------------------------------------------------------------------------
    // Materials
    // -------------------------------------------------------------------------
    std::vector<asset_ptr<Material>> materials(aiscene->mNumMaterials);
    std::vector<asset_ptr<Material>> skinnedMaterials(aiscene->mNumMaterials);

    for (unsigned int i = 0; i < aiscene->mNumMaterials; i++) {
        aiMaterial* mat = aiscene->mMaterials[i];

        auto diffuse = load(engine, aiscene, mat, aiTextureType_BASE_COLOR, vk::Format::eR8G8B8A8Srgb, modelDirectory);
        if (!diffuse) diffuse = load(engine, aiscene, mat, aiTextureType_DIFFUSE, vk::Format::eR8G8B8A8Srgb, modelDirectory);
        if (!diffuse) diffuse = assetManager->get<Texture>("builtin://textures/white");

        auto normalMap = load(engine, aiscene, mat, aiTextureType_NORMALS, vk::Format::eR8G8B8A8Unorm, modelDirectory);
        if (!normalMap) normalMap = assetManager->get<Texture>("builtin://textures/normal");

        auto metallicRoughness = load(engine, aiscene, mat, aiTextureType_GLTF_METALLIC_ROUGHNESS, vk::Format::eR8G8B8A8Srgb, modelDirectory);
        if (!metallicRoughness) metallicRoughness = assetManager->get<Texture>("builtin://textures/default_metallic_roughness");

        auto createMat = [&](asset_ptr<Shader> sh) {
            auto m2 = assetManager->emplace<Material>(assets::no_path{}, engine, sh);
            if (diffuse) m2->setTexture(0, diffuse);
            if (normalMap) m2->setTexture(1, normalMap);
            if (metallicRoughness) m2->setTexture(2, metallicRoughness);
            return m2;
        };

        materials[i] = createMat(shader);
        if (hasAnySkinning && skinnedMaterialIndices.count(i))
            skinnedMaterials[i] = createMat(skinnedShader);
    }

    // -------------------------------------------------------------------------
    // Vertex input descriptions
    // -------------------------------------------------------------------------
    VertexInputDescription vertexInput;
    {
        vk::VertexInputBindingDescription bd{};
        bd.binding = 0; bd.stride = sizeof(Vertex); bd.inputRate = vk::VertexInputRate::eVertex;
        vertexInput.bindings.push_back(bd);
        auto addAttr = [&](uint32_t loc, vk::Format fmt, uint32_t off) {
            vk::VertexInputAttributeDescription a{}; a.binding=0; a.location=loc; a.format=fmt; a.offset=off;
            vertexInput.attributes.push_back(a);
        };
        addAttr(std::to_underlying(VertexLocation::Position),  vk::Format::eR32G32B32Sfloat,    offsetof(Vertex, pos));
        addAttr(std::to_underlying(VertexLocation::Color),     vk::Format::eR32G32B32Sfloat,    offsetof(Vertex, color));
        addAttr(std::to_underlying(VertexLocation::TexCoord),  vk::Format::eR32G32Sfloat,       offsetof(Vertex, texCoord));
        addAttr(std::to_underlying(VertexLocation::Normal),    vk::Format::eR32G32B32Sfloat,    offsetof(Vertex, normal));
        addAttr(std::to_underlying(VertexLocation::Tangent),   vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, tangent));
    }

    VertexInputDescription skinnedVertexInput;
    if (hasAnySkinning) {
        vk::VertexInputBindingDescription bd{};
        bd.binding = 0; bd.stride = sizeof(SkinnedVertex); bd.inputRate = vk::VertexInputRate::eVertex;
        skinnedVertexInput.bindings.push_back(bd);
        auto addAttr = [&](uint32_t loc, vk::Format fmt, uint32_t off) {
            vk::VertexInputAttributeDescription a{}; a.binding=0; a.location=loc; a.format=fmt; a.offset=off;
            skinnedVertexInput.attributes.push_back(a);
        };
        addAttr(std::to_underlying(VertexLocation::Position),    vk::Format::eR32G32B32Sfloat,    offsetof(SkinnedVertex, pos));
        addAttr(std::to_underlying(VertexLocation::Color),       vk::Format::eR32G32B32Sfloat,    offsetof(SkinnedVertex, color));
        addAttr(std::to_underlying(VertexLocation::TexCoord),    vk::Format::eR32G32Sfloat,       offsetof(SkinnedVertex, texCoord));
        addAttr(std::to_underlying(VertexLocation::Normal),      vk::Format::eR32G32B32Sfloat,    offsetof(SkinnedVertex, normal));
        addAttr(std::to_underlying(VertexLocation::Tangent),     vk::Format::eR32G32B32A32Sfloat, offsetof(SkinnedVertex, tangent));
        addAttr(std::to_underlying(VertexLocation::BoneIndices), vk::Format::eR32G32B32A32Sint,   offsetof(SkinnedVertex, boneIndices));
        addAttr(std::to_underlying(VertexLocation::BoneWeights), vk::Format::eR32G32B32A32Sfloat, offsetof(SkinnedVertex, boneWeights));
    }

    // -------------------------------------------------------------------------
    // Build per-aiMesh vertex/index buffers
    // -------------------------------------------------------------------------
    std::vector<bool> meshHasBones(aiscene->mNumMeshes, false);
    for (unsigned int i = 0; i < aiscene->mNumMeshes; i++)
        meshHasBones[i] = aiscene->mMeshes[i]->mNumBones > 0;

    std::vector<asset_ptr<Mesh>> meshes;
    meshes.reserve(aiscene->mNumMeshes);

    for (unsigned int i = 0; i < aiscene->mNumMeshes; i++) {
        aiMesh* aiMesh = aiscene->mMeshes[i];
        meshes.push_back(assetManager->emplace<Mesh>(assets::no_path{}));
        auto mesh = meshes.back();

        std::vector<uint16_t> indices;
        indices.reserve(aiMesh->mNumFaces * 3);
        for (unsigned int f = 0; f < aiMesh->mNumFaces; f++) {
            for (unsigned int j = 0; j < aiMesh->mFaces[f].mNumIndices; j++)
                indices.push_back(static_cast<uint16_t>(aiMesh->mFaces[f].mIndices[j]));
        }

        if (meshHasBones[i]) {
            // Accumulate per-vertex bone weights
            struct VBW { std::vector<std::pair<uint32_t, float>> weights; };
            std::vector<VBW> vbw(aiMesh->mNumVertices);
            for (unsigned int j = 0; j < aiMesh->mNumBones; j++) {
                aiBone* bone = aiMesh->mBones[j];
                auto it = globalBoneRegistry.find(bone->mName.C_Str());
                if (it == globalBoneRegistry.end()) continue;
                uint32_t boneOutputIdx = it->second;
                for (unsigned int k = 0; k < bone->mNumWeights; k++)
                    vbw[bone->mWeights[k].mVertexId].weights.push_back({boneOutputIdx, bone->mWeights[k].mWeight});
            }

            std::vector<SkinnedVertex> verts;
            verts.reserve(aiMesh->mNumVertices);
            for (unsigned int v = 0; v < aiMesh->mNumVertices; v++) {
                SkinnedVertex sv{};
                sv.pos = {aiMesh->mVertices[v].x, aiMesh->mVertices[v].y, aiMesh->mVertices[v].z};
                sv.texCoord = aiMesh->mTextureCoords[0]
                    ? glm::vec2(aiMesh->mTextureCoords[0][v].x, aiMesh->mTextureCoords[0][v].y)
                    : glm::vec2(0.0f);
                sv.color = aiMesh->mColors[0]
                    ? glm::vec3(aiMesh->mColors[0][v].r, aiMesh->mColors[0][v].g, aiMesh->mColors[0][v].b)
                    : glm::vec3(1.0f);
                if (aiMesh->mNormals)
                    sv.normal = {aiMesh->mNormals[v].x, aiMesh->mNormals[v].y, aiMesh->mNormals[v].z};
                if (aiMesh->mTangents) {
                    glm::vec3 t(aiMesh->mTangents[v].x, aiMesh->mTangents[v].y, aiMesh->mTangents[v].z);
                    glm::vec3 b(aiMesh->mBitangents[v].x, aiMesh->mBitangents[v].y, aiMesh->mBitangents[v].z);
                    sv.tangent = glm::vec4(t, glm::dot(glm::cross(sv.normal, t), b));
                } else {
                    sv.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                }
                sv.boneIndices = glm::ivec4(0);
                sv.boneWeights = glm::vec4(0.0f);
                const auto& bws = vbw[v].weights;
                for (int k = 0; k < std::min(4, static_cast<int>(bws.size())); k++) {
                    sv.boneIndices[k] = static_cast<int>(bws[k].first);
                    sv.boneWeights[k] = bws[k].second;
                }
                verts.push_back(sv);
            }
            mesh->vertexBuffer = std::make_shared<VertexBuffer>(engine, skinnedVertexInput, sizeof(SkinnedVertex) * verts.size());
            mesh->vertexBuffer->updateBuffer(engine, verts.data(), sizeof(SkinnedVertex) * verts.size());
        } else {
            std::vector<Vertex> verts;
            verts.reserve(aiMesh->mNumVertices);
            for (unsigned int v = 0; v < aiMesh->mNumVertices; v++) {
                Vertex vx{};
                vx.pos = {aiMesh->mVertices[v].x, aiMesh->mVertices[v].y, aiMesh->mVertices[v].z};
                vx.texCoord = aiMesh->mTextureCoords[0]
                    ? glm::vec2(aiMesh->mTextureCoords[0][v].x, aiMesh->mTextureCoords[0][v].y)
                    : glm::vec2(0.0f);
                if (aiMesh->mColors[0]) {
                    vx.color = {aiMesh->mColors[v]->r, aiMesh->mColors[v]->r, aiMesh->mColors[v]->b};
                } else {
                    vx.color = glm::vec3(1.0f);
                }
                if (aiMesh->mNormals)
                    vx.normal = {aiMesh->mNormals[v].x, aiMesh->mNormals[v].y, aiMesh->mNormals[v].z};
                if (aiMesh->mTangents) {
                    glm::vec3 t(aiMesh->mTangents[v].x, aiMesh->mTangents[v].y, aiMesh->mTangents[v].z);
                    glm::vec3 b(aiMesh->mBitangents[v].x, aiMesh->mBitangents[v].y, aiMesh->mBitangents[v].z);
                    vx.tangent = glm::vec4(t, glm::dot(glm::cross(vx.normal, t), b));
                } else {
                    vx.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                }
                verts.push_back(vx);
            }
            mesh->vertexBuffer = std::make_shared<VertexBuffer>(engine, vertexInput, sizeof(Vertex) * verts.size());
            mesh->vertexBuffer->updateBuffer(engine, verts.data(), sizeof(Vertex) * verts.size());
        }

        mesh->indexBuffer = std::make_shared<BufferObject>(engine, BufferBinding::INDEX, sizeof(uint16_t) * indices.size());
        mesh->indexBuffer->updateBuffer(engine, indices.data(), sizeof(uint16_t) * indices.size());
        mesh->faces.push_back({0, static_cast<uint32_t>(indices.size())});
    }

    // -------------------------------------------------------------------------
    // Parse animation clips
    // -------------------------------------------------------------------------
    std::vector<AnimationClip> animationClips;
    if (hasAnySkinning) {
        animationClips.reserve(aiscene->mNumAnimations);
        for (unsigned int i = 0; i < aiscene->mNumAnimations; i++) {
            aiAnimation* aiAnim = aiscene->mAnimations[i];
            AnimationClip clip;
            clip.name = aiAnim->mName.C_Str();
            clip.ticksPerSecond = aiAnim->mTicksPerSecond > 0.0 ? static_cast<float>(aiAnim->mTicksPerSecond) : 25.0f;
            clip.duration = static_cast<float>(aiAnim->mDuration) / clip.ticksPerSecond;

            for (unsigned int j = 0; j < aiAnim->mNumChannels; j++) {
                aiNodeAnim* ch = aiAnim->mChannels[j];
                BoneChannel bc;
                bc.boneName = ch->mNodeName.C_Str();
                for (unsigned int k = 0; k < ch->mNumPositionKeys; k++)
                    bc.positionKeys.push_back({ static_cast<float>(ch->mPositionKeys[k].mTime) / clip.ticksPerSecond,
                        {ch->mPositionKeys[k].mValue.x, ch->mPositionKeys[k].mValue.y, ch->mPositionKeys[k].mValue.z} });
                for (unsigned int k = 0; k < ch->mNumRotationKeys; k++)
                    bc.rotationKeys.push_back({ static_cast<float>(ch->mRotationKeys[k].mTime) / clip.ticksPerSecond,
                        glm::quat(ch->mRotationKeys[k].mValue.w, ch->mRotationKeys[k].mValue.x,
                                  ch->mRotationKeys[k].mValue.y, ch->mRotationKeys[k].mValue.z) });
                for (unsigned int k = 0; k < ch->mNumScalingKeys; k++)
                    bc.scaleKeys.push_back({ static_cast<float>(ch->mScalingKeys[k].mTime) / clip.ticksPerSecond,
                        {ch->mScalingKeys[k].mValue.x, ch->mScalingKeys[k].mValue.y, ch->mScalingKeys[k].mValue.z} });
                clip.channels.push_back(std::move(bc));
            }
            animationClips.push_back(std::move(clip));
        }
    }

    std::shared_ptr<BufferObject> sharedBoneBuffer;
    if (hasAnySkinning)
        sharedBoneBuffer = std::make_shared<BufferObject>(engine, BufferBinding::UNIFORM, sizeof(BonesUniform));

    // -------------------------------------------------------------------------
    // Create entities from scene hierarchy
    // -------------------------------------------------------------------------
    std::vector<MeshData> meshDataList;
    processNode(aiscene->mRootNode, aiscene, transform, meshDataList);

    std::vector<Entity> entities;
    entities.reserve(meshDataList.size() + (hasAnySkinning ? 1 : 0));

    uint32_t renderableCount = 0;
    for (const auto& md : meshDataList) {
        auto entity = scene.addEntity();
        entities.push_back(entity);

        Renderable& renderable = scene.addComponent<Renderable>(entity);
        renderable.mesh = meshes[md.meshIndex];

        bool isSkinned = meshHasBones[md.meshIndex];
        if (isSkinned && skinnedMaterials[md.materialIndex])
            renderable.materials.push_back(skinnedMaterials[md.materialIndex]);
        else
            renderable.materials.push_back(materials[md.materialIndex]);

        Transform& tr = scene.addComponent<Transform>(entity);
        tr.transform = md.transform;

        if (isSkinned)
            scene.addComponent<Skin>(entity, sharedBoneBuffer);

        renderableCount++;
    }

    if (hasAnySkinning && !animationClips.empty()) {
        auto skelEntity = scene.addEntity();
        auto& animator = scene.addComponent<AnimatorComponent>(skelEntity);
        animator.skeleton = skeleton;
        animator.clips = std::move(animationClips);
        animator.boneBuffer = sharedBoneBuffer;
        entities.push_back(skelEntity);
    }

    std::cout << "Loaded " << renderableCount << " renderables" << std::endl;
    return entities;
}

}
