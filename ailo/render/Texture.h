#pragma once
#include <filesystem>

#include "RenderAPI.h"
#include "assets/AssetServer.h"

namespace ailo {

namespace texture { struct TexturePackage; }

class Texture {
public:
    Texture(RenderAPI*, TextureType, vk::Format, TextureUsage, uint32_t width, uint32_t height, uint8_t levels = 1);

    void updateImage(RenderAPI*, const void* data, size_t dataSize, uint32_t width, uint32_t height, uint32_t xOffset, uint32_t yOffset, uint32_t baseLayer = 0, uint32_t layerCount = 1, uint32_t level = 0);
    void updateImage(RenderAPI*, const void* data, size_t dataSize);
    void generateMipmaps(RenderAPI*);

    TextureHandle getHandle() const { return m_handle; }
    uint32_t getLevels() const { return m_levels; }

    static AssetPtr<Texture> loadCubemap(AssetStorage<Texture>&, RenderAPI*, const std::string& paths, vk::Format format, bool loadMipmaps = false);
    // An unnamed texture, e.g. one embedded in a model package.
    static AssetPtr<Texture> fromPackage(AssetStorage<Texture>&, RenderAPI*, const texture::TexturePackage&);

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
    void loadPackage(const std::filesystem::path& packagePath, LoadContext<Texture>& ctx);
    void loadImage(const std::string& key, LoadContext<Texture>& ctx);

    RenderAPI* m_renderApi;
};

}
