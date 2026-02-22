#include "Mesh.h"
#include <Engine.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>
#include <stdexcept>
#include <iostream>
#include <vector>
#include <filesystem>

#include "ecs/Scene.h"
#include "ecs/Transform.h"
#include "resources/ResourcePtr.h"

namespace ailo {

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;
    glm::vec3 normal;
    glm::vec4 tangent;
};

// Helper function to convert aiMatrix4x4 to glm::mat4
static glm::mat4 aiMatrixToGlm(const aiMatrix4x4& aiMat) {
    return glm::mat4(
        aiMat.a1, aiMat.b1, aiMat.c1, aiMat.d1,
        aiMat.a2, aiMat.b2, aiMat.c2, aiMat.d2,
        aiMat.a3, aiMat.b3, aiMat.c3, aiMat.d3,
        aiMat.a4, aiMat.b4, aiMat.c4, aiMat.d4
    );
}

// Structure to hold per-mesh data during processing
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
    glm::mat4 transform;
    unsigned int materialIndex;
};

// Helper function to load texture from file
static asset_ptr<Texture> loadTexture(Engine& engine, const std::string& texturePath, const std::string& modelDirectory, vk::Format format = vk::Format::eR8G8B8A8Srgb) {
    // Construct full texture path
    std::filesystem::path fullPath;

    // Check if the texture path is absolute
    if (std::filesystem::path(texturePath).is_absolute()) {
        fullPath = texturePath;
    } else {
        // Make it relative to the model directory
        fullPath = std::filesystem::path(modelDirectory) / texturePath;
    }

    return Texture::load(engine, fullPath.string(), format, true);
}

// Recursive function to traverse scene hierarchy and extract meshes with transforms
static void processNode(
    const aiNode* node,
    const aiScene* aiscene,
    const glm::mat4& parentTransform,
    std::vector<MeshData>& meshDataList
) {
    glm::mat4 nodeTransform = aiMatrixToGlm(node->mTransformation);
    glm::mat4 worldTransform = parentTransform * nodeTransform;

    // Process all meshes attached to this node
    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        unsigned int meshIdx = node->mMeshes[i];
        aiMesh* aiMesh = aiscene->mMeshes[meshIdx];

        MeshData meshData;
        meshData.transform = worldTransform;
        meshData.materialIndex = aiMesh->mMaterialIndex;

        // Extract vertices
        for (unsigned int v = 0; v < aiMesh->mNumVertices; v++) {
            Vertex vertex{};

            // Position
            vertex.pos = glm::vec3(
                aiMesh->mVertices[v].x,
                aiMesh->mVertices[v].y,
                aiMesh->mVertices[v].z
            );

            // Texture coordinates (if available)
            if (aiMesh->mTextureCoords[0]) {
                vertex.texCoord = glm::vec2(
                    aiMesh->mTextureCoords[0][v].x,
                    aiMesh->mTextureCoords[0][v].y
                );
            } else {
                vertex.texCoord = glm::vec2(0.0f, 0.0f);
            }

            if (aiMesh->mColors[0]) {
                vertex.color = glm::vec3(
                    aiMesh->mColors[v]->r,
                    aiMesh->mColors[v]->r,
                    aiMesh->mColors[v]->b
                );
            } else {
                vertex.color = glm::vec3(1.0f, 1.0f, 1.0f);
            }

            if (aiMesh->mNormals) {
                vertex.normal = glm::vec3(
                    (aiMesh->mNormals[v].x),
                    (aiMesh->mNormals[v].y),
                    (aiMesh->mNormals[v].z)
                );
            }

            if (aiMesh->mTangents) {
                glm::vec3 tangent = glm::vec3(
                    (aiMesh->mTangents[v].x),
                    (aiMesh->mTangents[v].y),
                    (aiMesh->mTangents[v].z)
                );
                glm::vec3 biTangent = glm::vec3(
                    aiMesh->mBitangents[v].x,
                    aiMesh->mBitangents[v].y,
                    aiMesh->mBitangents[v].z
                );

                float dot = glm::dot(glm::cross(vertex.normal, tangent), biTangent);

                vertex.tangent = glm::vec4(tangent, dot);

            } else {
                vertex.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
            }

            meshData.vertices.push_back(vertex);
        }

        // Extract indices
        for (unsigned int f = 0; f < aiMesh->mNumFaces; f++) {
            aiFace face = aiMesh->mFaces[f];
            for (unsigned int j = 0; j < face.mNumIndices; j++) {
                meshData.indices.push_back(static_cast<uint16_t>(face.mIndices[j]));
            }
        }
        meshDataList.push_back(std::move(meshData));
    }

    // Recursively process all child nodes
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        processNode(node->mChildren[i], aiscene, worldTransform, meshDataList);
    }
}

static constexpr glm::vec3 sCubeVertices[] = {
    {-10.0f,  10.0f, -10.0f},
    {-10.0f, -10.0f, -10.0f},
    {10.0f, -10.0f, -10.0f},
    {10.0f, -10.0f, -10.0f},
    {10.0f,  10.0f, -10.0f},
    {-10.0f,  10.0f, -10.0f},

    {-10.0f, -10.0f,  10.0f},
    {-10.0f, -10.0f, -10.0f},
    {-10.0f,  10.0f, -10.0f},
    {-10.0f,  10.0f, -10.0f},
    {-10.0f,  10.0f,  10.0f},
    {-10.0f, -10.0f,  10.0f},

    {10.0f, -10.0f, -10.0f},
    {10.0f, -10.0f,  10.0f},
    {10.0f,  10.0f,  10.0f},
    {10.0f,  10.0f,  10.0f},
    {10.0f,  10.0f, -10.0f},
    {10.0f, -10.0f, -10.0f},

    {-10.0f, -10.0f,  10.0f},
    {-10.0f,  10.0f,  10.0f},
    {10.0f,  10.0f,  10.0f},
    {10.0f,  10.0f,  10.0f},
    {10.0f, -10.0f,  10.0f},
    {-10.0f, -10.0f,  10.0f},

    {-10.0f,  10.0f, -10.0f},
    {10.0f,  10.0f, -10.0f},
    {10.0f,  10.0f,  10.0f},
    {10.0f,  10.0f,  10.0f},
    {-10.0f,  10.0f,  10.0f},
    {-10.0f,  10.0f, -10.0f},

    {-10.0f, -10.0f, -10.0f},
    {-10.0f, -10.0f,  10.0f},
    {10.0f, -10.0f, -10.0f},
    {10.0f, -10.0f, -10.0f},
    {-10.0f, -10.0f,  10.0f},
    {10.0f, -10.0f,  10.0f},
};

static constexpr uint16_t sCubeIndices[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35
};

Mesh MeshReader::createCubeMesh(Engine& engine) {
    Mesh mesh;
    vk::VertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(glm::vec3);
    binding.inputRate = vk::VertexInputRate::eVertex;

    vk::VertexInputAttributeDescription posAttr{};
    posAttr.binding = 0;
    posAttr.location = 0;
    posAttr.format = vk::Format::eR32G32B32Sfloat;
    posAttr.offset = 0;

    auto vb = ailo::make_resource<VertexBuffer>(
        engine,
        engine,
        VertexInputDescription {
            .bindings = { binding },
            .attributes = { posAttr }
        },
        sizeof(sCubeVertices)
    );

    vb->updateBuffer(engine, sCubeVertices, sizeof(sCubeVertices));

    auto ib = ailo::make_resource<BufferObject>(engine, engine, BufferBinding::INDEX, sizeof(sCubeIndices));
    ib->updateBuffer(engine, sCubeIndices, sizeof(sCubeIndices));

    mesh.vertexBuffer = vb;
    mesh.indexBuffer = ib;
    mesh.primitives.emplace_back(std::shared_ptr<Material>(), 0, 36);
    return mesh;
}

std::vector<Entity> MeshReader::instantiate(Engine& engine, Scene& scene, const std::string& path) {
    // Static container to keep textures alive
    // static std::vector<std::unique_ptr<Texture>> loadedTextures;

    Assimp::Importer importer;

    const aiScene* aiscene = importer.ReadFile(path,
        aiProcess_Triangulate |
        aiProcess_FlipUVs |
        aiProcess_GenNormals |
        aiProcess_CalcTangentSpace);

    if (!aiscene || aiscene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !aiscene->mRootNode) {
        throw std::runtime_error("Failed to load mesh: " + std::string(importer.GetErrorString()));
    }

    // Get the directory of the model file for resolving texture paths
    std::filesystem::path modelPath(path);
    std::string modelDirectory = modelPath.parent_path().string();

    // Create shader (shared by all meshes)
    auto shader = Shader::load(engine, Shader::getDefaultShaderDescription());

    std::vector<std::shared_ptr<Material>> materials;
    materials.resize(aiscene->mNumMaterials);

    // Process materials and load textures

    for (unsigned int i = 0; i < aiscene->mNumMaterials; i++) {
        aiMaterial* material = aiscene->mMaterials[i];
        asset_ptr<Texture> diffuse = {};
        asset_ptr<Texture> normalMap = {};
        asset_ptr<Texture> metallicRoughnessMap {};

        // Try to get the diffuse texture
        if (material->GetTextureCount(aiTextureType_BASE_COLOR) > 0) {
            aiString texturePath;
            if (material->GetTexture(aiTextureType_BASE_COLOR, 0, &texturePath) == AI_SUCCESS) {
                diffuse = loadTexture(engine, texturePath.C_Str(), modelDirectory);
            }
        }

        if (material->GetTextureCount(aiTextureType_NORMALS) > 0) {
            aiString texturePath;
            if (material->GetTexture(aiTextureType_NORMALS, 0, &texturePath) == AI_SUCCESS) {
                normalMap = loadTexture(engine, texturePath.C_Str(), modelDirectory, vk::Format::eR8G8B8A8Unorm);
            }
        }

        if (material->GetTextureCount(aiTextureType_GLTF_METALLIC_ROUGHNESS) > 0) {
            aiString texturePath;
            if (material->GetTexture(aiTextureType_GLTF_METALLIC_ROUGHNESS, 0, &texturePath) == AI_SUCCESS) {
                metallicRoughnessMap = loadTexture(engine, texturePath.C_Str(), modelDirectory);
            }
        }


        materials[i] = ailo::make_resource<Material>(engine, engine, shader);
        if (diffuse) {
            materials[i]->setTexture(0, diffuse);
        }
        if (normalMap) {
            materials[i]->setTexture(1, normalMap);
        }
        if (metallicRoughnessMap) {
            materials[i]->setTexture(2, metallicRoughnessMap);
        }
    }

    // Traverse the scene hierarchy and collect mesh data with transforms
    std::vector<MeshData> meshDataList;
    glm::mat4 identity(1.0f);
    processNode(aiscene->mRootNode, aiscene, identity, meshDataList);

    std::vector<Entity> entities;

    // Create vertex input description (shared by all meshes)
    VertexInputDescription vertexInput;
    vk::VertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = vk::VertexInputRate::eVertex;
    vertexInput.bindings.push_back(bindingDescription);

    vk::VertexInputAttributeDescription posAttr{};
    posAttr.binding = 0;
    posAttr.location = 0;
    posAttr.format = vk::Format::eR32G32B32Sfloat;
    posAttr.offset = offsetof(Vertex, pos);
    vertexInput.attributes.push_back(posAttr);

    vk::VertexInputAttributeDescription colorAttr{};
    colorAttr.binding = 0;
    colorAttr.location = 1;
    colorAttr.format = vk::Format::eR32G32B32Sfloat;
    colorAttr.offset = offsetof(Vertex, color);
    vertexInput.attributes.push_back(colorAttr);

    vk::VertexInputAttributeDescription texCoordAttr{};
    texCoordAttr.binding = 0;
    texCoordAttr.location = 2;
    texCoordAttr.format = vk::Format::eR32G32Sfloat;
    texCoordAttr.offset = offsetof(Vertex, texCoord);
    vertexInput.attributes.push_back(texCoordAttr);

    vk::VertexInputAttributeDescription normalAttr{};
    normalAttr.binding = 0;
    normalAttr.location = 3;
    normalAttr.format = vk::Format::eR32G32B32Sfloat;
    normalAttr.offset = offsetof(Vertex, normal);
    vertexInput.attributes.push_back(normalAttr);

    vk::VertexInputAttributeDescription tangentAttr{};
    tangentAttr.binding = 0;
    tangentAttr.location = 4;
    tangentAttr.format = vk::Format::eR32G32B32A32Sfloat;
    tangentAttr.offset = offsetof(Vertex, tangent);
    vertexInput.attributes.push_back(tangentAttr);

    // Create an entity for each mesh with its correct transform
    for (const auto& meshData : meshDataList) {
        auto entity = scene.addEntity();
        entities.push_back(entity);

        // Add Mesh component
        Mesh& mesh = scene.addComponent<Mesh>(entity);

        // Add Transform component with the correct world transform
        Transform& transform = scene.addComponent<Transform>(entity);
        transform.transform = meshData.transform;

        // Create vertex buffer
        mesh.vertexBuffer = ailo::make_resource<VertexBuffer>(
            engine,
            engine,
            vertexInput,
            sizeof(Vertex) * meshData.vertices.size()
        );
        mesh.vertexBuffer->updateBuffer(engine, meshData.vertices.data(), sizeof(Vertex) * meshData.vertices.size());

        // Create index buffer
        mesh.indexBuffer = ailo::make_resource<BufferObject>(
            engine,
            engine,
            BufferBinding::INDEX,
            sizeof(uint16_t) * meshData.indices.size()
        );
        mesh.indexBuffer->updateBuffer(engine, meshData.indices.data(), sizeof(uint16_t) * meshData.indices.size());

        auto material = materials[meshData.materialIndex];

        // Create a single primitive for this mesh
        RenderPrimitive primitive(material, 0, meshData.indices.size());
        mesh.primitives.push_back(primitive);
    }

    return entities;
}

}
