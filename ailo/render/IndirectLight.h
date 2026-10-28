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

// Reads a lighting package that lightc compiled from a .light file, keeping its name.
class IndirectLightLoader : public AssetLoader<IndirectLight> {
public:
    IndirectLightLoader(RenderAPI* renderApi, AssetStorage<Texture>* textures)
        : m_renderApi(renderApi), m_textures(textures) {}

    constexpr std::span<const std::string_view> extensions() const override { return kExtensions; }

protected:
    void load(const std::string& key, std::span<const uint8_t> bytes, LoadContext<IndirectLight>& ctx) override;

private:
    static constexpr std::array<std::string_view, 1> kExtensions = { ".light" };

    RenderAPI* m_renderApi;
    AssetStorage<Texture>* m_textures;
};

}
