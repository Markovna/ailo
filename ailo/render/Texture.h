#pragma once
#include "RenderAPI.h"
#include "../assets/Assets.h"

namespace ailo {
class Engine;

class Texture : public Asset {
public:
    Texture(RenderAPI*, TextureType, vk::Format, TextureUsage, uint32_t width, uint32_t height, uint8_t levels = 1);
    ~Texture();

    void updateImage(RenderAPI*, const void* data, size_t dataSize, uint32_t width, uint32_t height, uint32_t xOffset, uint32_t yOffset, uint32_t baseLayer = 0, uint32_t layerCount = 1, uint32_t level = 0);
    void updateImage(RenderAPI*, const void* data, size_t dataSize);
    void generateMipmaps(RenderAPI*);
    void release();

    TextureHandle getHandle() const { return m_handle; }
    uint32_t getLevels() const { return m_levels; }

    static void load(LoadContext<Texture>&, RenderAPI*, const std::string& key, bool mipmaps = false);
    static asset_ptr<Texture> loadCubemap(AssetManager*, RenderAPI*, const std::string& paths, vk::Format format, bool loadMipmaps = false);
    static asset_ptr<Texture> fromEmbedded(AssetManager*, RenderAPI*, const void* data, size_t dataSize, vk::Format format, uint32_t width, uint32_t height, uint8_t levels = 1);
    static asset_ptr<Texture> fromEmbeddedCompressed(AssetManager*, RenderAPI*, const void* data, size_t dataSize, vk::Format format);

private:
    TextureHandle m_handle;
    uint8_t m_levels;
    RenderAPI* m_renderApi;
};

class TextureLoader : public AssetLoader<Texture> {
public:
    TextureLoader(RenderAPI* renderApi) : m_renderApi(renderApi) {}

protected:
    void load(LoadContext<Texture>& ctx, const std::string& path) override;

private:
    RenderAPI* m_renderApi;
};

}
