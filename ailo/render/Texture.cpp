#include "Texture.h"

namespace ailo {

Texture::Texture(Engine& engine, vk::Format format, uint32_t width, uint32_t height, vk::Filter filter)
    : m_handle(engine.getRenderAPI()->createTexture(format, width, height, filter))
{ }

void Texture::updateImage(Engine& engine, const void* data, size_t dataSize, uint32_t width, uint32_t height, uint32_t xOffset,
    uint32_t yOffset) {
    engine.getRenderAPI()->updateTextureImage(m_handle, data, dataSize, width, height, xOffset, yOffset);
}

void Texture::updateImage(Engine& engine, const void* data, size_t dataSize) {
    engine.getRenderAPI()->updateTextureImage(m_handle, data, dataSize);
}

void Texture::destroy(Engine& engine) {
    engine.getRenderAPI()->destroyTexture(m_handle);
}

}
