#pragma once

#include <memory>
#include "Shader.h"
#include "Texture.h"

namespace ailo {

class BufferObject;

class Material {
public:
    Material(Engine& engine, asset_ptr<Shader>& shader);
    void setTexture(uint32_t binding, asset_ptr<Texture> texture);
    void setBuffer(uint32_t binding, BufferObject* buffer);

    void updateTextures(RenderAPI&);
    void updateBuffers(RenderAPI&);
    void bindDescriptorSet(RenderAPI&) const;

    [[nodiscard]] const Shader* getShader() const { return m_shader.get(); }

    void destroy(Engine&);

private:
    DescriptorSetHandle m_descriptorSet;
    std::unordered_map<uint32_t, asset_ptr<Texture>> m_textures;
    std::unordered_map<uint32_t, BufferObject*> m_buffers;
    std::bitset<64> m_pendingBindings;
    asset_ptr<Shader> m_shader;
};

}
