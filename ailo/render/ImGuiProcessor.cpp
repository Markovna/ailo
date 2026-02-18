#include "ImGuiProcessor.h"
#include <OS.h>
#include <cstring>
#include <iostream>

namespace ailo {

ImGuiProcessor::ImGuiProcessor(RenderAPI* renderAPI)
    : m_renderAPI(renderAPI)
{
  ImGuiIO& io = ImGui::GetIO();

  io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.
  io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;   // We can honor ImGuiPlatformIO::Textures[] requests during render.

  io.Fonts->TexDesiredFormat = ImTextureFormat_Alpha8;
}

ImGuiProcessor::~ImGuiProcessor() {
    shutdown();
}

void ImGuiProcessor::init() {
    createPipeline();
}

void ImGuiProcessor::shutdown() {

    // Destroy all textures
    for (ImTextureData* tex : ImGui::GetPlatformIO().Textures) {
      if (tex->Status != ImTextureStatus_WantCreate && tex->RefCount == 1) {
        auto texHandleId = tex->GetTexID();
        TextureHandle texHandle(texHandleId);
        m_renderAPI->destroyTexture(texHandle);
      }
    }

    m_renderAPI->destroyBuffer(m_vertexBuffer);
    m_renderAPI->destroyBuffer(m_indexBuffer);
    m_renderAPI->destroyBuffer(m_uniformBuffer);
    m_renderAPI->destroyDescriptorSet(m_descriptorSet);
    m_renderAPI->destroyDescriptorSetLayout(m_descriptorSetLayout);
    m_renderAPI->destroyProgram(m_program);
}

void ImGuiProcessor::createPipeline() {
    // Create uniform buffer for projection matrix (2 vec2s = 16 bytes)
    m_uniformBuffer = m_renderAPI->createBuffer(ailo::BufferBinding::UNIFORM, 16);

    // Create descriptor set layout with two bindings (swapped for MoltenVK compatibility test)
    ailo::DescriptorSetLayoutBinding uniformBinding{};
    uniformBinding.binding = 0;
    uniformBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
    uniformBinding.stageFlags = vk::ShaderStageFlagBits::eVertex;

    ailo::DescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 1;
    samplerBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    samplerBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

    std::vector<ailo::DescriptorSetLayoutBinding> bindings = { uniformBinding, samplerBinding };
    m_descriptorSetLayout = m_renderAPI->createDescriptorSetLayout(bindings);

    // Create and update descriptor set
    m_descriptorSet = m_renderAPI->createDescriptorSet(m_descriptorSetLayout);
    m_renderAPI->updateDescriptorSetBuffer(m_descriptorSet, m_uniformBuffer, 0);

    // Create the graphics pipeline
    m_program = m_renderAPI->createProgram(
        ShaderDescription {
                .vertexShader = ailo::os::readFile("shaders/imgui.vert.spv"),
                .fragmentShader = ailo::os::readFile("shaders/imgui.frag.spv"),
                    .raster = ailo::RasterDescription {
                        .cullingMode = ailo::CullingMode::NONE,
                        .inverseFrontFace = false,
                        .blendEnable = true,
                        .depthWriteEnable = false,
                        .rgbBlendOp = BlendOperation::ADD,
                        .alphaBlendOp = BlendOperation::ADD,
                        .srcRgbBlendFunc = BlendFunction::SRC_ALPHA,
                        .srcAlphaBlendFunc = BlendFunction::ONE,
                        .dstRgbBlendFunc = BlendFunction::ONE_MINUS_SRC_ALPHA,
                        .dstAlphaBlendFunc = BlendFunction::ONE_MINUS_SRC_ALPHA
                    },
                    .layout {
                       { bindings }
                    }
        }
    );
}

void ImGuiProcessor::setupRenderState(ImDrawData* drawData, const ImGuiIO& io, uint32_t fbWidth, uint32_t fbHeight) {
    // Bind pipeline
    m_renderAPI->bindPipeline(PipelineState{
        .program = m_program,
        .vertexBufferLayout = m_vertexLayoutHandle
    });

    // Bind descriptor set
    m_renderAPI->bindDescriptorSet(m_descriptorSet, 0);

    // Bind vertex and index buffers
    m_renderAPI->bindVertexBuffer(m_vertexBuffer);

    // ImGui uses 16-bit indices by default
    if (sizeof(ImDrawIdx) == 2) {
        m_renderAPI->bindIndexBuffer(m_indexBuffer, vk::IndexType::eUint16);
    } else {
        m_renderAPI->bindIndexBuffer(m_indexBuffer, vk::IndexType::eUint32);
    }

    // Setup viewport
    m_renderAPI->setViewport(0.0f, 0.0f, static_cast<float>(fbWidth), static_cast<float>(fbHeight));
}

void ImGuiProcessor::updateTexture(ImTextureData* tex) {
  if (tex->Status == ImTextureStatus_OK)
    return;

  if (tex->Status == ImTextureStatus_WantCreate) {
    // Create texture
    auto textureHandle = m_renderAPI->createTexture(
        TextureType::TEXTURE_2D,
        vk::Format::eR8Unorm,
        TextureUsage::Sampled,
        static_cast<uint32_t>(tex->Width),
        static_cast<uint32_t>(tex->Height)
    );

    tex->SetTexID((ImTextureID) textureHandle.getId());
    m_renderAPI->updateDescriptorSetTexture(m_descriptorSet, textureHandle, 1);
  }

  if (tex->Status == ImTextureStatus_WantCreate || tex->Status == ImTextureStatus_WantUpdates) {
    const int upload_x = (tex->Status == ImTextureStatus_WantCreate) ? 0 : tex->UpdateRect.x;
    const int upload_y = (tex->Status == ImTextureStatus_WantCreate) ? 0 : tex->UpdateRect.y;
    const int upload_w = (tex->Status == ImTextureStatus_WantCreate) ? tex->Width : tex->UpdateRect.w;
    const int upload_h = (tex->Status == ImTextureStatus_WantCreate) ? tex->Height : tex->UpdateRect.h;

    void* pixels = tex->GetPixels();
    auto uploadSize = tex->GetSizeInBytes();

    auto texHandleId = tex->GetTexID();
    ailo::TextureHandle texHandle(texHandleId);

    // Upload texture data
    m_renderAPI->updateTextureImage(texHandle, pixels, uploadSize);
    tex->SetStatus(ImTextureStatus_OK);
  }

  if (tex->Status == ImTextureStatus_WantDestroy) {
    auto texHandleId = tex->GetTexID();
    ailo::TextureHandle texHandle(texHandleId);

    m_renderAPI->destroyTexture(texHandle);

    // Clear identifiers and mark as destroyed (in order to allow e.g. calling InvalidateDeviceObjects while running)
    tex->SetTexID(ImTextureID_Invalid);
    tex->SetStatus(ImTextureStatus_Destroyed);
  }
}

void ImGuiProcessor::processImGuiCommands(ImDrawData* drawData, const ImGuiIO& io) {
    // Avoid rendering when minimized
    if (drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f) {
        return;
    }

    // Calculate framebuffer size
    int fbWidth = static_cast<int>(drawData->DisplaySize.x * drawData->FramebufferScale.x);
    int fbHeight = static_cast<int>(drawData->DisplaySize.y * drawData->FramebufferScale.y);
    if (fbWidth <= 0 || fbHeight <= 0) {
        return;
    }

    if (drawData->Textures != nullptr)
      for (ImTextureData* tex : *drawData->Textures)
        if (tex->Status != ImTextureStatus_OK)
          updateTexture(tex);

    // Calculate total vertex and index buffer sizes
    uint64_t vertexSize = drawData->TotalVtxCount * sizeof(ImDrawVert);
    uint64_t indexSize = drawData->TotalIdxCount * sizeof(ImDrawIdx);

    if (vertexSize == 0 || indexSize == 0) {
        return;
    }

    // Update uniform buffer with projection matrix
    struct UniformData {
        float scale[2];
        float translate[2];
    } uniformData;

    uniformData.scale[0] = 2.0f / drawData->DisplaySize.x;
    uniformData.scale[1] = 2.0f / drawData->DisplaySize.y;
    uniformData.translate[0] = -1.0f - drawData->DisplayPos.x * uniformData.scale[0];
    uniformData.translate[1] = -1.0f - drawData->DisplayPos.y * uniformData.scale[1];

    m_renderAPI->updateBuffer(m_uniformBuffer, &uniformData, sizeof(uniformData));

    // Create or resize vertex buffer if needed
    if (!m_vertexBuffer || m_vertexBufferSize < vertexSize) {
        if (m_vertexBuffer) {
            m_renderAPI->destroyBuffer(m_vertexBuffer);
        }
        m_vertexBufferSize = vertexSize + 5000 * sizeof(ImDrawVert); // Add some extra space
        m_vertexBuffer = m_renderAPI->createVertexBuffer(nullptr, m_vertexBufferSize);
    }

    if (!m_vertexLayoutHandle) {
        // Define vertex input description matching ImDrawVert structure
        VertexInputDescription vertexInput{};

        // Binding description
        vk::VertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(ImDrawVert);
        binding.inputRate = vk::VertexInputRate::eVertex;
        vertexInput.bindings.push_back(binding);

        // Attribute descriptions
        // Position (vec2 at location 0)
        vk::VertexInputAttributeDescription posAttr{};
        posAttr.location = 0;
        posAttr.binding = 0;
        posAttr.format = vk::Format::eR32G32Sfloat;
        posAttr.offset = offsetof(ImDrawVert, pos);
        vertexInput.attributes.push_back(posAttr);

        // UV (vec2 at location 1)
        vk::VertexInputAttributeDescription uvAttr{};
        uvAttr.location = 1;
        uvAttr.binding = 0;
        uvAttr.format = vk::Format::eR32G32Sfloat;
        uvAttr.offset = offsetof(ImDrawVert, uv);
        vertexInput.attributes.push_back(uvAttr);

        // Color (vec4 at location 2, packed as RGBA32)
        vk::VertexInputAttributeDescription colorAttr{};
        colorAttr.location = 2;
        colorAttr.binding = 0;
        colorAttr.format = vk::Format::eR8G8B8A8Unorm;
        colorAttr.offset = offsetof(ImDrawVert, col);
        vertexInput.attributes.push_back(colorAttr);

        m_vertexLayoutHandle = m_renderAPI->createVertexBufferLayout(vertexInput);
    }

    // Create or resize index buffer if needed
    if (!m_indexBuffer || m_indexBufferSize < indexSize) {
        if (m_indexBuffer) {
            m_renderAPI->destroyBuffer(m_indexBuffer);
        }
        m_indexBufferSize = indexSize + 10000 * sizeof(ImDrawIdx); // Add some extra space
        m_indexBuffer = m_renderAPI->createIndexBuffer(nullptr, m_indexBufferSize);
    }

    // Upload vertex and index data
    ImDrawVert* vtxDst = nullptr;
    ImDrawIdx* idxDst = nullptr;

    // Allocate temporary buffers to collect all vertex and index data
    std::vector<ImDrawVert> vertices;
    std::vector<ImDrawIdx> indices;
    vertices.reserve(drawData->TotalVtxCount);
    indices.reserve(drawData->TotalIdxCount);

    auto idxOffset = 0;
    for (int n = 0; n < drawData->CmdListsCount; n++) {
        const ImDrawList* cmdList = drawData->CmdLists[n];
        vertices.insert(vertices.end(), cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Data + cmdList->VtxBuffer.Size);
        indices.insert(indices.end(), cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Data + cmdList->IdxBuffer.Size);

//        idxOffset += cmdList->IdxBuffer.Size;
//        std::transform(
//            cmdList->IdxBuffer.Data,
//            cmdList->IdxBuffer.Data + cmdList->IdxBuffer.Size,
//            std::back_inserter(indices),
//            [&idxOffset](ImDrawIdx& idx){ return idx + idxOffset; }
//          );
    }

    // Update buffers with collected data
    m_renderAPI->updateBuffer(m_vertexBuffer, vertices.data(), vertexSize);
    m_renderAPI->updateBuffer(m_indexBuffer, indices.data(), indexSize);

    RenderPassDescription renderPass {};
    renderPass.color[0] = { vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore };
    renderPass.depth = { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare };

    m_renderAPI->beginRenderPass(renderPass);
    // Setup render state
    setupRenderState(drawData, io, fbWidth, fbHeight);

    // Will project scissor/clipping rectangles into framebuffer space
    ImVec2 clipOff = drawData->DisplayPos;
    ImVec2 clipScale = drawData->FramebufferScale;

    // Render command lists
    uint32_t globalVtxOffset = 0;
    uint32_t globalIdxOffset = 0;

    for (int n = 0; n < drawData->CmdListsCount; n++) {
        const ImDrawList* cmdList = drawData->CmdLists[n];

        for (int cmdIdx = 0; cmdIdx < cmdList->CmdBuffer.Size; cmdIdx++) {
            const ImDrawCmd* cmd = &cmdList->CmdBuffer[cmdIdx];

            if (cmd->UserCallback != nullptr) {
                // User callback
                cmd->UserCallback(cmdList, cmd);
            } else {
                // Project scissor/clipping rectangles into framebuffer space
                ImVec2 clipMin((cmd->ClipRect.x - clipOff.x) * clipScale.x, (cmd->ClipRect.y - clipOff.y) * clipScale.y);
                ImVec2 clipMax((cmd->ClipRect.z - clipOff.x) * clipScale.x, (cmd->ClipRect.w - clipOff.y) * clipScale.y);

                // Clamp to viewport
                if (clipMin.x < 0.0f) clipMin.x = 0.0f;
                if (clipMin.y < 0.0f) clipMin.y = 0.0f;
                if (clipMax.x > fbWidth) clipMax.x = static_cast<float>(fbWidth);
                if (clipMax.y > fbHeight) clipMax.y = static_cast<float>(fbHeight);
                if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y) {
                    continue;
                }

                // Apply scissor rectangle
                m_renderAPI->setScissor(
                    static_cast<int32_t>(clipMin.x),
                    static_cast<int32_t>(clipMin.y),
                    static_cast<uint32_t>(clipMax.x - clipMin.x),
                    static_cast<uint32_t>(clipMax.y - clipMin.y)
                );

                // Draw
                m_renderAPI->drawIndexed(
                    cmd->ElemCount,
                    1,
                    cmd->IdxOffset + globalIdxOffset,
                    cmd->VtxOffset + globalVtxOffset
                );
            }
        }

        globalIdxOffset += cmdList->IdxBuffer.Size;
        globalVtxOffset += cmdList->VtxBuffer.Size;
    }
    m_renderAPI->endRenderPass();
}

} // namespace ailo
