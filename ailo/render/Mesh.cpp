#include "Mesh.h"

#include <array>
#include <cstddef>
#include <utility>

namespace ailo {

namespace {

// Same layout as the non-skinned vertices of ModelImporter, which the PBR shader reads.
struct PbrVertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;
    glm::vec3 normal;
    glm::vec4 tangent;
};

VertexInputDescription pbrVertexInput() {
    VertexInputDescription input;
    input.bindings.push_back(vk::VertexInputBindingDescription {
        0, static_cast<uint32_t>(sizeof(PbrVertex)), vk::VertexInputRate::eVertex });
    auto addAttr = [&](VertexLocation location, vk::Format format, size_t offset) {
        input.attributes.push_back(vk::VertexInputAttributeDescription {
            static_cast<uint32_t>(std::to_underlying(location)), 0, format, static_cast<uint32_t>(offset) });
    };
    addAttr(VertexLocation::Position, vk::Format::eR32G32B32Sfloat, offsetof(PbrVertex, pos));
    addAttr(VertexLocation::Color, vk::Format::eR32G32B32Sfloat, offsetof(PbrVertex, color));
    addAttr(VertexLocation::TexCoord, vk::Format::eR32G32Sfloat, offsetof(PbrVertex, texCoord));
    addAttr(VertexLocation::Normal, vk::Format::eR32G32B32Sfloat, offsetof(PbrVertex, normal));
    addAttr(VertexLocation::Tangent, vk::Format::eR32G32B32A32Sfloat, offsetof(PbrVertex, tangent));
    return input;
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

AssetPtr<Mesh> Mesh::skyboxCube(AssetStorage<Mesh>& storage, RenderAPI* renderApi) {
    constexpr auto kPath = "builtin://meshes/cube";
    if (auto mesh = storage.get(kPath)) return *mesh;

    auto mesh = storage.emplace(kPath);

    vk::VertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(glm::vec3);
    binding.inputRate = vk::VertexInputRate::eVertex;

    vk::VertexInputAttributeDescription posAttr{};
    posAttr.binding = 0;
    posAttr.location = 0;
    posAttr.format = vk::Format::eR32G32B32Sfloat;
    posAttr.offset = 0;

    mesh->vertexBuffer = VertexBuffer(renderApi,
        VertexInputDescription{ .bindings = {binding}, .attributes = {posAttr} },
        sizeof(sCubeVertices));
    mesh->vertexBuffer.updateBuffer(renderApi, sCubeVertices, sizeof(sCubeVertices));

    mesh->indexBuffer = BufferObject(renderApi, BufferBinding::INDEX, sizeof(sCubeIndices));
    mesh->indexBuffer.updateBuffer(renderApi, sCubeIndices, sizeof(sCubeIndices));

    mesh->faces.emplace_back(0, 36);
    return mesh;
}

AssetPtr<Mesh> Mesh::unitCube(AssetStorage<Mesh>& storage, RenderAPI* renderApi) {
    constexpr auto kPath = "builtin://meshes/unit_cube";
    if (auto mesh = storage.get(kPath)) return *mesh;

    // Per face: outward normal and tangent (the +U direction); b = normal x tangent points up the face.
    struct Face { glm::vec3 normal; glm::vec3 tangent; };
    constexpr std::array<Face, 6> faces = {{
        { {  1,  0,  0 }, {  0,  0, -1 } },
        { { -1,  0,  0 }, {  0,  0,  1 } },
        { {  0,  1,  0 }, {  1,  0,  0 } },
        { {  0, -1,  0 }, {  1,  0,  0 } },
        { {  0,  0,  1 }, {  1,  0,  0 } },
        { {  0,  0, -1 }, { -1,  0,  0 } },
    }};
    constexpr std::array<glm::vec2, 4> corners = {{ { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } }};

    std::array<PbrVertex, 24> vertices {};
    std::array<uint16_t, 36> indices {};
    for (size_t f = 0; f < faces.size(); f++) {
        const glm::vec3 n = faces[f].normal;
        const glm::vec3 t = faces[f].tangent;
        const glm::vec3 b = glm::cross(n, t);
        for (size_t c = 0; c < corners.size(); c++) {
            const glm::vec2 corner = corners[c];
            vertices[f * 4 + c] = PbrVertex {
                .pos = 0.5f * (n + corner.x * t + corner.y * b),
                .color = glm::vec3(1.0f),
                .texCoord = { 0.5f + 0.5f * corner.x, 0.5f - 0.5f * corner.y },   // V points down, as imported (FlipUVs)
                .normal = n,
                // Handedness as ModelImporter computes it: dot(n x t, direction of +V) = dot(b, -b).
                .tangent = glm::vec4(t, -1.0f),
            };
        }
        // Counter-clockwise seen from outside, like the imported glTF meshes.
        const auto base = static_cast<uint16_t>(f * 4);
        const std::array<uint16_t, 6> quad = { 0, 1, 2, 0, 2, 3 };
        for (size_t i = 0; i < quad.size(); i++) {
            indices[f * 6 + i] = base + quad[i];
        }
    }

    auto mesh = storage.emplace(kPath);
    mesh->vertexBuffer = VertexBuffer(renderApi, pbrVertexInput(), sizeof(vertices));
    mesh->vertexBuffer.updateBuffer(renderApi, vertices.data(), sizeof(vertices));
    mesh->indexBuffer = BufferObject(renderApi, BufferBinding::INDEX, sizeof(indices));
    mesh->indexBuffer.updateBuffer(renderApi, indices.data(), sizeof(indices));
    mesh->faces.push_back({ 0, static_cast<uint32_t>(indices.size()) });
    return mesh;
}

}
