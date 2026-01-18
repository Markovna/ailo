#include "PipelineCache.h"

#include <iostream>
#include <ostream>

#include "vulkan/VulkanUtils.h"

ailo::Pipeline::Pipeline(
    vk::Device device,
    const resource_ptr<gpu::Program>& programPtr,
    vk::RenderPass renderPass,
    const PipelineCacheQuery& key)
        : m_device(device),
        m_programPtr(programPtr) {
    vk::PipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
    vertShaderStageInfo.module = programPtr->vertexShader();
    vertShaderStageInfo.pName = "main";

    vk::PipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
    fragShaderStageInfo.module = programPtr->fragmentShader();
    fragShaderStageInfo.pName = "main";

    vk::PipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // Vertex input
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.vertexBindingDescriptionCount = key.vertexBindingsCount;
    vertexInputInfo.pVertexBindingDescriptions = key.virtexBindings.data();
    vertexInputInfo.vertexAttributeDescriptionCount = key.vertexAttributesCount;
    vertexInputInfo.pVertexAttributeDescriptions = key.vertexAttributes.data();

    // Input assembly
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport state
    vk::PipelineViewportStateCreateInfo viewportState{};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    auto& raster = programPtr->rasterParams();
    // Rasterization
    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = vk::PolygonMode::eFill;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = raster.cullMode;
    rasterizer.frontFace = raster.frontFace;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

    // Depth and stencil testing
    vk::PipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = raster.depthWriteEnable;
    depthStencil.depthCompareOp = raster.depthCompareOp;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color blending
    PerColorAttachment<vk::PipelineColorBlendAttachmentState> colorBlendAttachments{};
    for (auto& colorBlendAttachment : colorBlendAttachments) {
        colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                              vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        colorBlendAttachment.blendEnable = raster.blendEnable;
        colorBlendAttachment.colorBlendOp = raster.blendOp.rgb;
        colorBlendAttachment.alphaBlendOp = raster.blendOp.a;
        colorBlendAttachment.srcColorBlendFactor = raster.srcBlendFactor.rgb;
        colorBlendAttachment.srcAlphaBlendFactor = raster.srcBlendFactor.a;
        colorBlendAttachment.dstColorBlendFactor = raster.dstBlendFactor.rgb;
        colorBlendAttachment.dstAlphaBlendFactor = raster.dstBlendFactor.a;
    }
    vk::PipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = colorBlendAttachments.size();
    colorBlending.pAttachments = colorBlendAttachments.data();

    // Dynamic state
    std::array dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };
    vk::PipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Create graphics pipeline
    vk::GraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = programPtr->pipelineLayout();
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    auto result = m_device.createGraphicsPipeline(nullptr, pipelineInfo);
    if (result.result != vk::Result::eSuccess) {
        throw std::runtime_error("failed to create graphics pipeline!");
    }
    m_pipeline = result.value;
}

ailo::Pipeline::~Pipeline() {
    m_device.destroyPipeline(m_pipeline);
}

ailo::PipelineCache::PipelineCache(vk::Device device, ResourceContainer<Pipeline>& pipelines) :
    m_pipelines(&pipelines), m_cache(kDefaultCacheSize), m_device(device), m_boundVertexLayout() {
}

void ailo::PipelineCache::bindProgram(const resource_ptr<gpu::Program>& program) {
    m_boundProgram = program;
}

ailo::resource_ptr<ailo::Pipeline> ailo::PipelineCache::getOrCreate() {
    PipelineCacheQuery query;
    query.vertexAttributes = m_boundVertexLayout.attributes;
    query.virtexBindings = m_boundVertexLayout.bindings;
    query.vertexAttributesCount = m_boundVertexLayout.attributesCount;
    query.vertexBindingsCount = m_boundVertexLayout.bindingsCount;
    query.programHandle = m_boundProgram.getHandle().getId();
    for (size_t i = 0; i < query.renderPassKey.colors.size(); i++) {
        query.renderPassKey.colors[i] = m_frameBufferFormat.color[i];
    }
    query.renderPassKey.depth = m_frameBufferFormat.depth;

    auto ptr = m_cache.get(query);
    if (ptr) {
        return *ptr;
    }

    resource_ptr<Pipeline> pipeline = resource_ptr<Pipeline>::make(*m_pipelines, m_device, m_boundProgram, m_boundRenderPass, query);
    auto [it, result] = m_cache.tryEmplace(query, pipeline);
    assert(result);
    assert(it->second);
    return it->second;
}
