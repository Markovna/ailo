#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

#include "render/texture/TexturePackage.h"

namespace lightc {

struct PrefilterOptions {
    uint32_t size = 256;        // level 0 face size, a power of two
    uint32_t samples = 1024;    // GGX samples per texel
    float maxRadiance = 0.0f;   // texels brighter than this (luminance) are scaled down to it; 0 keeps them
};

// Scales every RGB texel whose luminance exceeds maxLuminance down to it, keeping its color. Removes a sun that a
// directional light already provides, so the IBL doesn't light its shadows.
inline void clampRadiance(std::span<float> rgb, float maxLuminance) {
    for (size_t i = 0; i + 2 < rgb.size(); i += 3) {
        const float luminance = 0.2126f * rgb[i] + 0.7152f * rgb[i + 1] + 0.0722f * rgb[i + 2];
        if (luminance > maxLuminance) {
            const float scale = maxLuminance / luminance;
            std::for_each(rgb.begin() + i, rgb.begin() + i + 3, [scale](float& c) { c *= scale; });
        }
    }
}

// Prefilters an equirectangular HDR image into an rgba32f cubemap for the split-sum approximation. Level i holds the
// radiance for perceptual roughness lodToPerceptualRoughness(i / (levels - 1)); levels go down to 16x16.
std::optional<ailo::texture::TexturePackage> prefilterEnvironment(const std::filesystem::path& source,
                                                                  const PrefilterOptions&, std::string& error);

// The 256x256 rgba16f DFG LUT, sampled at (NoV, perceptualRoughness).
ailo::texture::TexturePackage dfgLut();

// Converts an rgba32f package to rgba16f, clamping to the largest finite half.
ailo::texture::TexturePackage toHalfFloat(const ailo::texture::TexturePackage& package);

}
