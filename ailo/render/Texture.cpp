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

AssetPtr<Texture> Texture::loadCubemap(AssetStorage<Texture>& storage, RenderAPI* renderApi, const std::string& path, vk::Format format, bool loadMipmaps) {
    const char* suffixes[] = { "_px", "_nx", "_py", "_ny", "_pz", "_nz" };
    std::filesystem::path p(path);
    std::string extension(p.extension().string());
    p.replace_extension("");

    bool isHdr = format == vk::Format::eR32G32B32A32Sfloat;
    AssetPtr<Texture> tex;

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
                    tex = storage.emplace(noname_t{}, renderApi, TextureType::TEXTURE_CUBEMAP, format, TextureUsage::Sampled, texWidth, texHeight, mipLevels);

                tex->updateImage(renderApi, pixels, byteSize, texWidth, texHeight, 0, 0, face, 1, mip);
                stbi_image_free(pixels);
            }
        }
    } else {
        for (size_t face = 0; face < 6; face++) {
            auto face_path = p.string() + suffixes[face] + extension;
            auto [pixels, byteSize, texWidth, texHeight] = loadFace(face_path, face, 0);

            if (!tex) {
                constexpr int MAX_MIP_LEVELS = 4;
                tex = storage.emplace(noname_t{}, renderApi, TextureType::TEXTURE_CUBEMAP, format, TextureUsage::Sampled, texWidth, texHeight, MAX_MIP_LEVELS);
            }

            tex->updateImage(renderApi, pixels, byteSize, texWidth, texHeight, 0, 0, face, 1);
            stbi_image_free(pixels);
        }

        tex->generateMipmaps(renderApi);
    }

    return tex;
}

AssetPtr<Texture> Texture::fromEmbedded(AssetStorage<Texture>& storage, RenderAPI* renderApi, const void* data, size_t dataSize, vk::Format format, uint32_t width,
    uint32_t height, uint8_t levels) {
    auto texture = storage.emplace(noname_t{}, renderApi, TextureType::TEXTURE_2D, format, TextureUsage::Sampled, width, height, levels);
    texture->updateImage(renderApi, data, dataSize, width, height, 0, 0, 0, 1);

    if (levels > 1) {
        texture->generateMipmaps(renderApi);
    }
    return texture;
}

AssetPtr<Texture> Texture::fromEmbeddedCompressed(AssetStorage<Texture>& storage, RenderAPI* renderApi, const void* data, size_t dataSize, vk::Format format) {
    int texChannels;
    int texWidth, texHeight;
    int desiredChannels = STBI_rgb_alpha;

    unsigned char* pixels = stbi_load_from_memory(static_cast<stbi_uc const*>(data), dataSize, &texWidth, &texHeight, &texChannels, desiredChannels);
    auto byteSize = texWidth * texHeight * desiredChannels * sizeof(uint8_t);

    auto texture = storage.emplace(noname_t{}, renderApi, TextureType::TEXTURE_2D, format, TextureUsage::Sampled, texWidth, texHeight);
    texture->updateImage(renderApi, pixels, byteSize, texWidth, texHeight, 0, 0, 0, 1);

    stbi_image_free(pixels);
    return texture;
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

}

void TextureLoader::load(const std::string& key, LoadContext<Texture>& ctx) {
    auto first = key.find_first_of('@');
    std::filesystem::path path = key.substr(0, first);
    if (path.extension() == ".tex") {
        if (first != std::string::npos) {
            throw std::runtime_error("texture '" + key + "': tags are not supported for .tex textures, set the options in the .tex file");
        }
        loadPackage(path, ctx);
    } else {
        loadImage(key, ctx);
    }
}

// texc packs <dir>/<name>.tex into <dir>/<name>.texpack in the build folder.
void TextureLoader::loadPackage(const std::filesystem::path& texPath, LoadContext<Texture>& ctx) {
    auto packagePath = texPath;
    packagePath.replace_extension(".texpack");

    std::string data;
    if (!fileio::readFile(packagePath, data)) {
        throw std::runtime_error("texture '" + packagePath.generic_string() + "': cannot read file (is " +
                                 texPath.generic_string() + " added with add_texture?)");
    }

    std::string error;
    auto pkg = texture::TexturePackage::deserialize({ reinterpret_cast<const uint8_t*>(data.data()), data.size() }, error);
    if (!pkg) {
        throw std::runtime_error("texture '" + packagePath.generic_string() + "': " + error);
    }

    Texture& tex = ctx.construct(m_renderApi, toTextureType(pkg->type), toVkFormat(pkg->format), TextureUsage::Sampled,
                                 pkg->width, pkg->height, pkg->allocatedLevels());

    // Levels are stored level-major, so all layers of a level are contiguous and upload in one copy.
    for (uint32_t level = 0; level < pkg->levels; level++) {
        auto image = pkg->image(level, 0);
        tex.updateImage(m_renderApi, image.data(), image.size() * pkg->layers(), pkg->levelWidth(level),
                        pkg->levelHeight(level), 0, 0, 0, pkg->layers(), level);
    }

    if (pkg->generateMipmaps) {
        tex.generateMipmaps(m_renderApi);
    }
}

void TextureLoader::loadImage(const std::string& key, LoadContext<Texture>& ctx) {
    const bool mipmaps = true;
    std::set<std::string> tags;
    auto first = key.find_first_of('@');
    if (first != std::string::npos) {
        size_t start = first, end;
        while ((end = key.find('@', start)) != std::string::npos) {
            tags.insert(key.substr(start, end - start));
            start = end + 1;
        }
        tags.insert(key.substr(start));
    }

    auto path = key.substr(0, first);
    bool isHdr = stbi_is_hdr(path.c_str());

    vk::Format format = isHdr ? vk::Format::eR32G32B32A32Sfloat : vk::Format::eR8G8B8A8Srgb;
    if (tags.contains("norm")) {
        format = vk::Format::eR8G8B8A8Unorm;
    }

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
