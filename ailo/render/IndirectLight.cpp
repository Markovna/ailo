#include "IndirectLight.h"

#include <filesystem>
#include <stdexcept>

#include "FileIO.h"
#include "Texture.h"

namespace ailo {

void IndirectLightLoader::load(const std::string& key, LoadContext<IndirectLight>& ctx) {
    const std::filesystem::path packagePath = key + ".lightpack";
    std::string data;
    if (!fileio::readFile(packagePath, data)) {
        throw std::runtime_error("indirect light '" + key + "': cannot read " + packagePath.generic_string());
    }

    std::string error;
    auto pkg = lighting::LightingPackage::deserialize({ reinterpret_cast<const uint8_t*>(data.data()), data.size() }, error);
    if (!pkg) {
        throw std::runtime_error("indirect light '" + packagePath.generic_string() + "': " + error);
    }

    IndirectLight& light = ctx.construct();
    light.reflections = m_textures->emplace(noname_t{}, Texture::fromPackage(m_renderApi, pkg->reflections));
    light.irradianceSH = pkg->irradianceSH;
}

}
