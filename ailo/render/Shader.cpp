#include "Shader.h"

#include <ranges>

#include "Engine.h"
#include "OS.h"
#include "Renderer.h"
#include "assimp/Vertex.h"

namespace ailo {

void Shader::bindPipeline(Engine& engine, const VertexInputDescription& vertexInput) const {
    PipelineCacheKey key;
    for (auto& binding : vertexInput.bindings) {
        key.vertexBindings[binding.binding] = binding;
    }

    for (auto& attribute : vertexInput.attributes) {
        key.vertexAttributes[attribute.binding] = attribute;
    }

    auto backend = engine.getRenderAPI();

    auto search = m_pipelines.find(key);
    if (search == m_pipelines.end()) {
        auto pipeline = backend->createGraphicsPipeline({.shader = m_description, .vertexInput = vertexInput});
        auto [it, success] = m_pipelines.insert({key, pipeline});

        search = it;
    }

    backend->bindPipeline(search->second);
}

DescriptorSetLayoutHandle Shader::getDescriptorSetLayout(uint32_t setIndex) const {
    if (setIndex >= m_descriptorSetLayouts.size()) {
        return {};
    }

    return  m_descriptorSetLayouts[setIndex];
}

void Shader::destroy(Engine& engine) {
    for (auto& layout : m_descriptorSetLayouts) {
        engine.getRenderAPI()->destroyDescriptorSetLayout(layout);
    }

    for (auto& pipeline : m_pipelines | std::views::values) {
        engine.getRenderAPI()->destroyPipeline(pipeline);
    }
}

ShaderDescription& Shader::getDefaultShaderDescription() {
    static ShaderDescription shaderDescription {
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
                  },
                    {
                        .binding = 1,
                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                        .stageFlags = vk::ShaderStageFlagBits::eFragment,
                    }
            },
        }
    };
    return shaderDescription;
}

Shader::Shader(Engine& engine, const ShaderDescription& description)
    : m_description(description) {

    for (auto& layout : m_description.layout) {
        m_descriptorSetLayouts.push_back(engine.getRenderAPI()->createDescriptorSetLayout(layout));
    }
}

}
