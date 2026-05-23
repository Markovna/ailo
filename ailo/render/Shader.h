#pragma once

#include "RenderAPI.h"
#include "../assets/Assets.h"

namespace ailo {

class Engine;

class Shader : public Asset {
 public:
    Shader(RenderAPI*, const ShaderDescription&);
    ~Shader();

    auto program() const { return m_program; }

    DescriptorSetLayoutHandle getDescriptorSetLayout(uint32_t setIndex) const;

    void release();

    static ShaderDescription& getDefaultShaderDescription();
    static ShaderDescription& getSkyboxShaderDescription();
    static ShaderDescription& getHdrShader();
    static ShaderDescription& getShadowShaderDescription();
    static ShaderDescription& getSkinnedShaderDescription();
    static ShaderDescription& getSkinnedShadowShaderDescription();

    static asset_ptr<Shader> load(AssetManager* assetManager, RenderAPI*, const ShaderDescription&);

 private:
    std::vector<DescriptorSetLayoutHandle> m_descriptorSetLayouts;
    ShaderDescription m_description;
    Handle<gpu::Program> m_program;
    RenderAPI* m_renderApi = nullptr;
};

}
