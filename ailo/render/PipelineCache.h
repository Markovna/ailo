#pragma once

#include <vulkan/vulkan.hpp>
#include <array>

namespace ailo {

struct BlendOp {
    vk::BlendOp rgb;
    vk::BlendOp a;
};

struct BlendFactor {
    vk::BlendFactor rgb;
    vk::BlendFactor a;
};

struct ProgramKey {
    vk::ShaderModule vertShader;
    vk::ShaderModule fragShader;
    vk::PipelineLayout layout;
    vk::CullModeFlags cullMode;
    vk::FrontFace frontFace;
    vk::CompareOp depthCompareOp;
    BlendOp blendOp;
    BlendFactor srcBlendFactor;
    BlendFactor dstBlendFactor;
    bool depthWriteEnable;
    bool blendEnable;
};

class PipelineCacheKey {
    static constexpr size_t kMaxAttributesCount = 8;

    ProgramKey programKey;
    std::array<vk::VertexInputBindingDescription, kMaxAttributesCount> virtexBindings;
    std::array<vk::VertexInputAttributeDescription, kMaxAttributesCount> vertexAttributes;


};

class PipelineCache {

private:
};
}