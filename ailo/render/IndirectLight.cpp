#include "IndirectLight.h"

#include <filesystem>
#include <stdexcept>

#include "Texture.h"

namespace ailo {

void IndirectLightLoader::load(const std::string& key, std::span<const uint8_t> bytes, LoadContext<IndirectLight>& ctx) {
    std::string error;
    auto pkg = lighting::LightingPackage::deserialize(bytes, error);
    if (!pkg) {
        throw std::runtime_error("indirect light '" + key + "': " + error);
    }

    IndirectLight& light = ctx.construct();
    light.reflections = m_textures->emplace(noname_t{}, Texture::fromPackage(m_renderApi, pkg->reflections));
    light.irradianceSH = pkg->irradianceSH;
}

}
