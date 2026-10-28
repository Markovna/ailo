#pragma once
#include <array>
#include <string_view>

#include "RenderAPI.h"
#include "assets/AssetServer.h"

namespace ailo {

namespace texture { struct TexturePackage; }

class Texture {
public:
    Texture(RenderAPI*, TextureType, TextureFormat, TextureUsage, uint32_t width, uint32_t height, uint8_t levels = 1);

    void updateImage(RenderAPI*, const void* data, size_t dataSize, uint32_t width, uint32_t height, uint32_t xOffset, uint32_t yOffset, uint32_t baseLayer = 0, uint32_t layerCount = 1, uint32_t level = 0);
    void updateImage(RenderAPI*, const void* data, size_t dataSize);
    void generateMipmaps(RenderAPI*);

    TextureHandle getHandle() const { return m_handle; }
    uint32_t getLevels() const { return m_levels; }

    static Texture fromPackage(RenderAPI*, const texture::TexturePackage&);

private:
    Unique<gpu::Texture> m_handle;
    uint8_t m_levels;
};

// Reads a texture package that texc compiled from a .tex file, keeping its name.
class TexturePackageLoader : public AssetLoader<Texture> {
public:
    explicit TexturePackageLoader(RenderAPI* renderApi) : m_renderApi(renderApi) {}

    constexpr std::span<const std::string_view> extensions() const override { return kExtensions; }

protected:
    void load(const std::string& key, std::span<const uint8_t> bytes, LoadContext<Texture>& ctx) override;

private:
    static constexpr std::array<std::string_view, 1> kExtensions = { ".tex" };

    RenderAPI* m_renderApi;
};

// Decodes an image file with stb_image.
class TextureRawLoader : public AssetLoader<Texture> {
public:
    explicit TextureRawLoader(RenderAPI* renderApi) : m_renderApi(renderApi) {}

    constexpr std::span<const std::string_view> extensions() const override { return kExtensions; }

protected:
    void load(const std::string& key, std::span<const uint8_t> bytes, LoadContext<Texture>& ctx) override;

private:
    static constexpr std::array<std::string_view, 6> kExtensions = { ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr" };

    RenderAPI* m_renderApi;
};

}
