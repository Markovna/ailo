#pragma once

#include "RenderAPI.h"

namespace ailo {


class Shader {
 public:
    Shader(RenderAPI*, const ShaderDescription&);

    ProgramHandle program() const { return m_program; }

    DescriptorSetLayoutHandle getDescriptorSetLayout(uint32_t setIndex) const;

    static ShaderDescription& getHdrShader();

 private:
    std::vector<Unique<gpu::DescriptorSetLayout>> m_descriptorSetLayouts;
    ShaderDescription m_description;
    Unique<gpu::Program> m_program;
};

}
