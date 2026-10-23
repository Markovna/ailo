#include "Texture.h"

#include <filesystem>

#include "render/texture/TexturePackage.h"
#include "stb_image/stb_image.h"
#include "utils/Log.h"

namespace ailo {

Texture::Texture(RenderAPI* renderApi, TextureType type, vk::Format format, TextureUsage usage, uint32_t width, uint32_t height, uint8_t levels)
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

namespace {

vk::Format toVkFormat(texture::TextureFormat format) {
    switch (format) {
        case texture::TextureFormat::RGBA8:      return vk::Format::eR8G8B8A8Unorm;
        case texture::TextureFormat::RGBA8_SRGB: return vk::Format::eR8G8B8A8Srgb;
        case texture::TextureFormat::RGBA32F:    return vk::Format::eR32G32B32A32Sfloat;
        case texture::TextureFormat::RGBA16F:    return vk::Format::eR16G16B16A16Sfloat;
        case texture::TextureFormat::BC1:        return vk::Format::eBc1RgbUnormBlock;
        case texture::TextureFormat::BC1_SRGB:   return vk::Format::eBc1RgbSrgbBlock;
        case texture::TextureFormat::BC3:        return vk::Format::eBc3UnormBlock;
        case texture::TextureFormat::BC3_SRGB:   return vk::Format::eBc3SrgbBlock;
        case texture::TextureFormat::BC4:        return vk::Format::eBc4UnormBlock;
        case texture::TextureFormat::BC5:        return vk::Format::eBc5UnormBlock;
        case texture::TextureFormat::BC6H:       return vk::Format::eBc6HUfloatBlock;
        case texture::TextureFormat::BC7:        return vk::Format::eBc7UnormBlock;
        case texture::TextureFormat::BC7_SRGB:   return vk::Format::eBc7SrgbBlock;
    }
    throw std::runtime_error("unknown texture package format");
}

}

Texture Texture::fromPackage(RenderAPI* renderApi, const texture::TexturePackage& pkg) {
    Texture tex(renderApi, pkg.type, toVkFormat(pkg.format), TextureUsage::Sampled, pkg.width, pkg.height,
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

    vk::Format format = isHdr ? vk::Format::eR32G32B32A32Sfloat : vk::Format::eR8G8B8A8Srgb;

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
