#include "Texture.h"

#include <filesystem>


#include <iostream>
#include <ostream>

#include "FileIO.h"
#include "render/texture/TexturePackage.h"
#include "stb_image/stb_image.h"

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
    }
    throw std::runtime_error("unknown texture package format");
}

TextureType toTextureType(texture::TextureType type) {
    return type == texture::TextureType::Cubemap ? TextureType::TEXTURE_CUBEMAP : TextureType::TEXTURE_2D;
}

void upload(Texture& tex, RenderAPI* renderApi, const texture::TexturePackage& pkg) {
    // Levels are stored level-major, so all layers of a level are contiguous and upload in one copy.
    for (uint32_t level = 0; level < pkg.levels; level++) {
        auto image = pkg.image(level, 0);
        tex.updateImage(renderApi, image.data(), image.size() * pkg.layers(), pkg.levelWidth(level),
                        pkg.levelHeight(level), 0, 0, 0, pkg.layers(), level);
    }

    if (pkg.generateMipmaps) {
        tex.generateMipmaps(renderApi);
    }
}

}

AssetPtr<Texture> Texture::fromPackage(AssetStorage<Texture>& storage, RenderAPI* renderApi, const texture::TexturePackage& pkg) {
    auto tex = storage.emplace(noname_t{}, renderApi, toTextureType(pkg.type), toVkFormat(pkg.format), TextureUsage::Sampled,
                               pkg.width, pkg.height, pkg.allocatedLevels());
    upload(*tex, renderApi, pkg);
    return tex;
}

void TextureLoader::load(const std::string& key, LoadContext<Texture>& ctx) {
    std::filesystem::path packagePath = key + ".texpack";
    if (std::filesystem::exists(packagePath)) {
        loadPackage(packagePath, ctx);
    } else {
        loadImage(key, ctx);
    }
}

void TextureLoader::loadPackage(const std::filesystem::path& packagePath, LoadContext<Texture>& ctx) {
    std::string data;
    if (!fileio::readFile(packagePath, data)) {
        throw std::runtime_error("texture '" + packagePath.generic_string() + "': cannot read file");
    }

    std::string error;
    auto pkg = texture::TexturePackage::deserialize({ reinterpret_cast<const uint8_t*>(data.data()), data.size() }, error);
    if (!pkg) {
        throw std::runtime_error("texture '" + packagePath.generic_string() + "': " + error);
    }

    Texture& tex = ctx.construct(m_renderApi, toTextureType(pkg->type), toVkFormat(pkg->format), TextureUsage::Sampled,
                                 pkg->width, pkg->height, pkg->allocatedLevels());
    upload(tex, m_renderApi, *pkg);
}

// Without a package there are no options: 8-bit images are sRGB color. Data textures (normal, metallic/roughness)
// need a .tex file with "srgb": false.
void TextureLoader::loadImage(const std::string& key, LoadContext<Texture>& ctx) {
    const bool mipmaps = true;
    const std::string& path = key;
    bool isHdr = stbi_is_hdr(path.c_str());

    vk::Format format = isHdr ? vk::Format::eR32G32B32A32Sfloat : vk::Format::eR8G8B8A8Srgb;

    Texture* tex;
    if (!isHdr) {
        // Load texture
        int texWidth, texHeight, texChannels;
        int desiredChannels = STBI_rgb_alpha;

        stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, desiredChannels);
        if (!pixels) {
            std::cerr << "Failed to load texture image at '" << path << "'! Reason " << stbi_failure_reason() << std::endl;
            throw std::runtime_error("failed to load texture image!");
        }

        uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;
        tex = &ctx.construct(m_renderApi, TextureType::TEXTURE_2D, format, TextureUsage::Sampled, texWidth, texHeight, mipmaps ? mipLevels : 1);
        tex->updateImage(m_renderApi, pixels, texWidth * texHeight * desiredChannels);
        stbi_image_free(pixels);

    } else {
        int texWidth, texHeight, texChannels;
        int desiredChannels = STBI_rgb_alpha;

        float* pixels = stbi_loadf(path.c_str(), &texWidth, &texHeight, &texChannels, desiredChannels);
        if (!pixels) {
            std::cerr << "Failed to load texture image at '" << path << "'! Reason " << stbi_failure_reason() << std::endl;
            throw std::runtime_error("failed to load texture image!");
        }

        uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;
        tex = &ctx.construct(m_renderApi, TextureType::TEXTURE_2D, format, TextureUsage::Sampled, texWidth, texHeight, mipmaps ? mipLevels : 1);
        tex->updateImage(m_renderApi, pixels, texWidth * texHeight * desiredChannels * sizeof(float));
        stbi_image_free(pixels);
    }

    if (mipmaps) {
        tex->generateMipmaps(m_renderApi);
    }
}

}
