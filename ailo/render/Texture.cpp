#include "Texture.h"

#include <filesystem>

#include "render/texture/TexturePackage.h"
#include "stb_image/stb_image.h"
#include "utils/Log.h"

namespace ailo {

Texture::Texture(RenderAPI* renderApi, TextureType type, TextureFormat format, TextureUsage usage, uint32_t width, uint32_t height, uint8_t levels)
    : m_handle(renderApi->createTexture(type, format, usage, width, height, levels)), m_levels(levels) {
}

void Texture::updateImage(RenderAPI* renderApi, const void* data, size_t dataSize, uint32_t width, uint32_t height, uint32_t xOffset,
                          uint32_t yOffset, uint32_t baseLayer, uint32_t layerCount, uint32_t level) {
    renderApi->updateTextureImage(m_handle, data, dataSize, width, height, xOffset, yOffset, baseLayer, layerCount, level);
}

void Texture::updateImage(RenderAPI* renderApi, const void* data, size_t dataSize) {
    renderApi->updateTextureImage(m_handle, data, dataSize);
}

void Texture::generateMipmaps(RenderAPI* renderApi) {
    renderApi->generateMipmaps(m_handle);
}

Texture Texture::fromPackage(RenderAPI* renderApi, const texture::TexturePackage& pkg) {
    Texture tex(renderApi, pkg.type, pkg.format, TextureUsage::Sampled, pkg.width, pkg.height,
                pkg.allocatedLevels());

    // Levels are stored level-major, so all layers of a level are contiguous and upload in one copy.
    for (uint32_t level = 0; level < pkg.levels; level++) {
        auto image = pkg.image(level, 0);
        tex.updateImage(renderApi, image.data(), image.size() * pkg.layers(), pkg.levelWidth(level),
                        pkg.levelHeight(level), 0, 0, 0, pkg.layers(), level);
    }

    if (pkg.generateMipmaps) {
        tex.generateMipmaps(renderApi);
    }
    return tex;
}

void TexturePackageLoader::load(const std::string& key, std::span<const uint8_t> bytes, LoadContext<Texture>& ctx) {
    std::string error;
    auto pkg = texture::TexturePackage::deserialize(bytes, error);
    if (!pkg) {
        throw std::runtime_error("texture '" + key + "': " + error);
    }

    ctx.construct(Texture::fromPackage(m_renderApi, *pkg));
}

// Without a package there are no options: 8-bit images are sRGB color. Data textures (normal, metallic/roughness)
// need a .tex file with "srgb": false.
void TextureRawLoader::load(const std::string& key, std::span<const uint8_t> bytes, LoadContext<Texture>& ctx) {
    const bool mipmaps = true;
    const auto* data = reinterpret_cast<const stbi_uc*>(bytes.data());
    const int size = static_cast<int>(bytes.size());
    bool isHdr = stbi_is_hdr_from_memory(data, size);

    TextureFormat format = isHdr ? TextureFormat::RGBA32F : TextureFormat::RGBA8_SRGB;

    Texture* tex;
    if (!isHdr) {
        // Load texture
        int texWidth, texHeight, texChannels;
        int desiredChannels = STBI_rgb_alpha;

        stbi_uc* pixels = stbi_load_from_memory(data, size, &texWidth, &texHeight, &texChannels, desiredChannels);
        if (!pixels) {
            log::error("Failed to load texture image at '{}'! Reason {}", key, stbi_failure_reason());
            throw std::runtime_error("failed to load texture image!");
        }

        uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;
        tex = &ctx.construct(m_renderApi, TextureType::Texture2D, format, TextureUsage::Sampled, texWidth, texHeight, mipmaps ? mipLevels : 1);
        tex->updateImage(m_renderApi, pixels, texWidth * texHeight * desiredChannels);
        stbi_image_free(pixels);

    } else {
        int texWidth, texHeight, texChannels;
        int desiredChannels = STBI_rgb_alpha;

        float* pixels = stbi_loadf_from_memory(data, size, &texWidth, &texHeight, &texChannels, desiredChannels);
        if (!pixels) {
            log::error("Failed to load texture image at '{}'! Reason {}", key, stbi_failure_reason());
            throw std::runtime_error("failed to load texture image!");
        }

        uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;
        tex = &ctx.construct(m_renderApi, TextureType::Texture2D, format, TextureUsage::Sampled, texWidth, texHeight, mipmaps ? mipLevels : 1);
        tex->updateImage(m_renderApi, pixels, texWidth * texHeight * desiredChannels * sizeof(float));
        stbi_image_free(pixels);
    }

    if (mipmaps) {
        tex->generateMipmaps(m_renderApi);
    }
}

}
