#include "Shader.h"

#include "Engine.h"

namespace ailo {

void Shader::destroy(Engine& engine) {
    for (auto& layout : m_descriptorSetLayouts) {
        engine.getRenderAPI()->destroyDescriptorSetLayout(layout);
    }

    engine.getRenderAPI()->destroyPipeline(m_pipeline);
}

Shader::Shader(Engine& engine, const PipelineDescription& description)
    : m_pipeline(engine.getRenderAPI()->createGraphicsPipeline(description)) {
    for (auto& bindings : description.layout) {
        m_descriptorSetLayouts.push_back(engine.getRenderAPI()->createDescriptorSetLayout(bindings));
    }
}



}
