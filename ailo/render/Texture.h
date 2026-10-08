#pragma once
#include "RenderAPI.h"
#include "assets/AssetServer.h"

namespace ailo {

class Texture {
public:
    Texture(RenderAPI*, TextureType, vk::Format, TextureUsage, uint32_t width, uint32_t height, uint8_t levels = 1);

    void updateImage(RenderAPI*, const void* data, size_t dataSize, uint32_t width, uint32_t height, uint32_t xOffset, uint32_t yOffset, uint32_t baseLayer = 0, uint32_t layerCount = 1, uint32_t level = 0);
    void updateImage(RenderAPI*, const void* data, size_t dataSize);
    void generateMipmaps(RenderAPI*);

    TextureHandle getHandle() const { return m_handle; }
    uint32_t getLevels() const { return m_levels; }

    static void load(LoadContext<Texture>&, RenderAPI*, const std::string& key, bool mipmaps = false);
    static AssetPtr<Texture> loadCubemap(AssetStorage<Texture>&, RenderAPI*, const std::string& paths, vk::Format format, bool loadMipmaps = false);
    // Embedded textures are stored under a unique key derived from `key`.
    static AssetPtr<Texture> fromEmbedded(AssetStorage<Texture>&, RenderAPI*, const std::string& key, const void* data, size_t dataSize, vk::Format format, uint32_t width, uint32_t height, uint8_t levels = 1);
    static AssetPtr<Texture> fromEmbeddedCompressed(AssetStorage<Texture>&, RenderAPI*, const std::string& key, const void* data, size_t dataSize, vk::Format format);

private:
    Unique<gpu::Texture> m_handle;
    uint8_t m_levels;
};

class TextureLoader : public AssetLoader<Texture> {
public:
    TextureLoader(RenderAPI* renderApi) : m_renderApi(renderApi) {}

protected:
    void load(const std::string& key, LoadContext<Texture>& ctx) override;

private:
    RenderAPI* m_renderApi;
};

}
