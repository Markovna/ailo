#include "MaterialInstance.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "DefaultAssetFactory.h"
#include "Renderer.h"
#include "material/MaterialInstancePackage.h"
#include "utils/Log.h"

namespace ailo {

namespace {

const char* defaultTexturePath(const material::MaterialParameter& p) {
    switch (p.samplerDefault) {
        case material::SamplerDefault::White:
            return textures::kWhite;
        case material::SamplerDefault::Black:
            return p.type == material::ParameterType::SamplerCubemap ? textures::kBlackCube : textures::kBlack;
        case material::SamplerDefault::Normal:
            return textures::kNormal;
    }
    return textures::kWhite;
}

}

MaterialInstance::MaterialInstance(RenderAPI* renderApi, AssetServer& server, AssetPtr<Material> material)
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
            m_samplers.push_back({ .binding = p.binding, .texture = server.load<Texture>(defaultTexturePath(p)) });
        }
    }
}

void MaterialInstance::setParameter(std::string_view name, AssetPtr<Texture> texture, const SamplerParams& sampler) {
    const auto* p = m_material->findParameter(name);
    if (!p || !material::isSampler(p->type)) {
        log::error("Material '{}' has no sampler parameter '{}'", m_material->getName(), name);
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
        log::error("Material '{}' has no uniform parameter '{}'", m_material->getName(), name);
        return;
    }
    if (p->type != type) {
        log::error("Material '{}': parameter '{}' is {}, not {}", m_material->getName(), name,
                   material::toString(p->type), material::toString(type));
        return;
    }

    const uint32_t components = material::componentCount(type);
    const uint32_t elements = uint32_t(words.size()) / components;
    const uint32_t arraySize = std::max(p->arraySize, 1u);
    if (firstElement + elements > arraySize) {
        log::error("Material '{}': writing elements [{}, {}) of '{}', which has {}", m_material->getName(),
                   firstElement, firstElement + elements, name, arraySize);
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

void MaterialInstance::copyParametersFrom(const MaterialInstance& other) {
    for (const auto& p : m_material->getPackage().parameters) {
        const auto* source = other.m_material->findParameter(p.name);
        if (!source || source->type != p.type || source->arraySize != p.arraySize) {
            continue;
        }

        if (!material::isSampler(p.type)) {
            std::memcpy(m_uniforms.data() + p.offset, other.m_uniforms.data() + source->offset, p.size);
            m_uniformsDirty = true;
            continue;
        }

        auto from = std::ranges::find(other.m_samplers, source->binding, &Sampler::binding);
        auto to = std::ranges::find(m_samplers, p.binding, &Sampler::binding);
        to->texture = from->texture;
        to->params = from->params;
        to->dirty = true;
    }
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

AssetPtr<MaterialInstance> MaterialInstance::create(AssetStorage<MaterialInstance>& storage, AssetServer& server,
                                                    RenderAPI& renderApi, const AssetPtr<Material>& material) {
    if (!material) return {};

    return storage.emplace(noname_t{}, &renderApi, server, material);
}

void MaterialInstanceLoader::load(const std::string& key, std::span<const uint8_t> bytes, LoadContext<MaterialInstance>& ctx) {
    std::string error;
    auto pkg = material::MaterialInstancePackage::deserialize(bytes, error);
    if (!pkg) {
        throw std::runtime_error(key + ": " + error);
    }

    auto material = ctx.load<Material>(pkg->material);
    if (!material) {
        throw std::runtime_error(key + ": failed to load material '" + pkg->material + "'");
    }

    auto& instance = ctx.construct(m_renderApi, *m_server, std::move(material));
    for (const auto& p : pkg->parameters) {
        instance.setParameter(p.name, p.type, p.value);
    }
    for (const auto& t : pkg->textures) {
        instance.setParameter(t.name, ctx.load<Texture>(t.texture));
    }
}

}
