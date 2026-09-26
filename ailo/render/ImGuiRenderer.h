#pragma once

#include <imgui.h>
#include "RenderAPI.h"

namespace ailo {

// Owns the ImGui context and renders its draw data with the RenderAPI.
// Constructing it creates (and makes current) the ImGui context; destroying it destroys the context and
// releases the GPU objects. Must be destroyed before the RenderAPI.
class ImGuiRenderer {
public:
    explicit ImGuiRenderer(RenderAPI* renderAPI);
    ~ImGuiRenderer();

    ImGuiRenderer(const ImGuiRenderer&) = delete;
    ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;

    void processImGuiCommands(ImDrawData* drawData, const ImGuiIO& io);

private:
    void createPipeline();
    void setupRenderState(ImDrawData* drawData, const ImGuiIO& io, uint32_t fbWidth, uint32_t fbHeight);
    void updateTexture(ImTextureData* tex);

    RenderAPI* m_renderAPI;
    ImGuiContext* m_context = nullptr;

    // Resources
    Unique<gpu::Program> m_program;
    Unique<gpu::DescriptorSetLayout> m_samplerDescriptorLayout;
    Unique<gpu::DescriptorSetLayout> m_uniformDescriptorLayout;
    Unique<gpu::DescriptorSet> m_uniformDescriptor;

    // Textures ImGui asked us to create (e.g. the font atlas), keyed by the ImTextureID handed back to it
    std::unordered_map<ImTextureID, Unique<gpu::Texture>> m_textures;
    // Sampler sets for every texture drawn, including textures owned elsewhere
    std::unordered_map<ImTextureID, Unique<gpu::DescriptorSet>> m_samplerDescriptors;

    // Uniform buffer for projection matrix
    Unique<gpu::Buffer> m_uniformBuffer;

    // Dynamic buffers (resized as needed)
    Unique<gpu::VertexBufferLayout> m_vertexLayoutHandle;
    Unique<gpu::Buffer> m_vertexBuffer;
    Unique<gpu::Buffer> m_indexBuffer;
    uint64_t m_vertexBufferSize = 0;
    uint64_t m_indexBufferSize = 0;
};

} // namespace ailo
