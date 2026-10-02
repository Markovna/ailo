#include "Shader.h"

#include "OS.h"
#include "Renderer.h"
#include "assets/Assets.h"

namespace ailo {

DescriptorSetLayoutHandle Shader::getDescriptorSetLayout(uint32_t setIndex) const {
    if (setIndex >= m_descriptorSetLayouts.size()) {
        return {};
    }

    return  m_descriptorSetLayouts[setIndex];
}

ShaderDescription& Shader::getDefaultShaderDescription() {
    static ShaderDescription shaderDescription {
        .vertexShader = os::readFile("shaders/pbr.vert.spv"),
        .fragmentShader = os::readFile("shaders/pbr.frag.spv"),
        .raster = RasterDescription {
            .cullingMode = CullingMode::FRONT,
            .inverseFrontFace = true,
            .depthWriteEnable = true,
            .depthCompareOp = CompareOp::LESS,
        },
        .layout = {
            DescriptorSetLayoutBindings::perView(),
            DescriptorSetLayoutBindings::perObject(),
            {
                  {
                      .binding = 0,
                      .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                      .stageFlags = vk::ShaderStageFlagBits::eFragment,
                  },
                    {
                        .binding = 1,
                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                        .stageFlags = vk::ShaderStageFlagBits::eFragment,
                    },
                    {
                        .binding = 2,
                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                        .stageFlags = vk::ShaderStageFlagBits::eFragment,
                    }
            },
        }
    };
    return shaderDescription;
}

ShaderDescription& Shader::getSkyboxShaderDescription() {
    static ShaderDescription description {
        .vertexShader = os::readFile("shaders/skybox.vert.spv"),
        .fragmentShader = os::readFile("shaders/skybox.frag.spv"),
        .raster = RasterDescription {
            .cullingMode = CullingMode::FRONT,
            .inverseFrontFace = true,
            .depthWriteEnable = true,
            .depthCompareOp = CompareOp::LESS_OR_EQUAL
        },
        .layout = {
            DescriptorSetLayoutBindings::perView(),
            DescriptorSetLayoutBindings::perObject(),
              {
                      {
                          .binding = 0,
                          .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                          .stageFlags = vk::ShaderStageFlagBits::eFragment,
                      },
                },
            }
    };
    return description;
}

ShaderDescription& Shader::getHdrShader() {
    static ShaderDescription description {
        .vertexShader = os::readFile("shaders/hdr.vert.spv"),
        .fragmentShader = os::readFile("shaders/hdr.frag.spv"),
        .raster = RasterDescription {
            .cullingMode = CullingMode::FRONT,
            .inverseFrontFace = false,
            .depthWriteEnable = false,
            .depthCompareOp = CompareOp::LESS_OR_EQUAL
        },
        .layout = {
            DescriptorSetLayoutBindings::perView(),
            DescriptorSetLayoutBindings::perObject(),
              {
                      {
                          .binding = 0,
                          .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                          .stageFlags = vk::ShaderStageFlagBits::eFragment,
                      },
                }
        }
    };
    return description;
}

ShaderDescription& Shader::getShadowShaderDescription() {
    static ShaderDescription description {
        .vertexShader = os::readFile("shaders/shadow.vert.spv"),
        .fragmentShader = os::readFile("shaders/shadow.frag.spv"),
        .raster = RasterDescription {
            .cullingMode = CullingMode::FRONT,
            .inverseFrontFace = true,
            .depthWriteEnable = true,
            .depthCompareOp = CompareOp::LESS,
        },
        .layout = {
            DescriptorSetLayoutBindings::perView(),
            DescriptorSetLayoutBindings::perObject(),
        }
    };
    return description;
}

ShaderDescription& Shader::getSkinnedShaderDescription() {
    static ShaderDescription shaderDescription {
        .vertexShader = os::readFile("shaders/pbr_skinned.vert.spv"),
        .fragmentShader = os::readFile("shaders/pbr.frag.spv"),
        .raster = RasterDescription {
            .cullingMode = CullingMode::FRONT,
            .inverseFrontFace = true,
            .depthWriteEnable = true,
            .depthCompareOp = CompareOp::LESS,
        },
        .layout = {
            DescriptorSetLayoutBindings::perView(),
            DescriptorSetLayoutBindings::perObject(),
            {
                {
                    .binding = 0,
                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                    .stageFlags = vk::ShaderStageFlagBits::eFragment,
                },
                {
                    .binding = 1,
                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                    .stageFlags = vk::ShaderStageFlagBits::eFragment,
                },
                {
                    .binding = 2,
                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                    .stageFlags = vk::ShaderStageFlagBits::eFragment,
                }
            },
        }
    };
    return shaderDescription;
}

ShaderDescription& Shader::getSkinnedShadowShaderDescription() {
    static ShaderDescription description {
        .vertexShader = os::readFile("shaders/shadow_skinned.vert.spv"),
        .fragmentShader = os::readFile("shaders/shadow.frag.spv"),
        .raster = RasterDescription {
            .cullingMode = CullingMode::FRONT,
            .inverseFrontFace = true,
            .depthWriteEnable = true,
            .depthCompareOp = CompareOp::LESS,
        },
        .layout = {
            DescriptorSetLayoutBindings::perView(),
            DescriptorSetLayoutBindings::perObject(),
        }
    };
    return description;
}

asset_ptr<Shader> Shader::load(AssetManager* assetManager, RenderAPI* renderApi, const ShaderDescription& description) {
    return assetManager->emplace<Shader>(renderApi, description);
}

Shader::Shader(RenderAPI* renderApi, const ShaderDescription& description)
    : m_description(description) {

    for (auto& layout : m_description.layout) {
        m_descriptorSetLayouts.push_back(renderApi->createDescriptorSetLayout(layout));
    }

    m_program = renderApi->createProgram(description);
}
}
