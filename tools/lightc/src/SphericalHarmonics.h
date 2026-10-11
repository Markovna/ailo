#pragma once

#include <array>

#include <glm/glm.hpp>

#include "render/lighting/LightingPackage.h"
#include "render/texture/TexturePackage.h"

namespace lightc {

using IrradianceSH = std::array<glm::vec3, ailo::lighting::kIrradianceSHCount>;

// Projects level 0 of an rgba32f cubemap onto L2 spherical harmonics and convolves it with the clamped cosine, in the
// form LightingPackage::irradianceSH stores.
IrradianceSH projectIrradianceSH(const ailo::texture::TexturePackage& cubemap);

// Evaluates SH from projectIrradianceSH in direction n, as the shader does (without its clamp to 0).
glm::vec3 evaluateIrradianceSH(const IrradianceSH& sh, const glm::vec3& n);

}
