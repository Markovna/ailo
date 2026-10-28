#include "render/material/MaterialInstancePackage.h"
#include "Testing.h"

#include <bit>
#include <string>
#include <vector>

#include "embedded/test_assets.h"

using namespace ailo;
using namespace ailo::material;

namespace {

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

std::vector<uint32_t> floatWords(std::initializer_list<float> values) {
    std::vector<uint32_t> words;
    for (float v : values) words.push_back(std::bit_cast<uint32_t>(v));
    return words;
}

void testRoundTrip() {
    MaterialInstancePackage pkg;
    pkg.material = "materials/lit.mat";
    pkg.parameters.push_back({ "baseColorFactor", ParameterType::Float4, floatWords({ 1.0f, 0.5f, 0.25f, 1.0f }) });
    pkg.parameters.push_back({ "layers", ParameterType::Int2, { 3u, std::bit_cast<uint32_t>(-4) } });
    pkg.textures.push_back({ "baseColorMap", "textures/rock.png.tex" });
    pkg.textures.push_back({ "normalMap", "engine:/textures/flat.tex" });

    auto data = pkg.serialize();
    std::string error;
    auto loaded = MaterialInstancePackage::deserialize(data, error);
    REQUIRE(loaded && error.empty());
    CHECK_EQ(loaded->material, pkg.material);
    REQUIRE_EQ(loaded->parameters.size(), 2u);
    CHECK_EQ(loaded->parameters[0].name, "baseColorFactor");
    CHECK_EQ(loaded->parameters[0].type, ParameterType::Float4);
    CHECK_EQ(loaded->parameters[0].value, pkg.parameters[0].value);
    CHECK_EQ(loaded->parameters[1].value, pkg.parameters[1].value);
    REQUIRE_EQ(loaded->textures.size(), 2u);
    CHECK_EQ(loaded->textures[1].name, "normalMap");
    CHECK_EQ(loaded->textures[1].texture, "engine:/textures/flat.tex");
}

void testDeserializeRejectsBadPackages() {
    std::string error;
    const std::string text = "{ \"material\": \"lit.mat\" }";
    CHECK(!MaterialInstancePackage::deserialize({ reinterpret_cast<const uint8_t*>(text.data()), text.size() }, error));
    CHECK(contains(error, "not a material instance package"));

    auto data = MaterialInstancePackage { .material = "lit.mat" }.serialize();
    auto badVersion = data;
    badVersion[4] ^= 0xff;
    CHECK(!MaterialInstancePackage::deserialize(badVersion, error));
    CHECK(contains(error, "version"));

    data.resize(data.size() - 2);
    CHECK(!MaterialInstancePackage::deserialize(data, error));
}

// red.matinst is compiled by matc through embed_assets.
void testCompiledInstance() {
    std::string error;
    auto pkg = MaterialInstancePackage::deserialize(embedded::test_assets::kMaterialsRed, error);
    REQUIRE(pkg);
    CHECK_EQ(pkg->material, "materials/tinted.mat");

    REQUIRE_EQ(pkg->parameters.size(), 5u);
    CHECK_EQ(pkg->parameters[0].name, "tint");
    CHECK_EQ(pkg->parameters[0].type, ParameterType::Float4);
    CHECK_EQ(pkg->parameters[0].value, floatWords({ 1.0f, 0.0f, 0.0f, 1.0f }));
    CHECK_EQ(pkg->parameters[1].type, ParameterType::Float);
    CHECK_EQ(pkg->parameters[1].value, floatWords({ 2.5f }));
    CHECK_EQ(pkg->parameters[2].type, ParameterType::Int);
    CHECK_EQ(pkg->parameters[2].value, (std::vector<uint32_t> { 3u, std::bit_cast<uint32_t>(-4) }));
    CHECK_EQ(pkg->parameters[3].type, ParameterType::Bool);
    CHECK_EQ(pkg->parameters[3].value, std::vector<uint32_t> { 1u });
    CHECK_EQ(pkg->parameters[4].type, ParameterType::Float3x3);
    CHECK_EQ(pkg->parameters[4].value, floatWords({ 1, 2, 3, 4, 5, 6, 7, 8, 9 }));

    REQUIRE_EQ(pkg->textures.size(), 1u);
    CHECK_EQ(pkg->textures[0].name, "baseColorMap");
    CHECK_EQ(pkg->textures[0].texture, "builtin://textures/black");
}

}

void runMaterialInstanceTests() {
    ailo::test::run("material_instance/testRoundTrip", testRoundTrip);
    ailo::test::run("material_instance/testDeserializeRejectsBadPackages", testDeserializeRejectsBadPackages);
    ailo::test::run("material_instance/testCompiledInstance", testCompiledInstance);
}
