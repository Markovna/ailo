#pragma once

#include <imgui.h>
#include "RenderAPI.h"

namespace ailo {

// Owns the ImGui context and renders its draw data with the RenderAPI.
// Constructing it creates (and makes current) the ImGui context; destroying it releases the GPU objects
// and then the context. Must be destroyed before the RenderAPI, while the GPU is idle.
class ImGuiRenderer {
public:
    explicit ImGuiRenderer(RenderAPI* renderAPI);
    ~ImGuiRenderer();

    ImGuiRenderer(const ImGuiRenderer&) = delete;
    ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;

    void processImGuiCommands(ImDrawData* drawData, const ImGuiIO& io);

private:
    void createPipeline();
    void releaseResources();
    void setupRenderState(ImDrawData* drawData, const ImGuiIO& io, uint32_t fbWidth, uint32_t fbHeight);
    void updateTexture(ImTextureData* tex);

    RenderAPI* m_renderAPI;
    ImGuiContext* m_context = nullptr;

    // Resources
    ProgramHandle m_program;
    std::unordered_map<ImTextureID, DescriptorSetHandle> m_samplerDescriptors;
    DescriptorSetHandle m_uniformDescriptor;
    DescriptorSetLayoutHandle m_samplerDescriptorLayout;
    DescriptorSetLayoutHandle m_uniformDescriptorLayout;

    // Uniform buffer for projection matrix
    BufferHandle m_uniformBuffer;

    // Dynamic buffers (resized as needed)
    VertexBufferLayoutHandle m_vertexLayoutHandle;
    BufferHandle m_vertexBuffer;
    BufferHandle m_indexBuffer;
    uint64_t m_vertexBufferSize = 0;
    uint64_t m_indexBufferSize = 0;
};

} // namespace ailo
