#pragma once

#include "RenderAPI.h"
#include "../assets/Assets.h"

namespace ailo {


class Shader : public Asset {
 public:
    Shader(RenderAPI*, const ShaderDescription&);

    ProgramHandle program() const { return m_program; }

    DescriptorSetLayoutHandle getDescriptorSetLayout(uint32_t setIndex) const;

    static ShaderDescription& getHdrShader();

    static asset_ptr<Shader> load(AssetManager* assetManager, RenderAPI*, const ShaderDescription&);

 private:
    std::vector<Unique<gpu::DescriptorSetLayout>> m_descriptorSetLayouts;
    ShaderDescription m_description;
    Unique<gpu::Program> m_program;
};

}
