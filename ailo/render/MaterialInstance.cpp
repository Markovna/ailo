#include "MaterialInstance.h"

#include <cstring>
#include <iostream>

#include "Renderer.h"

namespace ailo {

namespace {

const char* defaultTexturePath(const material::MaterialParameter& p) {
    switch (p.samplerDefault) {
        case material::SamplerDefault::White:
            return "builtin://textures/white";
        case material::SamplerDefault::Black:
            return p.type == material::ParameterType::SamplerCubemap ? "builtin://textures/black_cube" : "builtin://textures/black";
        case material::SamplerDefault::Normal:
            return "builtin://textures/normal@norm";
    }
    return "builtin://textures/white";
}

}

MaterialInstance::MaterialInstance(RenderAPI* renderApi, AssetManager& assets, asset_ptr<Material> material)
    : m_material(std::move(material)) {
    const auto& package = m_material->getPackage();

    if (auto layout = m_material->getDescriptorSetLayout()) {
        m_descriptorSet = renderApi->createDescriptorSet(layout);
    }

    if (package.uniformBlockSize > 0) {
        m_uniforms.assign(package.uniformBlockSize, 0);
        for (const auto& p : package.parameters) {
            if (!material::isSampler(p.type)) {
                std::memcpy(m_uniforms.data() + p.offset, p.defaultValue.data(), p.defaultValue.size());
            }
        }
        m_uniformBuffer = renderApi->createBuffer(BufferBinding::UNIFORM, package.uniformBlockSize);
        renderApi->updateDescriptorSetBuffer(m_descriptorSet, m_uniformBuffer, material::kMaterialParamsBinding);
    }

    for (const auto& p : package.parameters) {
        if (material::isSampler(p.type)) {
            m_samplers.push_back({ .binding = p.binding, .texture = assets.load<Texture>(defaultTexturePath(p)) });
        }
    }
}

void MaterialInstance::setParameter(std::string_view name, asset_ptr<Texture> texture, const SamplerParams& sampler) {
    const auto* p = m_material->findParameter(name);
    if (!p || !material::isSampler(p->type)) {
        std::cerr << "Material '" << m_material->getName() << "' has no sampler parameter '" << name << "'" << std::endl;
        return;
    }
    if (!texture) {
        return;
    }

    for (auto& slot : m_samplers) {
        if (slot.binding == p->binding) {
            slot.texture = std::move(texture);
            slot.params = sampler;
            slot.dirty = true;
        }
    }
}

void MaterialInstance::setUniform(std::string_view name, material::ParameterType type, std::span<const uint32_t> words, uint32_t firstElement) {
    const auto* p = m_material->findParameter(name);
    if (!p || material::isSampler(p->type)) {
        std::cerr << "Material '" << m_material->getName() << "' has no uniform parameter '" << name << "'" << std::endl;
        return;
    }
    if (p->type != type) {
        std::cerr << "Material '" << m_material->getName() << "': parameter '" << name << "' is "
                  << material::toString(p->type) << ", not " << material::toString(type) << std::endl;
        return;
    }

    const uint32_t components = material::componentCount(type);
    const uint32_t elements = uint32_t(words.size()) / components;
    const uint32_t arraySize = std::max(p->arraySize, 1u);
    if (firstElement + elements > arraySize) {
        std::cerr << "Material '" << m_material->getName() << "': writing elements [" << firstElement << ", "
                  << firstElement + elements << ") of '" << name << "', which has " << arraySize << std::endl;
        return;
    }

    // std140: array elements are 16-byte aligned, and so are matrix columns.
    const uint32_t stride = p->size / arraySize;
    const bool matrix = type == material::ParameterType::Float3x3 || type == material::ParameterType::Float4x4;
    const uint32_t rows = type == material::ParameterType::Float3x3 ? 3 : type == material::ParameterType::Float4x4 ? 4 : components;

    for (uint32_t e = 0; e < elements; e++) {
        for (uint32_t i = 0; i < components; i++) {
            const uint32_t offset = p->offset + (firstElement + e) * stride + (matrix ? (i / rows) * 16 + (i % rows) * 4 : i * 4);
            std::memcpy(m_uniforms.data() + offset, &words[e * components + i], sizeof(uint32_t));
        }
    }
    m_uniformsDirty = true;
}

void MaterialInstance::commit(RenderAPI& renderApi) {
    if (m_uniformsDirty && m_uniformBuffer) {
        renderApi.updateBuffer(m_uniformBuffer, m_uniforms.data(), m_uniforms.size());
    }
    m_uniformsDirty = false;

    for (auto& slot : m_samplers) {
        if (slot.dirty) {
            renderApi.updateDescriptorSetTexture(m_descriptorSet, slot.texture->getHandle(), slot.binding, slot.params);
            slot.dirty = false;
        }
    }
}

void MaterialInstance::bind(RenderAPI& renderApi) const {
    if (m_descriptorSet) {
        renderApi.bindDescriptorSet(m_descriptorSet, std::to_underlying(DescriptorSetBindingPoints::PER_MATERIAL));
    }
}

asset_ptr<MaterialInstance> MaterialInstance::create(AssetManager& assets, RenderAPI& renderApi, const asset_ptr<Material>& material) {
    return assets.emplace<MaterialInstance>(&renderApi, assets, material);
}

}
