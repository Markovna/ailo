#include "Mesh.h"
#include <Engine.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <stdexcept>
#include <iostream>
#include <vector>

#include "ecs/Scene.h"
#include "ecs/Transform.h"

namespace ailo {

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;
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
};

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

            // Color (use normal as color if available, otherwise white)
            if (aiMesh->mNormals) {
                vertex.color = glm::vec3(
                    (aiMesh->mNormals[v].x + 1.0f) * 0.5f,
                    (aiMesh->mNormals[v].y + 1.0f) * 0.5f,
                    (aiMesh->mNormals[v].z + 1.0f) * 0.5f
                );
            } else {
                vertex.color = glm::vec3(1.0f, 1.0f, 1.0f);
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

std::vector<Entity> MeshReader::read(Engine& engine, Scene& scene, const std::string& path) {
    Assimp::Importer importer;

    const aiScene* aiscene = importer.ReadFile(path,
        aiProcess_Triangulate |
        aiProcess_FlipUVs |
        aiProcess_GenNormals |
        aiProcess_JoinIdenticalVertices);

    if (!aiscene || aiscene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !aiscene->mRootNode) {
        throw std::runtime_error("Failed to load mesh: " + std::string(importer.GetErrorString()));
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

    // Create shader and material (shared by all meshes)
    auto shader = Shader::createDefaultShader(engine, vertexInput);

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
        mesh.vertexBuffer = std::make_unique<BufferObject>(
            engine,
            BufferBinding::VERTEX,
            sizeof(Vertex) * meshData.vertices.size()
        );
        mesh.vertexBuffer->updateBuffer(engine, meshData.vertices.data(), sizeof(Vertex) * meshData.vertices.size());

        // Create index buffer
        mesh.indexBuffer = std::make_unique<BufferObject>(
            engine,
            BufferBinding::INDEX,
            sizeof(uint16_t) * meshData.indices.size()
        );
        mesh.indexBuffer->updateBuffer(engine, meshData.indices.data(), sizeof(uint16_t) * meshData.indices.size());

        auto material = std::make_unique<Material>(engine, *shader);
        // Create a single primitive for this mesh
        RenderPrimitive primitive;
        primitive.setVertexBuffer(mesh.vertexBuffer.get());
        primitive.setIndexBuffer(mesh.indexBuffer.get(), 0, meshData.indices.size());
        primitive.setMaterial(material.get());
        mesh.primitives.push_back(primitive);

        // Store material reference (all meshes share the same material for now)
        mesh.materials.push_back(std::move(material));
    }
    // TODO: fix the leak
    shader.release();

    return entities;
}

}
