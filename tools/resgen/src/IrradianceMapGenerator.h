#pragma once

#include <string>
#include <cstdint>
#include <glm/glm.hpp>

#include "../../../third_party/glm/glm/common.hpp"

namespace ailo {

struct IrradianceGeneratorConfig {
    uint32_t outputSize = 64;    // Size of each cubemap face (width = height)
    uint32_t sampleCount = 1024; // Number of hemisphere samples for integration
};

class IrradianceMapGenerator {
public:
    // Generate diffuse irradiance cubemap from an equirectangular HDR image.
    // Input: path to equirectangular HDR image
    // Output: 6 images saved to outputPath with suffixes (_px, _nx, _py, _ny, _pz, _nz)
    // Returns true on success, false on failure
    static bool irradiance(
        const std::string& inputPath,
        const std::string& outputPath,
        const IrradianceGeneratorConfig& config = {}
    );

    static void dfg(const std::string& path);

private:
    struct HDRImage {
        float* data = nullptr;
        int width = 0;
        int height = 0;
        int channels = 0;

        ~HDRImage();
        bool load(const std::string& path);
    };

    struct CubemapFace {
        float* data = nullptr;
        uint32_t size = 0;

        CubemapFace() = default;
        CubemapFace(uint32_t size);
        ~CubemapFace();
        CubemapFace(const CubemapFace&) = delete;
        CubemapFace& operator=(const CubemapFace&) = delete;
        CubemapFace(CubemapFace&& other) noexcept;
        CubemapFace& operator=(CubemapFace&& other) noexcept;

        bool save(const std::string& path) const;
    };

    // Sample the equirectangular image at a given direction
    static void sampleEquirectangular(
        const HDRImage& image,
        float dirX, float dirY, float dirZ,
        float& outR, float& outG, float& outB
    );

    // Get world-space direction from cubemap face and UV coordinates
    static void getDirectionFromCubemapUV(
        int face, float u, float v,
        float& dirX, float& dirY, float& dirZ
    );

    // Compute irradiance at a given normal direction
    static void computeIrradiance(
        const HDRImage& image,
        float normalX, float normalY, float normalZ,
        uint32_t sampleCount,
        float& outR, float& outG, float& outB
    );

    // Generate a single cubemap face
    static void generateFace(
        const HDRImage& image,
        int face,
        CubemapFace& outFace,
        uint32_t sampleCount
    );
};

} // namespace ailo
