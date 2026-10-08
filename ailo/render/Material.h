#pragma once

#include <array>
#include <bitset>

#include "RenderAPI.h"
#include "assets/AssetServer.h"
#include "material/MaterialPackage.h"

namespace ailo {

namespace materials {
constexpr auto kLit = "materials/lit.matpack";
constexpr auto kSkybox = "materials/skybox.matpack";
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

class MaterialLoader : public AssetLoader<Material> {
public:
    explicit MaterialLoader(RenderAPI* renderApi) : m_renderApi(renderApi) {}

protected:
    void load(const std::string& key, LoadContext<Material>& ctx) override;

private:
    RenderAPI* m_renderApi;
};

}
