#include "Shader.h"

#include "OS.h"
#include "Renderer.h"
#include "assets/Assets.h"
#include "material/MaterialPackage.h"

namespace ailo {

namespace {

// Transitional until materials are loaded as assets (step 2 of the material system): the lit shader
// descriptions below take their SPIR-V from the package matcomp compiles from materials/lit.mat.
const material::MaterialPackage& litMaterialPackage() {
    static const material::MaterialPackage package = [] {
        const char* path = "materials/lit.matpack";
        auto data = os::readFile(path);
        std::string error;
        auto pkg = material::MaterialPackage::deserialize(
            { reinterpret_cast<const uint8_t*>(data.data()), data.size() }, error);
        if (!pkg) {
            throw std::runtime_error(std::string(path) + ": " + error);
        }
        return std::move(*pkg);
    }();
    return package;
}

ShaderDescription::ShaderCode litShader(material::ShaderStage stage, material::Variant::type_t variant) {
    auto spirv = litMaterialPackage().getShader(stage, material::Variant { variant });
    if (spirv.empty()) {
        throw std::runtime_error("materials/lit.matpack has no " + std::string(material::toString(stage)) +
                                 " shader for variant " + std::to_string(variant));
    }
    auto* bytes = reinterpret_cast<const char*>(spirv.data());
    return { bytes, bytes + spirv.size_bytes() };
}

// Set 2 of materials/lit.mat: baseColorMap, normalMap, metallicRoughnessMap (samplers start at kFirstSamplerBinding).
ShaderDescription::SetLayout litMaterialSetLayout() {
    ShaderDescription::SetLayout layout;
    for (uint32_t i = 0; i < 3; i++) {
        layout.push_back({
            .binding = material::kFirstSamplerBinding + i,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .stageFlags = vk::ShaderStageFlagBits::eFragment,
        });
    }
    return layout;
}

using material::ShaderStage;
using material::Variant;

}

DescriptorSetLayoutHandle Shader::getDescriptorSetLayout(uint32_t setIndex) const {
    if (setIndex >= m_descriptorSetLayouts.size()) {
        return {};
    }

    return  m_descriptorSetLayouts[setIndex];
}

ShaderDescription& Shader::getDefaultShaderDescription() {
    static ShaderDescription shaderDescription {
        .vertexShader = litShader(ShaderStage::Vertex, Variant::SHADOWS),
        .fragmentShader = litShader(ShaderStage::Fragment, Variant::SHADOWS),
        .raster = RasterDescription {
            .cullingMode = CullingMode::FRONT,
            .inverseFrontFace = true,
            .depthWriteEnable = true,
            .depthCompareOp = CompareOp::LESS,
        },
        .layout = {
            DescriptorSetLayoutBindings::perView(),
            DescriptorSetLayoutBindings::perObject(),
            litMaterialSetLayout(),
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
        .vertexShader = litShader(ShaderStage::Vertex, Variant::DEPTH_ONLY),
        .fragmentShader = litShader(ShaderStage::Fragment, Variant::DEPTH_ONLY),
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
        .vertexShader = litShader(ShaderStage::Vertex, Variant::SKINNING | Variant::SHADOWS),
        .fragmentShader = litShader(ShaderStage::Fragment, Variant::SKINNING | Variant::SHADOWS),
        .raster = RasterDescription {
            .cullingMode = CullingMode::FRONT,
            .inverseFrontFace = true,
            .depthWriteEnable = true,
            .depthCompareOp = CompareOp::LESS,
        },
        .layout = {
            DescriptorSetLayoutBindings::perView(),
            DescriptorSetLayoutBindings::perObject(),
            litMaterialSetLayout(),
        }
    };
    return shaderDescription;
}

ShaderDescription& Shader::getSkinnedShadowShaderDescription() {
    static ShaderDescription description {
        .vertexShader = litShader(ShaderStage::Vertex, Variant::DEPTH_ONLY | Variant::SKINNING),
        .fragmentShader = litShader(ShaderStage::Fragment, Variant::DEPTH_ONLY | Variant::SKINNING),
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
