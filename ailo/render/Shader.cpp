#include "Shader.h"

#include "Engine.h"
#include "OS.h"
#include "Renderer.h"
#include "assimp/Vertex.h"

namespace ailo {

void Shader::destroy(Engine& engine) {
    for (auto& layout : m_descriptorSetLayouts) {
        engine.getRenderAPI()->destroyDescriptorSetLayout(layout);
    }

    engine.getRenderAPI()->destroyPipeline(m_pipeline);
}

std::unique_ptr<Shader> Shader::createDefaultShader(Engine& engine, VertexInputDescription& vertexInput) {
    return std::make_unique<Shader>(
    engine,
    PipelineDescription {
        .vertexShader = os::readFile("shaders/shader.vert.spv"),
        .fragmentShader = os::readFile("shaders/shader.frag.spv"),
        .raster = RasterDescription {
            .cullingMode = CullingMode::FRONT,
            .inverseFrontFace = true,
            .depthWriteEnable = true,
            .depthCompareOp = CompareOp::LESS
        },
        .layout = {
            DescriptorSetLayoutBindings::perView(),
            DescriptorSetLayoutBindings::perObject(),
            {
              {
                .binding = 0,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .stageFlags = vk::ShaderStageFlagBits::eFragment,
              }
            },
          },
        .vertexInput = vertexInput
      }
  );
}

Shader::Shader(Engine& engine, const PipelineDescription& description)
    : m_pipeline(engine.getRenderAPI()->createGraphicsPipeline(description)) {
    for (auto& bindings : description.layout) {
        m_descriptorSetLayouts.push_back(engine.getRenderAPI()->createDescriptorSetLayout(bindings));
    }
}



}
