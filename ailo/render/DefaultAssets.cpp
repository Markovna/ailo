#include "DefaultAssets.h"

#include <array>
#include <cstdint>

namespace ailo {

namespace {

using Pixel = std::array<uint8_t, 4>;

AssetPtr<Texture> createTexture2D(RenderAPI* renderApi, AssetStorage<Texture>& textures, const char* key,
                                  vk::Format format, const Pixel& pixel) {
    auto texture = textures.emplace(key, renderApi, TextureType::TEXTURE_2D, format, TextureUsage::Sampled, 1, 1, 1);
    texture->updateImage(renderApi, pixel.data(), pixel.size());
    return texture;
}

AssetPtr<Texture> createCubemap(RenderAPI* renderApi, AssetStorage<Texture>& textures, const char* key,
                                vk::Format format, const Pixel& pixel) {
    auto texture = textures.emplace(key, renderApi, TextureType::TEXTURE_CUBEMAP, format, TextureUsage::Sampled, 1, 1, 1);
    for (uint32_t face = 0; face < 6; face++) {
        texture->updateImage(renderApi, pixel.data(), pixel.size(), 1, 1, 0, 0, face, 1);
    }
    return texture;
}

}

DefaultAssets::DefaultAssets(RenderAPI* renderApi, AssetStorage<Texture>& textures) {
    m_textures.push_back(createTexture2D(renderApi, textures, kWhiteTexture, vk::Format::eR8G8B8A8Srgb, { 255, 255, 255, 255 }));
    m_textures.push_back(createTexture2D(renderApi, textures, kBlackTexture, vk::Format::eR8G8B8A8Srgb, { 0, 0, 0, 255 }));
    m_textures.push_back(createTexture2D(renderApi, textures, kNormalTexture, vk::Format::eR8G8B8A8Unorm, { 128, 128, 255, 255 }));
    m_textures.push_back(createTexture2D(renderApi, textures, kDefaultMetallicRoughnessTexture, vk::Format::eR8G8B8A8Unorm, { 0, 128, 0, 255 }));
    m_textures.push_back(createCubemap(renderApi, textures, kBlackCubeTexture, vk::Format::eR8G8B8A8Srgb, { 0, 0, 0, 255 }));
}

}
