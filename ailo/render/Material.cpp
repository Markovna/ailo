#include "Material.h"

#include "Renderer.h"
#include "RenderPrimitive.h"

ailo::Material::Material(Engine& engine, Shader& shader)
    : m_shader(&shader) {
    if (auto descriptorSetLayout = shader.getDescriptorSetLayout(std::to_underlying(DescriptorSetBindingPoints::PER_MATERIAL))) {
        m_descriptorSet = engine.getRenderAPI()->createDescriptorSet(descriptorSetLayout);
    }
}

void ailo::Material::setTexture(uint32_t binding, Texture* texture) {
    m_textures[binding] = texture;
    pendingBindings[binding] = true;
}

void ailo::Material::setBuffer(uint32_t binding, BufferObject* buffer) {
    m_buffers[binding] = buffer;
    pendingBindings[binding] = true;
}

void ailo::Material::updateTextures(RenderAPI& renderAPI) {
    for (auto& [binding, texture] : m_textures) {
        if(!pendingBindings.test(binding)) {
            continue;
        }
        renderAPI.updateDescriptorSetTexture(m_descriptorSet, texture->getHandle(), binding);
        pendingBindings.reset(binding);
    }
}

void ailo::Material::updateBuffers(RenderAPI& renderAPI) {
    for (auto& [binding, buffer] : m_buffers) {
        if(!pendingBindings.test(binding)) {
            continue;
        }
        renderAPI.updateDescriptorSetBuffer(m_descriptorSet, buffer->getHandle(), binding);
        pendingBindings.reset(binding);
    }
}

void ailo::Material::bindDescriptorSet(RenderAPI& renderAPI) {
    if (m_descriptorSet) {
        renderAPI.bindDescriptorSet(m_descriptorSet, std::to_underlying(DescriptorSetBindingPoints::PER_MATERIAL));
    }
}

void ailo::Material::destroy(Engine& engine) {
    engine.getRenderAPI()->destroyDescriptorSet(m_descriptorSet);
}
