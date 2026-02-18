#include "Texture.h"

#include <iostream>
#include <ostream>

#include "stb_image/stb_image.h"

namespace ailo {

Texture::Texture(Engine& engine, TextureType type, vk::Format format, TextureUsage usage, uint32_t width, uint32_t height, uint8_t levels)
    : m_handle(engine.getRenderAPI()->createTexture(type, format, usage, width, height, levels)), m_levels(levels) {
}

void Texture::updateImage(Engine& engine, const void* data, size_t dataSize, uint32_t width, uint32_t height, uint32_t xOffset,
    uint32_t yOffset, uint32_t baseLayer, uint32_t layerCount) {
    engine.getRenderAPI()->updateTextureImage(m_handle, data, dataSize, width, height, xOffset, yOffset, baseLayer, layerCount);
}

void Texture::updateImage(Engine& engine, const void* data, size_t dataSize) {
    engine.getRenderAPI()->updateTextureImage(m_handle, data, dataSize);
}

void Texture::generateMipmaps(Engine& engine) {
    engine.getRenderAPI()->generateMipmaps(m_handle);
}

void Texture::destroy(Engine& engine) {
    engine.getRenderAPI()->destroyTexture(m_handle);
}

std::unique_ptr<Texture> Texture::createFromFile(Engine& engine, const std::string& path, vk::Format format, bool mipmaps) {
    bool isHdr = stbi_is_hdr(path.c_str());

    std::unique_ptr<Texture> tex;
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
        tex = std::make_unique<Texture>(engine, TextureType::TEXTURE_2D, format, TextureUsage::Sampled, texWidth, texHeight, mipmaps ? mipLevels : 1);
        tex->updateImage(engine, pixels, texWidth * texHeight * desiredChannels);
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
        tex = std::make_unique<Texture>(engine, TextureType::TEXTURE_2D, format, TextureUsage::Sampled, texWidth, texHeight, mipmaps ? mipLevels : 1);
        tex->updateImage(engine, pixels, texWidth * texHeight * desiredChannels * sizeof(float));
        stbi_image_free(pixels);
    }

    if (mipmaps) {
        tex->generateMipmaps(engine);
    }

    return tex;
}
}
