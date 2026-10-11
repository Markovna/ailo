#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "render/texture/TexturePackage.h"

namespace ailo::lighting {

// Bump whenever the binary layout or the meaning of any field changes; packages with another version are rejected.
static constexpr uint32_t kLightingPackageVersion = 1;

static constexpr uint32_t kIrradianceSHCount = 9;

// Image-based lighting baked by lightc from one environment.
struct LightingPackage {
    // L2 spherical harmonics of the diffuse irradiance divided by pi, with the basis constants folded in, so
    // sum(irradianceSH[i] * poly_i(n)) is the light a Lambertian surface with normal n reflects for an albedo of 1.
    // poly = 1, y, z, x, y*x, y*z, 3*z*z - 1, z*x, x*x - y*y.
    std::array<glm::vec3, kIrradianceSHCount> irradianceSH{};

    // The prefiltered specular radiance cubemap.
    texture::TexturePackage reflections;

    std::string validate() const;

    std::vector<uint8_t> serialize() const;

    static std::optional<LightingPackage> deserialize(std::span<const uint8_t> data, std::string& error);
};

}
