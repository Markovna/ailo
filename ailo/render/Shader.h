#pragma once

#include "RenderAPI.h"
#include "common/AssetPool.h"

namespace ailo {

class Engine;

class Shader : public enable_asset_ptr<Shader> {
 public:
    Shader(Engine&, const ShaderDescription&);

    auto program() const { return m_program; }

    DescriptorSetLayoutHandle getDescriptorSetLayout(uint32_t setIndex) const;

    void destroy(Engine&);

    static ShaderDescription& getDefaultShaderDescription();
    static ShaderDescription& getSkyboxShaderDescription();
    static ShaderDescription& getHdrShader();

    static asset_ptr<Shader> load(Engine&, const ShaderDescription&);

 private:
    std::vector<DescriptorSetLayoutHandle> m_descriptorSetLayouts;
    ShaderDescription m_description;
    Handle<gpu::Program> m_program;
};

}
