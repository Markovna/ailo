#include "Ibl.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/packing.hpp>

#include "Cubemap.h"
#include "stb_image/stb_image.h"

namespace lightc {

using namespace ailo::texture;
namespace fs = std::filesystem;

namespace {

constexpr float PI = glm::pi<float>();
constexpr float TWO_PI = glm::two_pi<float>();
constexpr float INV_PI = 1.0f / PI;
constexpr float INV_TWO_PI = 0.5f / PI;

constexpr uint32_t kMinPrefilterSize = 16;
constexpr uint32_t kDfgSize = 256;
constexpr uint32_t kDfgSamples = 1024;

inline float visibility(float NoV, float NoL, float a) {
    // Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
    // Height-correlated GGX
    const float a2 = a * a;
    const float GGXL = NoV * std::sqrt((NoL - NoL * a2) * NoL + a2);
    const float GGXV = NoL * std::sqrt((NoV - NoV * a2) * NoV + a2);
    return 0.5f / (GGXV + GGXL);
}

glm::vec2 hammersley(uint32_t i, float iN) {
    constexpr float tof = 0.5f / 0x80000000U;
    uint32_t bits = i;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return { i * iN, bits * tof };
}

inline glm::vec3 hemisphereImportanceSampleDggx(glm::vec2 u, float a) { // pdf = D(a) * cosTheta
    const float phi = 2.0f * PI * u.x;
    // NOTE: (aa-1) == (a-1)(a+1) produces better fp accuracy
    const float cosTheta2 = (1 - u.y) / (1 + (a + 1) * ((a - 1) * u.y));
    const float cosTheta = std::sqrt(cosTheta2);
    const float sinTheta = std::sqrt(1 - cosTheta2);
    return { sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta };
}

float pow5(float x) {
    const float x2 = x * x;
    return x2 * x2 * x;
}

inline float D_GGX(float NoH, float a) {
    const float a2 = a * a;
    const float f = (NoH * a2 - NoH) * NoH + 1.0f;
    return a2 / (PI * f * f);
}

inline glm::vec2 directionToEquirectUV(const glm::vec3& dir) {
    float theta = std::acos(glm::clamp(dir.y, -1.0f, 1.0f));
    float phi = std::atan2(dir.x, dir.z);
    return glm::vec2((phi + PI) * INV_TWO_PI, theta * INV_PI);
}

// Bilinear sample of an RGB equirect image, wrapping horizontally.
inline glm::vec3 sampleEquirect(const float* data, int width, int height, const glm::vec2& uv) {
    float u = uv.x - std::floor(uv.x);
    float v = glm::clamp(uv.y, 0.0f, 1.0f);

    float fx = u * width - 0.5f;
    float fy = v * height - 0.5f;

    int x0 = static_cast<int>(std::floor(fx));
    int y0 = static_cast<int>(std::floor(fy));

    float fracX = fx - x0;
    float fracY = fy - y0;

    int x1 = x0 + 1;
    x0 = ((x0 % width) + width) % width;
    x1 = ((x1 % width) + width) % width;

    y0 = glm::clamp(y0, 0, height - 1);
    int y1 = glm::clamp(y0 + 1, 0, height - 1);

    const float* p00 = data + (y0 * width + x0) * 3;
    const float* p10 = data + (y0 * width + x1) * 3;
    const float* p01 = data + (y1 * width + x0) * 3;
    const float* p11 = data + (y1 * width + x1) * 3;

    float w00 = (1.0f - fracX) * (1.0f - fracY);
    float w10 = fracX * (1.0f - fracY);
    float w01 = (1.0f - fracX) * fracY;
    float w11 = fracX * fracY;

    return glm::max(glm::vec3(0.0f), glm::vec3(
        p00[0] * w00 + p10[0] * w10 + p01[0] * w01 + p11[0] * w11,
        p00[1] * w00 + p10[1] * w10 + p01[1] * w01 + p11[1] * w11,
        p00[2] * w00 + p10[2] * w10 + p01[2] * w01 + p11[2] * w11
    ));
}

// Mip chain of the equirect source for filtered importance sampling (Krivanek & Colbert 2008).
// Level 0 aliases the loaded image.
class EquirectMips {
public:
    EquirectMips(const float* data, int width, int height) {
        m_levels.push_back({ data, width, height });
        while (width > 1 || height > 1) {
            const int w = std::max(width / 2, 1);
            const int h = std::max(height / 2, 1);
            m_storage.push_back(downsample(m_levels.back(), w, h));
            m_levels.push_back({ m_storage.back().data(), w, h });
            width = w;
            height = h;
        }
        m_log2TexelSolidAngle = std::log2((TWO_PI / m_levels[0].width) * (PI / m_levels[0].height));
        m_minSinTheta = std::sin(0.5f * PI / m_levels[0].height);
    }

    float maxLod() const { return static_cast<float>(m_levels.size() - 1); }

    // log2 of the solid angle covered by a level 0 texel in direction dir.
    float log2TexelSolidAngle(const glm::vec3& dir) const {
        const float sinTheta = std::sqrt(std::max(1.0f - dir.y * dir.y, 0.0f));
        return m_log2TexelSolidAngle + std::log2(std::max(sinTheta, m_minSinTheta));
    }

    glm::vec3 sample(const glm::vec2& uv, float lod) const {
        lod = glm::clamp(lod, 0.0f, maxLod());
        const int l0 = static_cast<int>(lod);
        const float t = lod - static_cast<float>(l0);
        const Level& a = m_levels[l0];
        const glm::vec3 c0 = sampleEquirect(a.data, a.width, a.height, uv);
        if (t == 0.0f) {
            return c0;
        }
        const Level& b = m_levels[l0 + 1];
        return glm::mix(c0, sampleEquirect(b.data, b.width, b.height, uv), t);
    }

private:
    struct Level {
        const float* data;
        int width;
        int height;
    };

    // 2x2 box filter; rows are weighted by sin(theta) since equirect texels shrink towards the poles.
    static std::vector<float> downsample(const Level& src, int w, int h) {
        std::vector<float> dst(static_cast<size_t>(w) * h * 3);
        for (int y = 0; y < h; ++y) {
            const int y0 = std::min(2 * y, src.height - 1);
            const int y1 = std::min(2 * y + 1, src.height - 1);
            const float w0 = std::sin((y0 + 0.5f) * PI / src.height);
            const float w1 = y1 != y0 ? std::sin((y1 + 0.5f) * PI / src.height) : 0.0f;
            const float norm = 0.5f / (w0 + w1);
            for (int x = 0; x < w; ++x) {
                const int x0 = std::min(2 * x, src.width - 1);
                const int x1 = std::min(2 * x + 1, src.width - 1);
                for (int c = 0; c < 3; ++c) {
                    const float r0 = src.data[(y0 * src.width + x0) * 3 + c] + src.data[(y0 * src.width + x1) * 3 + c];
                    const float r1 = src.data[(y1 * src.width + x0) * 3 + c] + src.data[(y1 * src.width + x1) * 3 + c];
                    dst[(static_cast<size_t>(y) * w + x) * 3 + c] = (r0 * w0 + r1 * w1) * norm;
                }
            }
        }
        return dst;
    }

    std::vector<Level> m_levels;
    std::vector<std::vector<float>> m_storage;
    float m_log2TexelSolidAngle;
    float m_minSinTheta;
};

float lodToPerceptualRoughness(float lod) noexcept {
    const float a = 2.0f;
    const float b = -1.0f;
    return (lod != 0)
            ? std::clamp((std::sqrt(a * a + 4.0f * b * lod) - a) / (2.0f * b), 0.0f, 1.0f)
            : 0.0f;
}

glm::vec2 DFV_Multiscatter(float NoV, float linearRoughness, size_t numSamples) {
    glm::vec2 r(0.0f);
    const glm::vec3 V(std::sqrt(1 - NoV * NoV), 0, NoV);
    for (size_t i = 0; i < numSamples; i++) {
        const glm::vec2 u = hammersley(uint32_t(i), 1.0f / numSamples);
        const glm::vec3 H = hemisphereImportanceSampleDggx(u, linearRoughness);
        const glm::vec3 L = 2 * dot(V, H) * H - V;
        const float VoH = glm::clamp(glm::dot(V, H), 0.0f, 1.0f);
        const float NoL = glm::clamp(L.z, 0.0f, 1.0f);
        const float NoH = glm::clamp(H.z, 0.0f, 1.0f);
        if (NoL > 0) {
            const float v = visibility(NoV, NoL, linearRoughness) * NoL * (VoH / NoH);
            const float Fc = pow5(1 - VoH);
            r.x += v * Fc;
            r.y += v;
        }
    }
    return r * (4.0f / numSamples);
}

void appendPixels(TexturePackage& pkg, const std::vector<glm::vec4>& pixels) {
    const size_t offset = pkg.data.size();
    pkg.data.resize(offset + pixels.size() * sizeof(glm::vec4));
    std::memcpy(pkg.data.data() + offset, pixels.data(), pixels.size() * sizeof(glm::vec4));
}

}

std::optional<TexturePackage> prefilterEnvironment(const fs::path& source, const PrefilterOptions& options,
                                                   std::string& error) {
    if (!std::has_single_bit(options.size)) {
        error = "size must be a power of two, got " + std::to_string(options.size);
        return std::nullopt;
    }
    if (options.samples == 0) {
        error = "samples must be positive";
        return std::nullopt;
    }

    int width = 0, height = 0, channels = 0;
    float* image = stbi_loadf(source.string().c_str(), &width, &height, &channels, 3);
    if (!image) {
        error = source.generic_string() + ": cannot load image: " + stbi_failure_reason();
        return std::nullopt;
    }
    std::vector<float> pixels(image, image + size_t(width) * height * 3);
    stbi_image_free(image);

    const uint32_t baseExp = std::countr_zero(options.size);
    uint32_t minExp = std::countr_zero(kMinPrefilterSize);
    if (minExp >= baseExp) {
        minExp = 0;
    }
    const uint32_t numLevels = baseExp + 1 - minExp;
    const uint32_t numSamples = options.samples;

    const EquirectMips equirect(pixels.data(), width, height);

    TexturePackage pkg;
    pkg.type = TextureType::Cubemap;
    pkg.format = TextureFormat::RGBA32F;
    pkg.width = options.size;
    pkg.height = options.size;
    pkg.levels = numLevels;
    pkg.generateMipmaps = false;
    pkg.data.reserve(pkg.expectedDataSize());

    // A GGX sample of the split-sum lobe, in the tangent frame of N = V.
    struct LobeSample {
        glm::vec3 L;
        float NoL;
        float log2SolidAngle; // log2 of the solid angle the sample stands for: 1 / (numSamples * pdf)
    };
    std::vector<LobeSample> lobe;
    std::vector<glm::vec4> face;

    for (uint32_t level = 0; level < numLevels; level++) {
        const uint32_t dim = options.size >> level;

        const float lod = numLevels > 1 ? float(level) / float(numLevels - 1) : 0.0f;
        const float perceptualRoughness = lodToPerceptualRoughness(lod);
        const float roughness = perceptualRoughness * perceptualRoughness;
        // At roughness 0 the GGX lobe is a delta (every sample yields L = N), so a single lookup gives the same result.
        const bool mirror = roughness == 0.0f;

        lobe.clear();
        if (!mirror) {
            const float iN = 1.0f / static_cast<float>(numSamples);
            for (uint32_t s = 0; s < numSamples; ++s) {
                const glm::vec3 H = hemisphereImportanceSampleDggx(hammersley(s, iN), roughness);
                const float NoH = H.z; // == VoH
                const glm::vec3 L(2.0f * NoH * H.x, 2.0f * NoH * H.y, 2.0f * NoH * NoH - 1.0f);
                if (L.z > 0.0f) {
                    // pdf(L) = D * NoH / (4 * VoH) = D / 4
                    const float pdf = D_GGX(NoH, roughness) * 0.25f;
                    lobe.push_back({ L, L.z, -std::log2(static_cast<float>(numSamples) * pdf) });
                }
            }
        }

        for (int faceIndex = 0; faceIndex < 6; ++faceIndex) {
            face.assign(size_t(dim) * dim, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

            #ifdef _OPENMP
            #pragma omp parallel for schedule(dynamic)
            #endif
            for (int y = 0; y < static_cast<int>(dim); ++y) {
                for (uint32_t x = 0; x < dim; ++x) {
                    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(dim);
                    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(dim);

                    // Split-sum approximation: N = V = R (outgoing direction)
                    const glm::vec3 N = cubemapToDirection(faceIndex, u, v);
                    glm::vec4& out = face[size_t(y) * dim + x];

                    if (mirror) {
                        out = glm::vec4(equirect.sample(directionToEquirectUV(N), 0.0f), 1.0f);
                        continue;
                    }

                    const glm::vec3 up = std::abs(N.z) < 0.999f
                        ? glm::vec3(0.0f, 0.0f, 1.0f)
                        : glm::vec3(1.0f, 0.0f, 0.0f);
                    const glm::vec3 tangentX = glm::normalize(glm::cross(up, N));
                    const glm::vec3 tangentY = glm::cross(N, tangentX);

                    glm::vec3 prefilteredColor(0.0f);
                    float totalWeight = 0.0f;

                    for (const LobeSample& s : lobe) {
                        const glm::vec3 L = tangentX * s.L.x + tangentY * s.L.y + N * s.L.z;

                        // Read the source mip whose texels cover the sample's solid angle, so each sample
                        // integrates its neighbourhood instead of point-sampling it. +1 biases towards
                        // blur, as overlapping footprints alias less (Krivanek & Colbert 2008).
                        const float sampleLod = 0.5f * (s.log2SolidAngle - equirect.log2TexelSolidAngle(L)) + 1.0f;
                        const glm::vec3 envSample = equirect.sample(directionToEquirectUV(L), sampleLod);

                        // Weight by NoL — cancels the cosine term in the pdf
                        prefilteredColor += envSample * s.NoL;
                        totalWeight += s.NoL;
                    }

                    if (totalWeight > 0.0f) {
                        prefilteredColor /= totalWeight;
                    }
                    out = glm::vec4(prefilteredColor, 1.0f);
                }
            }

            appendPixels(pkg, face);
        }
    }

    error = pkg.validate();
    if (!error.empty()) return std::nullopt;
    return pkg;
}

TexturePackage dfgLut() {
    std::vector<glm::vec4> pixels(size_t(kDfgSize) * kDfgSize);

    #ifdef _OPENMP
    #pragma omp parallel for schedule(dynamic)
    #endif
    for (int y = 0; y < static_cast<int>(kDfgSize); y++) {
        // Row 0 is uploaded at v = 0 and the shader samples v = perceptualRoughness,
        // so rows go from smooth (top) to rough (bottom), sampled at texel centers.
        const float coord = (y + 0.5f) / kDfgSize;
        const float linearRoughness = coord * coord;
        for (uint32_t x = 0; x < kDfgSize; x++) {
            const float NoV = (x + 0.5f) / kDfgSize;
            pixels[size_t(y) * kDfgSize + x] = glm::vec4(DFV_Multiscatter(NoV, linearRoughness, kDfgSamples), 0.0f, 1.0f);
        }
    }

    TexturePackage pkg;
    pkg.type = TextureType::Texture2D;
    pkg.format = TextureFormat::RGBA32F;
    pkg.width = kDfgSize;
    pkg.height = kDfgSize;
    pkg.levels = 1;
    pkg.generateMipmaps = false;
    appendPixels(pkg, pixels);
    return toHalfFloat(pkg);
}

TexturePackage toHalfFloat(const TexturePackage& package) {
    assert(package.format == TextureFormat::RGBA32F);

    TexturePackage half = package;
    half.format = TextureFormat::RGBA16F;
    half.data.resize(package.data.size() / 2);

    const size_t count = package.data.size() / sizeof(float);
    for (size_t i = 0; i < count; i++) {
        float value;
        std::memcpy(&value, package.data.data() + i * sizeof(float), sizeof(float));
        const uint16_t bits = glm::packHalf1x16(std::min(value, 65504.0f));
        std::memcpy(half.data.data() + i * sizeof(uint16_t), &bits, sizeof(uint16_t));
    }
    return half;
}

}
