#pragma once

#include <imgui.h>
#include "RenderAPI.h"

namespace ailo {

class ImGuiProcessor {
public:
    ImGuiProcessor(RenderAPI* renderAPI);
    ~ImGuiProcessor();

    void init();
    void shutdown();

    void processImGuiCommands(ImDrawData* drawData, const ImGuiIO& io);

private:
    void createPipeline();
    void setupRenderState(ImDrawData* drawData, const ImGuiIO& io, uint32_t fbWidth, uint32_t fbHeight);
    void updateTexture(ImTextureData* tex);

    RenderAPI* m_renderAPI;

    // Resources
    PipelineHandle m_pipeline;
    DescriptorSetHandle m_descriptorSet;
    DescriptorSetLayoutHandle m_descriptorSetLayout;

    // Uniform buffer for projection matrix
    BufferHandle m_uniformBuffer;

    // Dynamic buffers (resized as needed)
    BufferHandle m_vertexBuffer;
    BufferHandle m_indexBuffer;
    uint64_t m_vertexBufferSize = 0;
    uint64_t m_indexBufferSize = 0;
};

} // namespace ailo
