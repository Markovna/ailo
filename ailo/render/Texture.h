#pragma once
#include "RenderAPI.h"
#include "common/AssetPool.h"

namespace ailo {

class Engine;

class Texture : public enable_asset_ptr<Texture> {
public:
    Texture(Engine&, TextureType, vk::Format, TextureUsage, uint32_t width, uint32_t height, uint8_t levels = 1);
    void updateImage(Engine&, const void* data, size_t dataSize, uint32_t width, uint32_t height, uint32_t xOffset, uint32_t yOffset, uint32_t baseLayer = 0, uint32_t layerCount = 1);
    void updateImage(Engine&, const void* data, size_t dataSize);
    void generateMipmaps(Engine&);
    void destroy(Engine&);

    TextureHandle getHandle() const { return m_handle; }
    uint32_t getLevels() const { return m_levels; }

    static asset_ptr<Texture> load(Engine&, const std::string& path, vk::Format format, bool mipmaps = false);

private:
    TextureHandle m_handle;
    uint8_t m_levels;
};

}
