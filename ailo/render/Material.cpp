#include "Material.h"

#include "Renderer.h"

ailo::Material::Material(RenderAPI* renderApi, asset_ptr<Shader>& shader)
    : m_shader(shader) {
    if (auto descriptorSetLayout = shader->getDescriptorSetLayout(std::to_underlying(DescriptorSetBindingPoints::PER_MATERIAL))) {
        m_descriptorSet = renderApi->createDescriptorSet(descriptorSetLayout);
    }
}

void ailo::Material::setTexture(uint32_t binding, asset_ptr<Texture> texture) {
    m_textures[binding] = texture;
    m_pendingBindings[binding] = true;
}

void ailo::Material::setBuffer(uint32_t binding, Shared<gpu::Buffer> buffer) {
    m_buffers[binding] = std::move(buffer);
    m_pendingBindings[binding] = true;
}

void ailo::Material::updateTextures(RenderAPI& renderAPI) {
    for (auto& [binding, texture] : m_textures) {
        if(!m_pendingBindings.test(binding)) {
            continue;
        }
        renderAPI.updateDescriptorSetTexture(m_descriptorSet, texture->getHandle(), binding);
        m_pendingBindings.reset(binding);
    }
}

void ailo::Material::updateBuffers(RenderAPI& renderAPI) {
    for (auto& [binding, buffer] : m_buffers) {
        if(!m_pendingBindings.test(binding)) {
            continue;
        }
        renderAPI.updateDescriptorSetBuffer(m_descriptorSet, buffer.getHandle(), binding);
        m_pendingBindings.reset(binding);
    }
}

void ailo::Material::bindDescriptorSet(RenderAPI& renderAPI) const {
    if (m_descriptorSet) {
        renderAPI.bindDescriptorSet(m_descriptorSet, std::to_underlying(DescriptorSetBindingPoints::PER_MATERIAL));
    }
}

ailo::asset_ptr<ailo::Material> ailo::Material::create(AssetManager* loader, RenderAPI* renderApi, asset_ptr<Shader> shader) {
    return loader->emplace<Material>(renderApi, shader);
}
