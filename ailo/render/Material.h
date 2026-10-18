#pragma once

#include <array>
#include <bitset>

#include "RenderAPI.h"
#include "assets/AssetServer.h"
#include "material/MaterialPackage.h"

namespace ailo {

namespace materials {
constexpr auto kLit = "assets/materials/lit.matpack";
constexpr auto kSkybox = "assets/materials/skybox.matpack";
}

class Material {
public:
    Material(RenderAPI*, material::MaterialPackage);

    const std::string& getName() const { return m_package.name; }
    const material::MaterialPackage& getPackage() const { return m_package; }
    const material::MaterialParameter* findParameter(std::string_view name) const { return m_package.findParameter(name); }

    DescriptorSetLayoutHandle getDescriptorSetLayout() const { return m_descriptorSetLayout; }

    // Variant bits in the material's variantFilter are dropped. Returns an invalid handle if the
    // package has no shaders for the variant.
    ProgramHandle getProgram(material::Variant variant) const;

private:
    RenderAPI* m_renderApi;
    material::MaterialPackage m_package;
    RasterDescription m_raster {};
    std::vector<DescriptorSetLayoutBinding> m_descriptorSetLayoutBindings;
    Unique<gpu::DescriptorSetLayout> m_descriptorSetLayout;

    mutable std::array<Unique<gpu::Program>, material::Variant::COUNT> m_programs;
    mutable std::bitset<material::Variant::COUNT> m_missingVariants;
};

// Reads a material package built by matcomp (<name>.mat -> <name>.matpack).
class MaterialLoader : public AssetLoader<Material> {
public:
    explicit MaterialLoader(RenderAPI* renderApi) : m_renderApi(renderApi) {}

    constexpr std::span<const std::string_view> extensions() const override { return kExtensions; }

protected:
    void load(const std::string& key, LoadContext<Material>& ctx) override;

private:
    static constexpr std::array<std::string_view, 1> kExtensions = { ".matpack" };

    RenderAPI* m_renderApi;
};

}
