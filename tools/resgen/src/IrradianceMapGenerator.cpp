#include "IrradianceMapGenerator.h"
#include <stb_image/stb_image.h>
#include <stb_image/stb_image_write.h>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <vector>

#include "../../../third_party/glm/glm/ext/scalar_constants.hpp"
#include "../../../third_party/stb_image/include/stb_image/stb_image_write.h"

#ifdef _OPENMP
#include <omp.h>
#endif

namespace ailo {

namespace {

constexpr float PI = glm::pi<float>();
constexpr float TWO_PI = glm::two_pi<float>();
constexpr float INV_PI = 1.0f / PI;
constexpr float INV_TWO_PI = 0.5f / PI;

enum CubeFace {
    FACE_POS_X = 0,
    FACE_NEG_X = 1,
    FACE_POS_Y = 2,
    FACE_NEG_Y = 3,
    FACE_POS_Z = 4,
    FACE_NEG_Z = 5
};

const char* FACE_SUFFIXES[] = {
    "_px", "_nx", "_py", "_ny", "_pz", "_nz"
};

// Precomputed sample data for integration
struct SampleData {
    std::vector<float> sinTheta;
    std::vector<float> cosTheta;
    std::vector<float> sinPhi;
    std::vector<float> cosPhi;
    std::vector<glm::vec2> uvs;  // Precomputed equirect UVs for each sample
    std::vector<glm::vec3> directions;  // Precomputed world directions
    float dTheta;
    float dPhi;
    uint32_t thetaSamples;
    uint32_t phiSamples;
};

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
    const float phi = 2.0f * (float) glm::pi<float>() * u.x;
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

glm::vec2 DFV(float NoV, float linearRoughness, size_t numSamples) {
    glm::vec2 r(0);
    const glm::vec3 V(std::sqrt(1 - NoV * NoV), 0, NoV);
    for (size_t i = 0; i < numSamples; i++) {
        const glm::vec2 u = hammersley(uint32_t(i), 1.0f / numSamples);
        const glm::vec3 H = hemisphereImportanceSampleDggx(u, linearRoughness);
        const glm::vec3 L = 2 * dot(V, H) * H - V;
        const float VoH = glm::clamp(dot(V, H), 0.0f, 1.0f);
        const float NoL = glm::clamp(L.z, 0.0f, 1.0f);
        const float NoH = glm::clamp(H.z, 0.0f, 1.0f);
        if (NoL > 0) {
            const float v = visibility(NoV, NoL, linearRoughness) * NoL * (VoH / NoH);
            const float Fc = pow5(1 - VoH);
            r.x += v * (1.0f - Fc);
            r.y += v * Fc;
        }
    }
    return r * (4.0f / numSamples);
}

// Convert direction to equirectangular UV coordinates
inline glm::vec2 directionToEquirectUV(const glm::vec3& dir) {
    float theta = std::acos(glm::clamp(dir.y, -1.0f, 1.0f));
    float phi = std::atan2(dir.x, dir.z);
    return glm::vec2((phi + PI) * INV_TWO_PI, theta * INV_PI);
}

// Get direction from cubemap face and UV (OpenGL convention)
inline glm::vec3 cubemapToDirection(int face, float u, float v) {
    float uc = 2.0f * u - 1.0f;
    float vc = 2.0f * v - 1.0f;

    glm::vec3 dir;
    switch (face) {
        case FACE_POS_X: dir = glm::vec3( 1.0f,   -vc,   -uc); break;
        case FACE_NEG_X: dir = glm::vec3(-1.0f,   -vc,    uc); break;
        case FACE_POS_Y: dir = glm::vec3(   uc,  1.0f,    vc); break;
        case FACE_NEG_Y: dir = glm::vec3(   uc, -1.0f,   -vc); break;
        case FACE_POS_Z: dir = glm::vec3(   uc,   -vc,  1.0f); break;
        case FACE_NEG_Z: dir = glm::vec3(  -uc,   -vc, -1.0f); break;
        default: dir = glm::vec3(0.0f, 0.0f, 1.0f); break;
    }
    return glm::normalize(dir);
}

std::string getOutputPathForFace(const std::string& basePath, int face) {
    size_t dotPos = basePath.rfind('.');
    if (dotPos != std::string::npos) {
        return basePath.substr(0, dotPos) + FACE_SUFFIXES[face] + basePath.substr(dotPos);
    }
    return basePath + FACE_SUFFIXES[face] + ".hdr";
}

// Sample equirectangular image with bilinear filtering
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

// Precompute all sample directions and their corresponding UVs
SampleData precomputeSamples(uint32_t sampleCount, int imageWidth) {
    SampleData data;

    data.phiSamples = glm::min(sampleCount, static_cast<uint32_t>(imageWidth));
    data.thetaSamples = data.phiSamples / 2;
    data.thetaSamples = glm::max(data.thetaSamples, 32u);
    data.phiSamples = glm::max(data.phiSamples, 64u);

    data.dTheta = PI / static_cast<float>(data.thetaSamples);
    data.dPhi = TWO_PI / static_cast<float>(data.phiSamples);

    // Precompute trig values
    data.sinTheta.resize(data.thetaSamples);
    data.cosTheta.resize(data.thetaSamples);
    for (uint32_t ti = 0; ti < data.thetaSamples; ++ti) {
        float theta = (static_cast<float>(ti) + 0.5f) * data.dTheta;
        data.sinTheta[ti] = std::sin(theta);
        data.cosTheta[ti] = std::cos(theta);
    }

    data.sinPhi.resize(data.phiSamples);
    data.cosPhi.resize(data.phiSamples);
    for (uint32_t pi = 0; pi < data.phiSamples; ++pi) {
        float phi = (static_cast<float>(pi) + 0.5f) * data.dPhi;
        data.sinPhi[pi] = std::sin(phi);
        data.cosPhi[pi] = std::cos(phi);
    }

    // Precompute all directions and UVs
    uint32_t totalSamples = data.thetaSamples * data.phiSamples;
    data.directions.resize(totalSamples);
    data.uvs.resize(totalSamples);

    for (uint32_t ti = 0; ti < data.thetaSamples; ++ti) {
        float sinT = data.sinTheta[ti];
        float cosT = data.cosTheta[ti];

        for (uint32_t pi = 0; pi < data.phiSamples; ++pi) {
            uint32_t idx = ti * data.phiSamples + pi;

            glm::vec3 dir(
                sinT * data.sinPhi[pi],
                cosT,
                sinT * data.cosPhi[pi]
            );

            data.directions[idx] = dir;
            data.uvs[idx] = directionToEquirectUV(dir);
        }
    }

    return data;
}

} // anonymous namespace

// HDRImage implementation

IrradianceMapGenerator::HDRImage::~HDRImage() {
    if (data) {
        stbi_image_free(data);
        data = nullptr;
    }
}

bool IrradianceMapGenerator::HDRImage::load(const std::string& path) {
    stbi_set_flip_vertically_on_load(false);
    data = stbi_loadf(path.c_str(), &width, &height, &channels, 3);
    channels = 3;

    if (!data) {
        std::cerr << "Failed to load HDR image: " << path << std::endl;
        std::cerr << "Reason: " << stbi_failure_reason() << std::endl;
        return false;
    }

    return true;
}

// CubemapFace implementation

IrradianceMapGenerator::CubemapFace::CubemapFace(uint32_t faceSize)
    : size(faceSize) {
    data = new float[size * size * 3]();
}

IrradianceMapGenerator::CubemapFace::~CubemapFace() {
    delete[] data;
    data = nullptr;
}

IrradianceMapGenerator::CubemapFace::CubemapFace(CubemapFace&& other) noexcept
    : data(other.data), size(other.size) {
    other.data = nullptr;
    other.size = 0;
}

IrradianceMapGenerator::CubemapFace& IrradianceMapGenerator::CubemapFace::operator=(CubemapFace&& other) noexcept {
    if (this != &other) {
        delete[] data;
        data = other.data;
        size = other.size;
        other.data = nullptr;
        other.size = 0;
    }
    return *this;
}

bool IrradianceMapGenerator::CubemapFace::save(const std::string& path) const {
    int result = stbi_write_hdr(path.c_str(), size, size, 3, data);
    if (!result) {
        std::cerr << "Failed to write HDR image: " << path << std::endl;
        return false;
    }
    return true;
}

// Stub implementations for API compatibility
void IrradianceMapGenerator::sampleEquirectangular(
    const HDRImage&, float, float, float, float&, float&, float&) {}

void IrradianceMapGenerator::getDirectionFromCubemapUV(
    int face, float u, float v, float& dirX, float& dirY, float& dirZ) {
    glm::vec3 d = cubemapToDirection(face, u, v);
    dirX = d.x; dirY = d.y; dirZ = d.z;
}

void IrradianceMapGenerator::computeIrradiance(
    const HDRImage&, float, float, float, uint32_t, float&, float&, float&) {}

void IrradianceMapGenerator::generateFace(
    const HDRImage& image,
    int face,
    CubemapFace& outFace,
    uint32_t sampleCount
) {
    // This is now handled in generate() with shared precomputed data
}

bool IrradianceMapGenerator::irradiance(
    const std::string& inputPath,
    const std::string& outputPath,
    const IrradianceGeneratorConfig& config
) {
    std::cout << "Loading HDR image: " << inputPath << std::endl;

    HDRImage image;
    if (!image.load(inputPath)) {
        return false;
    }

    std::cout << "Loaded image: " << image.width << "x" << image.height << std::endl;

    // Precompute sample data once for all faces
    std::cout << "Precomputing sample directions..." << std::endl;
    SampleData samples = precomputeSamples(config.sampleCount, image.width);

    std::cout << "Generating irradiance map (" << config.outputSize << "x"
              << config.outputSize << " per face, "
              << samples.thetaSamples << "x" << samples.phiSamples << " samples)..." << std::endl;

    const uint32_t totalSamples = samples.thetaSamples * samples.phiSamples;

    for (int face = 0; face < 6; ++face) {
        std::cout << "Processing face " << (face + 1) << "/6 ("
                  << (FACE_SUFFIXES[face] + 1) << ")..." << std::endl;

        CubemapFace cubeFace(config.outputSize);
        uint32_t size = cubeFace.size;

        // Parallelize the outer loop with OpenMP
        #ifdef _OPENMP
        #pragma omp parallel for schedule(dynamic)
        #endif
        for (int y = 0; y < static_cast<int>(size); ++y) {
            for (uint32_t x = 0; x < size; ++x) {
                float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
                float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);

                glm::vec3 normal = cubemapToDirection(face, u, v);
                glm::vec3 irradiance(0.0f);

                // Integrate using precomputed samples
                for (uint32_t ti = 0; ti < samples.thetaSamples; ++ti) {
                    float sinT = samples.sinTheta[ti];
                    if (sinT < 0.0001f) continue;

                    uint32_t rowStart = ti * samples.phiSamples;

                    for (uint32_t pi = 0; pi < samples.phiSamples; ++pi) {
                        uint32_t sampleIdx = rowStart + pi;
                        const glm::vec3& sampleDir = samples.directions[sampleIdx];

                        float NdotL = glm::dot(normal, sampleDir);

                        if (NdotL > 0.0f) {
                            glm::vec3 L = sampleEquirect(image.data, image.width, image.height, samples.uvs[sampleIdx]);
                            float solidAngle = sinT * samples.dTheta * samples.dPhi;
                            irradiance += L * (NdotL * solidAngle);
                        }
                    }
                }

                // Divide by PI for PBR shaders
                irradiance *= INV_PI;

                uint32_t idx = (y * size + x) * 3;
                cubeFace.data[idx + 0] = irradiance.x;
                cubeFace.data[idx + 1] = irradiance.y;
                cubeFace.data[idx + 2] = irradiance.z;
            }
        }

        std::string facePath = getOutputPathForFace(outputPath, face);
        if (!cubeFace.save(facePath)) {
            return false;
        }

        std::cout << "Saved: " << facePath << std::endl;
    }

    std::cout << "Irradiance map generation complete!" << std::endl;
    return true;
}

void IrradianceMapGenerator::dfg(const std::string& path) {
    const size_t width = 256;
    const size_t height = 256;
    auto data = new glm::vec3[width * height];
    auto dataPtr = data;

    for (size_t y = 0; y < height; y++) {
        const float h = static_cast<float>(height);
        const float coord = glm::clamp((h - y + 0.5f) / h, 0.0f, 1.0f);
        const float linear_roughness = coord * coord;
        for (size_t x = 0; x < width; x++) {
            const float w = static_cast<float>(width);
            const float NoV = glm::clamp((x + 0.5f) / w, 0.0f, 1.0f);
            const glm::vec3 r = { DFV(NoV, linear_roughness, 1024), 0 };
            *dataPtr = r;
            dataPtr++;
        }
    }

    int result = stbi_write_hdr(path.c_str(), width, height, 3, reinterpret_cast<float*>(data));
    delete[] data;
    if (!result) {
        std::cerr << "Failed to write PNG image: " << path << std::endl;
        return;
    }

    std::cout << "DFG LUT texture generation complete!" << std::endl;
}
} // namespace ailo
