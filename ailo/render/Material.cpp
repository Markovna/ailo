#include "Material.h"

#include <iostream>

#include "OS.h"
#include "Renderer.h"

namespace ailo {

namespace {

CullingMode toCullingMode(material::CullingMode mode) {
    switch (mode) {
        case material::CullingMode::None:         return CullingMode::NONE;
        case material::CullingMode::Front:        return CullingMode::FRONT;
        case material::CullingMode::Back:         return CullingMode::BACK;
        case material::CullingMode::FrontAndBack: return CullingMode::FRONT_AND_BACK;
    }
    return CullingMode::BACK;
}

CompareOp toCompareOp(material::DepthFunc func) {
    switch (func) {
        case material::DepthFunc::Never:        return CompareOp::NEVER;
        case material::DepthFunc::Less:         return CompareOp::LESS;
        case material::DepthFunc::Equal:        return CompareOp::EQUAL;
        case material::DepthFunc::LessEqual:    return CompareOp::LESS_OR_EQUAL;
        case material::DepthFunc::Greater:      return CompareOp::GREATER;
        case material::DepthFunc::NotEqual:     return CompareOp::NOT_EQUAL;
        case material::DepthFunc::GreaterEqual: return CompareOp::GREATER_OR_EQUAL;
        case material::DepthFunc::Always:       return CompareOp::ALWAYS;
    }
    return CompareOp::LESS;
}

ShaderDescription::ShaderCode toShaderCode(std::span<const uint32_t> spirv) {
    auto* bytes = reinterpret_cast<const char*>(spirv.data());
    return { bytes, bytes + spirv.size_bytes() };
}

}

Material::Material(RenderAPI* renderApi, material::MaterialPackage package)
    : m_renderApi(renderApi), m_package(std::move(package)) {
    const auto& raster = m_package.raster;
    m_raster.cullingMode = raster.doubleSided ? CullingMode::NONE : toCullingMode(raster.culling);
    m_raster.inverseFrontFace = false;
    m_raster.depthWriteEnable = raster.depthWrite;
    m_raster.colorWriteEnable = raster.colorWrite;
    m_raster.depthCompareOp = raster.depthCulling ? toCompareOp(raster.depthFunc) : CompareOp::ALWAYS;

    constexpr auto kAllStages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    if (m_package.uniformBlockSize > 0) {
        m_descriptorSetLayoutBindings.push_back({
            .binding = material::kMaterialParamsBinding,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .stageFlags = kAllStages,
        });
    }
    for (const auto& p : m_package.parameters) {
        if (!material::isSampler(p.type)) continue;
        m_descriptorSetLayoutBindings.push_back({
            .binding = p.binding,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .stageFlags = kAllStages,
        });
    }
    if (!m_descriptorSetLayoutBindings.empty()) {
        m_descriptorSetLayout = renderApi->createDescriptorSetLayout(m_descriptorSetLayoutBindings);
    }
}

ProgramHandle Material::getProgram(material::Variant variant) const {
    const material::Variant filtered { material::Variant::type_t(variant.key & ~m_package.variantFilter) };

    auto& program = m_programs[filtered.key];
    if (program || m_missingVariants.test(filtered.key)) {
        return program;
    }

    auto vertex = m_package.getShader(material::ShaderStage::Vertex, filtered);
    auto fragment = m_package.getShader(material::ShaderStage::Fragment, filtered);
    if (vertex.empty() || fragment.empty()) {
        std::cerr << "Material '" << m_package.name << "' has no shaders for variant 0x" << std::hex
                  << uint32_t(filtered.key) << std::dec << std::endl;
        m_missingVariants.set(filtered.key);
        return {};
    }

    program = m_renderApi->createProgram(ShaderDescription {
        .vertexShader = toShaderCode(vertex),
        .fragmentShader = toShaderCode(fragment),
        .raster = m_raster,
        .layout = {
            DescriptorSetLayoutBindings::perView(),
            DescriptorSetLayoutBindings::perObject(),
            m_descriptorSetLayoutBindings,
        },
    });
    return program;
}

void MaterialLoader::load(const std::string& key, LoadContext<Material>& ctx) {
    auto data = os::readFile(key);
    std::string error;
    auto package = material::MaterialPackage::deserialize({ reinterpret_cast<const uint8_t*>(data.data()), data.size() }, error);
    if (!package) {
        throw std::runtime_error(key + ": " + error);
    }
    ctx.construct(m_renderApi, std::move(*package));
}

}
