#include "Cubemap.h"
#include "Ibl.h"
#include "SphericalHarmonics.h"
#include "Testing.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

#include "render/texture/TexturePackage.h"

using namespace ailo;

namespace {

bool near(const glm::vec3& a, const glm::vec3& b, float tolerance) {
    return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(tolerance)));
}

// An rgba32f cubemap whose texels hold radiance(direction).
template<typename F>
texture::TexturePackage makeCubemap(uint32_t dim, F radiance) {
    texture::TexturePackage pkg;
    pkg.type = TextureType::Cubemap;
    pkg.format = TextureFormat::RGBA32F;
    pkg.width = dim;
    pkg.height = dim;
    pkg.levels = 1;
    pkg.data.resize(pkg.expectedDataSize());
    auto* texels = reinterpret_cast<glm::vec4*>(pkg.data.data());
    for (int face = 0; face < 6; face++) {
        for (uint32_t y = 0; y < dim; y++) {
            for (uint32_t x = 0; x < dim; x++) {
                const glm::vec3 dir = lightc::cubemapToDirection(face, (x + 0.5f) / dim, (y + 0.5f) / dim);
                texels[(size_t(face) * dim + y) * dim + x] = glm::vec4(radiance(dir), 1.0f);
            }
        }
    }
    return pkg;
}

// A uniform environment of radiance L reflects L from every direction.
void testConstantEnvironment() {
    const glm::vec3 radiance(1.0f, 0.5f, 2.0f);
    auto sh = lightc::projectIrradianceSH(makeCubemap(32, [&](const glm::vec3&) { return radiance; }));
    for (int face = 0; face < 6; face++) {
        const glm::vec3 n = lightc::cubemapToDirection(face, 0.3f, 0.7f);
        CHECK(near(lightc::evaluateIrradianceSH(sh, n), radiance, 1e-4f));
    }
}

// Radiance 1 above the horizon and 0 below: irradiance / pi is exactly 1 at +Y and 0 at -Y, and L2 SH represent
// both exactly. Sideways, the exact value is 1/2.
void testHemisphere() {
    auto sh = lightc::projectIrradianceSH(makeCubemap(64, [](const glm::vec3& dir) {
        return glm::vec3(dir.y > 0.0f ? 1.0f : 0.0f);
    }));
    CHECK(near(lightc::evaluateIrradianceSH(sh, { 0.0f, 1.0f, 0.0f }), glm::vec3(1.0f), 1e-2f));
    CHECK(near(lightc::evaluateIrradianceSH(sh, { 0.0f, -1.0f, 0.0f }), glm::vec3(0.0f), 1e-2f));
    CHECK(near(lightc::evaluateIrradianceSH(sh, { 1.0f, 0.0f, 0.0f }), glm::vec3(0.5f), 1e-2f));
    CHECK(near(lightc::evaluateIrradianceSH(sh, { 0.0f, 0.0f, -1.0f }), glm::vec3(0.5f), 1e-2f));
}

// A light from one direction peaks there: the SH must use the same directions as the cubemap's texels.
void testOrientation() {
    const glm::vec3 lightDir = glm::normalize(glm::vec3(0.3f, -0.4f, 0.8f));
    auto sh = lightc::projectIrradianceSH(makeCubemap(32, [&](const glm::vec3& dir) {
        return glm::vec3(std::pow(std::max(glm::dot(dir, lightDir), 0.0f), 8.0f));
    }));
    const float towards = lightc::evaluateIrradianceSH(sh, lightDir).x;
    const float away = lightc::evaluateIrradianceSH(sh, -lightDir).x;
    const float side = lightc::evaluateIrradianceSH(sh, glm::normalize(glm::cross(lightDir, glm::vec3(0, 1, 0)))).x;
    CHECK(towards > side && side > away);
}


// A sun capped to the sky's level keeps its color; dimmer texels are untouched.
void testClampRadiance() {
    std::vector<float> rgb = { 1000.0f, 800.0f, 600.0f, 0.5f, 0.6f, 0.7f };
    lightc::clampRadiance(rgb, 10.0f);
    const float luminance = 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
    CHECK_NEAR(luminance, 10.0f, 1e-4f);
    CHECK_NEAR(rgb[1] / rgb[0], 0.8f, 1e-5f);
    CHECK_NEAR(rgb[2] / rgb[0], 0.6f, 1e-5f);
    CHECK_EQ(rgb[3], 0.5f);
    CHECK_EQ(rgb[4], 0.6f);
    CHECK_EQ(rgb[5], 0.7f);
}

}

void runSphericalHarmonicsTests() {
    ailo::test::run("spherical_harmonics/testConstantEnvironment", testConstantEnvironment);
    ailo::test::run("spherical_harmonics/testHemisphere", testHemisphere);
    ailo::test::run("spherical_harmonics/testOrientation", testOrientation);
    ailo::test::run("spherical_harmonics/testClampRadiance", testClampRadiance);
}

int main() {
    runSphericalHarmonicsTests();
    return ailo::test::finish();
}
