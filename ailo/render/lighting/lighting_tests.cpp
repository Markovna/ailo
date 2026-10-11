#include "LightingPackage.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "Cubemap.h"
#include "SphericalHarmonics.h"

using namespace ailo;
using namespace ailo::lighting;

namespace {

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

bool near(const glm::vec3& a, const glm::vec3& b, float tolerance) {
    return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(tolerance)));
}

// An rgba32f cubemap whose texels hold radiance(direction).
template<typename F>
texture::TexturePackage makeCubemap(uint32_t dim, F radiance) {
    texture::TexturePackage pkg;
    pkg.type = texture::TextureType::Cubemap;
    pkg.format = texture::TextureFormat::RGBA32F;
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

LightingPackage makePackage() {
    LightingPackage pkg;
    for (size_t i = 0; i < pkg.irradianceSH.size(); i++) {
        pkg.irradianceSH[i] = glm::vec3(float(i), -float(i) * 0.5f, 0.25f);
    }
    pkg.reflections = makeCubemap(2, [](const glm::vec3& dir) { return dir; });
    return pkg;
}

void testRoundTrip() {
    LightingPackage pkg = makePackage();
    assert(pkg.validate().empty());

    auto data = pkg.serialize();
    std::string error;
    auto loaded = LightingPackage::deserialize(data, error);
    assert(loaded && error.empty());
    assert(loaded->irradianceSH == pkg.irradianceSH);
    assert(loaded->reflections.type == texture::TextureType::Cubemap);
    assert(loaded->reflections.width == 2);
    assert(loaded->reflections.data == pkg.reflections.data);
}

void testValidate() {
    LightingPackage pkg = makePackage();
    pkg.reflections.type = texture::TextureType::Texture2D;
    assert(contains(pkg.validate(), "cubemap"));

    pkg = makePackage();
    pkg.irradianceSH[4].y = NAN;
    assert(contains(pkg.validate(), "non-finite"));

    pkg = makePackage();
    pkg.reflections.data.pop_back();
    assert(contains(pkg.validate(), "reflections"));
}

void testDeserializeRejectsBadPackages() {
    std::string error;
    auto data = makePackage().serialize();

    std::vector<uint8_t> wrongMagic = data;
    wrongMagic[0] ^= 0xff;
    assert(!LightingPackage::deserialize(wrongMagic, error));
    assert(contains(error, "not a lighting package"));

    std::vector<uint8_t> wrongVersion = data;
    uint32_t version = kLightingPackageVersion + 1;
    std::memcpy(wrongVersion.data() + 4, &version, sizeof(version));
    assert(!LightingPackage::deserialize(wrongVersion, error));
    assert(contains(error, "version"));

    std::vector<uint8_t> truncated(data.begin(), data.end() - 3);
    assert(!LightingPackage::deserialize(truncated, error));
    assert(contains(error, "truncated"));

    // The header alone: valid chunks, but none of them.
    std::vector<uint8_t> empty(data.begin(), data.begin() + 8);
    assert(!LightingPackage::deserialize(empty, error));
    assert(contains(error, "no irradiance SH"));
}

// A uniform environment of radiance L reflects L from every direction.
void testConstantEnvironment() {
    const glm::vec3 radiance(1.0f, 0.5f, 2.0f);
    auto sh = lightc::projectIrradianceSH(makeCubemap(32, [&](const glm::vec3&) { return radiance; }));
    for (int face = 0; face < 6; face++) {
        const glm::vec3 n = lightc::cubemapToDirection(face, 0.3f, 0.7f);
        assert(near(lightc::evaluateIrradianceSH(sh, n), radiance, 1e-4f));
    }
}

// Radiance 1 above the horizon and 0 below: irradiance / pi is exactly 1 at +Y and 0 at -Y, and L2 SH represent
// both exactly. Sideways, the exact value is 1/2.
void testHemisphere() {
    auto sh = lightc::projectIrradianceSH(makeCubemap(64, [](const glm::vec3& dir) {
        return glm::vec3(dir.y > 0.0f ? 1.0f : 0.0f);
    }));
    assert(near(lightc::evaluateIrradianceSH(sh, { 0.0f, 1.0f, 0.0f }), glm::vec3(1.0f), 1e-2f));
    assert(near(lightc::evaluateIrradianceSH(sh, { 0.0f, -1.0f, 0.0f }), glm::vec3(0.0f), 1e-2f));
    assert(near(lightc::evaluateIrradianceSH(sh, { 1.0f, 0.0f, 0.0f }), glm::vec3(0.5f), 1e-2f));
    assert(near(lightc::evaluateIrradianceSH(sh, { 0.0f, 0.0f, -1.0f }), glm::vec3(0.5f), 1e-2f));
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
    assert(towards > side && side > away);
}

}

int main() {
    testRoundTrip();
    testValidate();
    testDeserializeRejectsBadPackages();
    testConstantEnvironment();
    testHemisphere();
    testOrientation();
    std::cout << "lighting tests passed\n";
    return 0;
}
