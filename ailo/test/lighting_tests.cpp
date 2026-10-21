#include "render/lighting/LightingPackage.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace ailo;
using namespace ailo::lighting;

namespace {

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

// A 2x2 rgba32f cubemap whose texels count up.
texture::TexturePackage makeCubemap() {
    texture::TexturePackage pkg;
    pkg.type = texture::TextureType::Cubemap;
    pkg.format = texture::TextureFormat::RGBA32F;
    pkg.width = 2;
    pkg.height = 2;
    pkg.levels = 1;
    pkg.data.resize(pkg.expectedDataSize());
    auto* texels = reinterpret_cast<float*>(pkg.data.data());
    for (size_t i = 0; i < pkg.data.size() / sizeof(float); i++) {
        texels[i] = float(i);
    }
    return pkg;
}

LightingPackage makePackage() {
    LightingPackage pkg;
    for (size_t i = 0; i < pkg.irradianceSH.size(); i++) {
        pkg.irradianceSH[i] = glm::vec3(float(i), -float(i) * 0.5f, 0.25f);
    }
    pkg.reflections = makeCubemap();
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

}

void runLightingTests() {
    testRoundTrip();
    testValidate();
    testDeserializeRejectsBadPackages();
    std::cout << "All lighting tests passed" << std::endl;
}
