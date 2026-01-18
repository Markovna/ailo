#include "Program.h"
#include "RenderAPI.h"
#include "vulkan/VulkanUtils.h"

namespace ailo::gpu {

Program::Program(vk::Device device, const ShaderDescription& description) : m_device(device) {
    m_vertexShader = createShaderModule(description.vertexShader);
    m_fragmentShader = createShaderModule(description.fragmentShader);

    m_pipelineLayout = createPipelineLayout(description.layout);

    auto& raster = description.raster;
    m_rasterParams.cullMode = vkutils::getCullMode(raster.cullingMode);
    m_rasterParams.frontFace = raster.inverseFrontFace ? vk::FrontFace::eClockwise : vk::FrontFace::eCounterClockwise;
    m_rasterParams.blendEnable = raster.blendEnable;
    m_rasterParams.blendOp = { vkutils::getBlendOp(raster.rgbBlendOp), vkutils::getBlendOp(raster.alphaBlendOp) };
    m_rasterParams.depthCompareOp = vkutils::getCompareOperation(raster.depthCompareOp);
    m_rasterParams.depthWriteEnable = raster.depthWriteEnable;
    m_rasterParams.srcBlendFactor = { vkutils::getBlendFunction(raster.srcRgbBlendFunc), vkutils::getBlendFunction(raster.srcAlphaBlendFunc) };
    m_rasterParams.dstBlendFactor = { vkutils::getBlendFunction(raster.dstRgbBlendFunc), vkutils::getBlendFunction(raster.dstAlphaBlendFunc) };
}

Program::~Program() {
    m_device.destroyShaderModule(m_vertexShader);
    m_device.destroyShaderModule(m_fragmentShader);
    m_device.destroyPipelineLayout(m_pipelineLayout);
}

vk::PipelineLayout Program::createPipelineLayout(const std::vector<ShaderDescription::SetLayout>& layoutDescription) {
    std::vector<vk::DescriptorSetLayout> setLayouts;
    for(auto& set : layoutDescription) {
        std::vector<vk::DescriptorSetLayoutBinding> bindings;
        for(auto& binding : set) {
            vk::DescriptorSetLayoutBinding vkBinding;
            vkBinding.binding = binding.binding;
            vkBinding.descriptorType = binding.descriptorType;
            vkBinding.stageFlags = binding.stageFlags;
            vkBinding.descriptorCount = 1;
            bindings.push_back(vkBinding);
        }

        vk::DescriptorSetLayoutCreateInfo layoutInfo {};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        auto descriptorSetLayout = m_device.createDescriptorSetLayout(layoutInfo);
        setLayouts.push_back(descriptorSetLayout);
    }

    // Pipeline layout
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.setLayoutCount = setLayouts.size();
    pipelineLayoutInfo.pSetLayouts = setLayouts.data();

    auto pipelineLayout = m_device.createPipelineLayout(pipelineLayoutInfo);
    for(auto& setLayout : setLayouts) {
        m_device.destroyDescriptorSetLayout(setLayout);
    }

    return pipelineLayout;
 }

vk::ShaderModule Program::createShaderModule(const std::vector<char>& code) {
    vk::ShaderModuleCreateInfo createInfo{};
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    return m_device.createShaderModule(createInfo);
}
}
