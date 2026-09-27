#pragma once

#include "Shader.h"
#include "Texture.h"

namespace ailo {

class Material : public Asset {
public:
    Material(RenderAPI*, asset_ptr<Shader>& shader);
    void setTexture(uint32_t binding, asset_ptr<Texture> texture);
    void setBuffer(uint32_t binding, Shared<gpu::Buffer> buffer);

    void updateTextures(RenderAPI&);
    void updateBuffers(RenderAPI&);
    void bindDescriptorSet(RenderAPI&) const;

    DescriptorSetHandle getDescriptorSet() const { return m_descriptorSet; }

    [[nodiscard]] const Shader* getShader() const { return m_shader.get(); }

    static asset_ptr<Material> create(AssetManager*, RenderAPI*, asset_ptr<Shader>);

private:
    Unique<gpu::DescriptorSet> m_descriptorSet;
    std::unordered_map<uint32_t, asset_ptr<Texture>> m_textures;
    std::unordered_map<uint32_t, Shared<gpu::Buffer>> m_buffers;
    std::bitset<64> m_pendingBindings;
    asset_ptr<Shader> m_shader;
};

}
