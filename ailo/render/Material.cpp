#include "Material.h"

#include "Renderer.h"
#include "utils/Log.h"

namespace ailo {

namespace {

ShaderDescription::ShaderCode toShaderCode(std::span<const uint32_t> spirv) {
    auto* bytes = reinterpret_cast<const char*>(spirv.data());
    return { bytes, bytes + spirv.size_bytes() };
}

}

Material::Material(RenderAPI* renderApi, material::MaterialPackage package)
    : m_renderApi(renderApi), m_package(std::move(package)) {
    const auto& raster = m_package.raster;
    m_raster.cullingMode = raster.doubleSided ? CullingMode::None : raster.culling;
    m_raster.inverseFrontFace = false;
    m_raster.depthWriteEnable = raster.depthWrite;
    m_raster.colorWriteEnable = raster.colorWrite;
    m_raster.depthCompareOp = raster.depthCulling ? raster.depthFunc : CompareOp::Always;

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
        log::error("Material '{}' has no shaders for variant 0x{:x}", m_package.name, uint32_t(filtered.key));
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

void MaterialLoader::load(const std::string& key, std::span<const uint8_t> bytes, LoadContext<Material>& ctx) {
    std::string error;
    auto package = material::MaterialPackage::deserialize(bytes, error);
    if (!package) {
        throw std::runtime_error(key + ": " + error);
    }
    ctx.construct(m_renderApi, std::move(*package));
}

}
