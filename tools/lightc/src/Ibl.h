#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "render/texture/TexturePackage.h"

namespace lightc {

struct PrefilterOptions {
    uint32_t size = 256;        // level 0 face size, a power of two
    uint32_t samples = 1024;    // GGX samples per texel
};

// Prefilters an equirectangular HDR image into an rgba32f cubemap for the split-sum approximation. Level i holds the
// radiance for perceptual roughness lodToPerceptualRoughness(i / (levels - 1)); levels go down to 16x16.
std::optional<ailo::texture::TexturePackage> prefilterEnvironment(const std::filesystem::path& source,
                                                                  const PrefilterOptions&, std::string& error);

// The 256x256 rgba16f DFG LUT, sampled at (NoV, perceptualRoughness).
ailo::texture::TexturePackage dfgLut();

// Converts an rgba32f package to rgba16f, clamping to the largest finite half.
ailo::texture::TexturePackage toHalfFloat(const ailo::texture::TexturePackage& package);

}
