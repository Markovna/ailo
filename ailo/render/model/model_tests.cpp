#include "ModelPackage.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace ailo;
using namespace ailo::model;

namespace {

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

template<typename V>
Mesh makeMesh(VertexLayout layout, const std::vector<V>& vertices, std::vector<uint32_t> indices) {
    Mesh mesh;
    mesh.layout = layout;
    mesh.vertexCount = uint32_t(vertices.size());
    mesh.vertices.resize(vertices.size() * sizeof(V));
    std::memcpy(mesh.vertices.data(), vertices.data(), mesh.vertices.size());
    mesh.indices = std::move(indices);
    return mesh;
}

texture::TexturePackage makeTexture() {
    texture::TexturePackage tex;
    tex.format = texture::TextureFormat::RGBA8;
    tex.width = 2;
    tex.height = 1;
    tex.generateMipmaps = true;
    tex.data = { 1, 2, 3, 4, 5, 6, 7, 8 };
    return tex;
}

ModelPackage makePackage() {
    ModelPackage pkg;

    std::vector<StaticVertex> quad(4);
    for (int i = 0; i < 4; i++) quad[i].position = glm::vec3(float(i), 0.0f, 0.0f);
    pkg.meshes.push_back(makeMesh(VertexLayout::Static, quad, { 0, 1, 2, 2, 3, 0 }));

    std::vector<SkinnedVertex> triangle(3);
    triangle[1].boneIndices = glm::ivec4(1, 0, 0, 0);
    triangle[1].boneWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    pkg.meshes.push_back(makeMesh(VertexLayout::Skinned, triangle, { 0, 1, 2 }));

    Material& lit = pkg.materials.emplace_back();
    lit.name = "Stone";
    lit.material = "assets/materials/lit";
    lit.floats.push_back({ "baseColorFactor", 4, { 0.5f, 0.25f, 1.0f, 1.0f } });
    lit.floats.push_back({ "roughnessFactor", 1, { 0.75f } });
    lit.textures.push_back({ "baseColorMap", { TextureSource::Uri, "textures/stone.png", 0 } });
    lit.textures.push_back({ "normalMap", { TextureSource::Embedded, "", 0 } });
    lit.textures.push_back({ "metallicRoughnessMap", { TextureSource::Key, "builtin://textures/white", 0 } });

    Material& skin = pkg.materials.emplace_back();
    skin.name = "Skin";
    skin.material = "assets/materials/toon";

    pkg.instances.push_back({ 0, 0, glm::mat4(1.0f) });
    pkg.instances.push_back({ 1, 1, glm::mat4(2.0f) });

    pkg.skeleton.push_back({ "Armature", -1, glm::mat4(1.0f), glm::mat4(1.0f), -1 });
    pkg.skeleton.push_back({ "Hips", 0, glm::mat4(3.0f), glm::mat4(0.5f), 0 });
    pkg.skeleton.push_back({ "Spine", 1, glm::mat4(1.0f), glm::mat4(1.0f), 1 });

    Clip& clip = pkg.clips.emplace_back();
    clip.name = "Kick";
    clip.ticksPerSecond = 30.0f;
    clip.duration = 2.0f;
    Channel& channel = clip.channels.emplace_back();
    channel.node = "Hips";
    channel.positions = { { 0.0f, { 1, 2, 3 } }, { 1.0f, { 4, 5, 6 } } };
    channel.rotations = { { 0.5f, glm::quat(0.5f, 0.1f, 0.2f, 0.3f) } };
    channel.scales = { { 0.0f, { 1, 1, 1 } } };

    pkg.textures.push_back(makeTexture());
    return pkg;
}

std::optional<ModelPackage> roundTrip(const ModelPackage& pkg, std::string& error) {
    auto bytes = pkg.serialize();
    return ModelPackage::deserialize(bytes, error);
}

void testRoundTrip() {
    ModelPackage pkg = makePackage();
    assert(pkg.validate().empty());

    std::string error;
    auto out = roundTrip(pkg, error);
    assert(out && error.empty());

    assert(out->meshes.size() == 2);
    assert(out->meshes[0].layout == VertexLayout::Static);
    assert(out->meshes[0].vertexCount == 4);
    assert(out->meshes[0].vertices == pkg.meshes[0].vertices);
    assert(out->meshes[0].indices == pkg.meshes[0].indices);
    assert(out->meshes[1].layout == VertexLayout::Skinned);
    assert(out->meshes[1].vertices == pkg.meshes[1].vertices);

    assert(out->materials.size() == 2);
    const auto& lit = out->materials[0];
    assert(lit.name == "Stone" && lit.material == "assets/materials/lit");
    assert(lit.floats.size() == 2);
    assert(lit.floats[0].name == "baseColorFactor" && lit.floats[0].components == 4);
    assert(lit.floats[0].value[1] == 0.25f);
    assert(lit.floats[1].components == 1 && lit.floats[1].value[0] == 0.75f);
    assert(lit.textures.size() == 3);
    assert(lit.textures[0].texture.source == TextureSource::Uri && lit.textures[0].texture.path == "textures/stone.png");
    assert(lit.textures[1].texture.source == TextureSource::Embedded && lit.textures[1].texture.embeddedIndex == 0);
    assert(lit.textures[2].texture.source == TextureSource::Key);
    assert(out->materials[1].floats.empty() && out->materials[1].textures.empty());

    assert(out->instances.size() == 2);
    assert(out->instances[1].mesh == 1 && out->instances[1].material == 1);
    assert(out->instances[1].transform == glm::mat4(2.0f));

    assert(out->skeleton.size() == 3);
    assert(out->skeleton[1].name == "Hips" && out->skeleton[1].parent == 0);
    assert(out->skeleton[1].localTransform == glm::mat4(3.0f));
    assert(out->skeleton[1].inverseBindPose == glm::mat4(0.5f));
    assert(out->skeleton[0].boneOutputIndex == -1 && out->skeleton[2].boneOutputIndex == 1);

    assert(out->clips.size() == 1);
    const auto& clip = out->clips[0];
    assert(clip.name == "Kick" && clip.ticksPerSecond == 30.0f && clip.duration == 2.0f);
    assert(clip.channels.size() == 1 && clip.channels[0].node == "Hips");
    assert(clip.channels[0].positions.size() == 2 && clip.channels[0].positions[1].value == glm::vec3(4, 5, 6));
    assert(clip.channels[0].rotations[0].time == 0.5f);
    assert(clip.channels[0].rotations[0].value == glm::quat(0.5f, 0.1f, 0.2f, 0.3f));
    assert(clip.channels[0].scales.size() == 1);

    assert(out->textures.size() == 1);
    assert(out->textures[0].data == pkg.textures[0].data && out->textures[0].generateMipmaps);
}

void testIndexWidth() {
    ModelPackage pkg;
    pkg.meshes.push_back(makeMesh(VertexLayout::Static, std::vector<StaticVertex>(65536), { 0, 1, 65535 }));
    pkg.meshes.push_back(makeMesh(VertexLayout::Static, std::vector<StaticVertex>(65537), { 0, 1, 65536 }));
    assert(fitsUint16Indices(pkg.meshes[0]) && !fitsUint16Indices(pkg.meshes[1]));
    assert(pkg.validate().empty());

    std::string error;
    auto out = roundTrip(pkg, error);
    assert(out && error.empty());
    assert(out->meshes[0].indices == pkg.meshes[0].indices);
    assert(out->meshes[1].indices == pkg.meshes[1].indices);

    // One more vertex switches the mesh to 32-bit indices, 2 more bytes per index.
    ModelPackage wide = pkg;
    wide.meshes.pop_back();
    ModelPackage narrow = wide;
    wide.meshes[0].vertices.resize(size_t(65537) * sizeof(StaticVertex));
    wide.meshes[0].vertexCount = 65537;
    assert(wide.serialize().size() - narrow.serialize().size() == sizeof(StaticVertex) + 3 * sizeof(uint16_t));
}

void testEmptyPackage() {
    std::string error;
    auto out = roundTrip(ModelPackage {}, error);
    assert(out && out->meshes.empty() && out->instances.empty() && out->skeleton.empty());
}

void testValidate() {
    {
        auto pkg = makePackage();
        pkg.meshes[0].indices.push_back(0);
        assert(contains(pkg.validate(), "multiple of 3"));
    }
    {
        auto pkg = makePackage();
        pkg.meshes[0].indices[0] = 4;
        assert(contains(pkg.validate(), "out of range"));
    }
    {
        auto pkg = makePackage();
        pkg.meshes[0].vertices.pop_back();
        assert(contains(pkg.validate(), "vertex data"));
    }
    {
        auto pkg = makePackage();
        pkg.skeleton.clear();
        assert(contains(pkg.validate(), "no skeleton"));
    }
    {
        auto pkg = makePackage();
        pkg.skeleton[1].parent = 2;
        assert(contains(pkg.validate(), "parents must come first"));
    }
    {
        auto pkg = makePackage();
        pkg.instances[0].material = 7;
        assert(contains(pkg.validate(), "uses material 7"));
    }
    {
        auto pkg = makePackage();
        pkg.materials[0].textures[1].texture.embeddedIndex = 1;
        assert(contains(pkg.validate(), "embedded texture 1"));
    }
    {
        auto pkg = makePackage();
        pkg.materials[0].textures[0].texture.path.clear();
        assert(contains(pkg.validate(), "empty path"));
    }
    {
        auto pkg = makePackage();
        pkg.materials[1].material.clear();
        assert(contains(pkg.validate(), "names no material"));
    }
    {
        auto pkg = makePackage();
        pkg.textures[0].data.pop_back();
        assert(contains(pkg.validate(), "embedded texture 0"));
    }
}

void testDeserializeRejectsBadPackages() {
    std::string error;
    auto bytes = makePackage().serialize();

    std::vector<uint8_t> notAPackage = { 'n', 'o', 'p', 'e', 0, 0, 0, 0 };
    assert(!ModelPackage::deserialize(notAPackage, error) && contains(error, "not a model package"));

    auto wrongVersion = bytes;
    wrongVersion[4] = uint8_t(kModelPackageVersion + 1);
    assert(!ModelPackage::deserialize(wrongVersion, error) && contains(error, "version"));

    auto truncated = bytes;
    truncated.resize(truncated.size() - 3);
    assert(!ModelPackage::deserialize(truncated, error) && contains(error, "truncated"));

    // Unknown chunks are skipped.
    auto withUnknown = bytes;
    const uint8_t unknown[] = { 'X', 'T', 'R', 'A', 2, 0, 0, 0, 0xAB, 0xCD };
    withUnknown.insert(withUnknown.end(), std::begin(unknown), std::end(unknown));
    auto out = ModelPackage::deserialize(withUnknown, error);
    assert(out && out->meshes.size() == 2);
}

}

int main() {
    testRoundTrip();
    testIndexWidth();
    testEmptyPackage();
    testValidate();
    testDeserializeRejectsBadPackages();

    std::cout << "All model tests passed" << std::endl;
    return 0;
}
