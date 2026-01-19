#pragma once
#include "Engine.h"
#include "RenderAPI.h"

namespace ailo {

class Texture {
public:
    Texture(Engine&, vk::Format, uint32_t width, uint32_t height, vk::Filter = vk::Filter::eLinear, uint8_t levels = 1);
    void updateImage(Engine&, const void* data, size_t dataSize, uint32_t width, uint32_t height, uint32_t xOffset, uint32_t yOffset);
    void updateImage(Engine&, const void* data, size_t dataSize);
    void generateMipmaps(Engine&);
    void destroy(Engine&);

    TextureHandle getHandle() const { return m_handle; }

    static std::unique_ptr<Texture> createFromFile(Engine&, const std::string& path, bool mipmaps = false);

private:
    TextureHandle m_handle;
};

}
