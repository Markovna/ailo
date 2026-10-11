#pragma once
#include <array>
#include <glm/glm.hpp>

#include "Texture.h"
#include "assets/AssetServer.h"
#include "render/lighting/LightingPackage.h"

namespace ailo {

// Image-based lighting baked by lightc (a .light asset).
struct IndirectLight {
    // Prefiltered specular radiance cubemap.
    AssetPtr<Texture> reflections;
    // Diffuse irradiance, see lighting::LightingPackage::irradianceSH.
    std::array<glm::vec3, lighting::kIrradianceSHCount> irradianceSH{};
};

// Reads <key>.lightpack.
class IndirectLightLoader : public AssetLoader<IndirectLight> {
public:
    IndirectLightLoader(RenderAPI* renderApi, AssetStorage<Texture>* textures)
        : m_renderApi(renderApi), m_textures(textures) {}

protected:
    void load(const std::string& key, LoadContext<IndirectLight>& ctx) override;

private:
    RenderAPI* m_renderApi;
    AssetStorage<Texture>* m_textures;
};

}
