#include "Texture.h"

#include <filesystem>

#include "Engine.h"

#include <iostream>
#include <ostream>

#include "stb_image/stb_image.h"

namespace ailo {

Texture::Texture(Engine& engine, TextureType type, vk::Format format, TextureUsage usage, uint32_t width, uint32_t height, uint8_t levels)
    : m_handle(engine.getRenderAPI()->createTexture(type, format, usage, width, height, levels)), m_levels(levels) {
}

void Texture::updateImage(Engine& engine, const void* data, size_t dataSize, uint32_t width, uint32_t height, uint32_t xOffset,
    uint32_t yOffset, uint32_t baseLayer, uint32_t layerCount, uint32_t level) {
    engine.getRenderAPI()->updateTextureImage(m_handle, data, dataSize, width, height, xOffset, yOffset, baseLayer, layerCount, level);
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

asset_ptr<Texture> Texture::load(Engine& engine, const std::string& path, vk::Format format, bool mipmaps) {
    auto ptr = engine.getAssetManager()->get<Texture>(path);
    if (ptr) { return ptr; }

    bool isHdr = stbi_is_hdr(path.c_str());

    asset_ptr<Texture> tex;
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
        tex = engine.getAssetManager()->emplace<Texture>(path, engine, TextureType::TEXTURE_2D, format, TextureUsage::Sampled, texWidth, texHeight, mipmaps ? mipLevels : 1);
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
        tex = engine.getAssetManager()->emplace<Texture>(path, engine, TextureType::TEXTURE_2D, format, TextureUsage::Sampled, texWidth, texHeight, mipmaps ? mipLevels : 1);
        tex->updateImage(engine, pixels, texWidth * texHeight * desiredChannels * sizeof(float));
        stbi_image_free(pixels);
    }

    if (mipmaps) {
        tex->generateMipmaps(engine);
    }

    return tex;
}

asset_ptr<Texture> Texture::loadCubemap(Engine& engine, const std::string& path, vk::Format format, bool loadMipmaps) {
    const char* suffixes[] = { "_px", "_nx", "_py", "_ny", "_pz", "_nz" };
    std::filesystem::path p(path);
    std::string extension(p.extension().string());
    p.replace_extension("");

    bool isHdr = format == vk::Format::eR32G32B32A32Sfloat;
    asset_ptr<Texture> tex;

    auto loadFace = [&](const std::string& face_path, size_t face, uint32_t mip) {
        int texChannels, texWidth, texHeight;
        int desiredChannels = STBI_rgb_alpha;
        void* pixels;
        uint32_t byteSize;

        if (isHdr) {
            pixels = stbi_loadf(face_path.c_str(), &texWidth, &texHeight, &texChannels, desiredChannels);
            byteSize = texWidth * texHeight * desiredChannels * sizeof(float);
        } else {
            pixels = stbi_load(face_path.c_str(), &texWidth, &texHeight, &texChannels, desiredChannels);
            byteSize = texWidth * texHeight * desiredChannels * sizeof(uint8_t);
        }

        if (!pixels) {
            std::cerr << "Failed to load texture image at '" << face_path << "'! Reason " << stbi_failure_reason() << std::endl;
            throw std::runtime_error("failed to load texture image!");
        }

        return std::tuple{ pixels, byteSize, texWidth, texHeight };
    };

    if (loadMipmaps) {
        uint32_t mipLevels = 0;
        while (std::filesystem::exists(p.string() + "_m" + std::to_string(mipLevels) + suffixes[0] + extension))
            mipLevels++;

        if (mipLevels == 0) {
            std::cerr << "No mip map files found for '" << path << "'!" << std::endl;
            throw std::runtime_error("failed to find mip map files!");
        }

        for (uint32_t mip = 0; mip < mipLevels; mip++) {
            for (size_t face = 0; face < 6; face++) {
                auto face_path = p.string() + "_m" + std::to_string(mip) + suffixes[face] + extension;
                auto [pixels, byteSize, texWidth, texHeight] = loadFace(face_path, face, mip);

                if (!tex)
                    tex = engine.getAssetManager()->emplace<Texture>(path, engine, TextureType::TEXTURE_CUBEMAP, format, TextureUsage::Sampled, texWidth, texHeight, mipLevels);

                tex->updateImage(engine, pixels, byteSize, texWidth, texHeight, 0, 0, face, 1, mip);
                stbi_image_free(pixels);
            }
        }
    } else {
        for (size_t face = 0; face < 6; face++) {
            auto face_path = p.string() + suffixes[face] + extension;
            auto [pixels, byteSize, texWidth, texHeight] = loadFace(face_path, face, 0);

            if (!tex) {
                constexpr int MAX_MIP_LEVELS = 4;
                tex = engine.getAssetManager()->emplace<Texture>(face_path, engine, TextureType::TEXTURE_CUBEMAP, format, TextureUsage::Sampled, texWidth, texHeight, MAX_MIP_LEVELS);
            }

            tex->updateImage(engine, pixels, byteSize, texWidth, texHeight, 0, 0, face, 1);
            stbi_image_free(pixels);
        }

        tex->generateMipmaps(engine);
    }

    return tex;
}

asset_ptr<Texture> Texture::fromEmbedded(Engine& engine, const void* data, size_t dataSize, vk::Format format, uint32_t width,
    uint32_t height, uint8_t levels) {
    asset_ptr<Texture> texture = engine.getAssetManager()->emplace<Texture>(assets::no_path{}, engine, TextureType::TEXTURE_2D, format, TextureUsage::Sampled, width, height, levels);
    texture->updateImage(engine, data, dataSize, width, height, 0, 0, 0, 1);

    if (levels > 1) {
        texture->generateMipmaps(engine);
    }
    return texture;
}

asset_ptr<Texture> Texture::fromEmbeddedCompressed(Engine& engine, const void* data, size_t dataSize, vk::Format format) {
    int texChannels;
    int texWidth, texHeight;
    int desiredChannels = STBI_rgb_alpha;

    unsigned char* pixels = stbi_load_from_memory(static_cast<stbi_uc const*>(data), dataSize, &texWidth, &texHeight, &texChannels, desiredChannels);
    auto byteSize = texWidth * texHeight * desiredChannels * sizeof(uint8_t);

    asset_ptr<Texture> texture = engine.getAssetManager()->emplace<Texture>(assets::no_path{}, engine, TextureType::TEXTURE_2D, format, TextureUsage::Sampled, texWidth, texHeight);
    texture->updateImage(engine, pixels, byteSize, texWidth, texHeight, 0, 0, 0, 1);

    stbi_image_free(pixels);
    return texture;
}
}
