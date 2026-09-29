#include "Mesh.h"

namespace ailo {

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

asset_ptr<Mesh> Mesh::cube(AssetManager* assetManager, RenderAPI* renderApi) {
    auto mesh = assetManager->get<Mesh>("builtin://meshes/cube");
    if (mesh) return mesh;

    mesh = assetManager->emplaceWithPath<Mesh>("builtin://meshes/cube");

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

}
