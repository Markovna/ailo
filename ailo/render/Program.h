#pragma once

#include "render/vulkan/Resources.h"
#include "ResourcePtr.h"
#include <vulkan/vulkan.hpp>

namespace ailo {

struct ShaderDescription;
}

namespace ailo::gpu {

struct BlendOp {
    vk::BlendOp rgb;
    vk::BlendOp a;
};

struct BlendFactor {
    vk::BlendFactor rgb;
    vk::BlendFactor a;
};

struct RasterParams {
    vk::CullModeFlags cullMode;
    vk::FrontFace frontFace;
    vk::CompareOp depthCompareOp;
    BlendOp blendOp;
    BlendFactor srcBlendFactor;
    BlendFactor dstBlendFactor;
    bool depthWriteEnable;
    bool blendEnable;
};

class Program : public enable_resource_ptr<Program> {
public:
    Program(vk::Device device, const ShaderDescription& description);
    ~Program();

    RasterParams& rasterParams() { return m_rasterParams; }
    vk::PipelineLayout pipelineLayout() { return m_pipelineLayout; }
    vk::ShaderModule vertexShader() { return m_vertexShader; }
    vk::ShaderModule fragmentShader() { return m_fragmentShader; }

private:
    vk::PipelineLayout createPipelineLayout(const std::vector<ShaderDescription::SetLayout>& layoutDescription);
    vk::ShaderModule createShaderModule(const std::vector<char>& code);

private:
    vk::Device m_device;
    vk::ShaderModule m_vertexShader;
    vk::ShaderModule m_fragmentShader;
    vk::PipelineLayout m_pipelineLayout;
    RasterParams m_rasterParams;
};

}
