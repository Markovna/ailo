#pragma once

#include "RenderAPI.h"

namespace ailo {

class Engine;

class Shader {
 public:
    Shader(Engine&, const ShaderDescription&);

    auto program() const { return m_program; }

    DescriptorSetLayoutHandle getDescriptorSetLayout(uint32_t setIndex) const;

    void destroy(Engine&);

    static ShaderDescription& getDefaultShaderDescription();

 private:
    std::vector<DescriptorSetLayoutHandle> m_descriptorSetLayouts;
    ShaderDescription m_description;
    Handle<gpu::Program> m_program;
};

}
