#include "TexturePackage.h"
#include "TexturePacker.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "stb_image/stb_image_write.h"

using namespace ailo::texture;
namespace fs = std::filesystem;

namespace {

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

void appendBytes(void* context, void* data, int size) {
    auto* out = static_cast<std::vector<uint8_t>*>(context);
    auto* bytes = static_cast<const uint8_t*>(data);
    out->insert(out->end(), bytes, bytes + size);
}

std::vector<uint8_t> encodePng(int width, int height, int channels, const std::vector<uint8_t>& pixels) {
    std::vector<uint8_t> out;
    stbi_write_png_to_func(appendBytes, &out, width, height, channels, pixels.data(), width * channels);
    return out;
}

std::vector<uint8_t> encodeHdr(int width, int height, const std::vector<float>& rgb) {
    std::vector<uint8_t> out;
    stbi_write_hdr_to_func(appendBytes, &out, width, height, 3, rgb.data());
    return out;
}

std::vector<uint8_t> solid(int width, int height, std::array<uint8_t, 4> rgba) {
    std::vector<uint8_t> pixels;
    for (int i = 0; i < width * height; i++) pixels.insert(pixels.end(), rgba.begin(), rgba.end());
    return pixels;
}

void writeFile(const fs::path& path, const std::vector<uint8_t>& data) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    assert(file);
}

// A fresh, empty directory for the tests that need files.
fs::path scratchDir() {
    auto dir = fs::temp_directory_path() / "ailo_texture_tests";
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

TexturePackage makeCubemapWithLevels() {
    TexturePackage pkg;
    pkg.type = TextureType::Cubemap;
    pkg.format = TextureFormat::RGBA32F;
    pkg.width = 4;
    pkg.height = 4;
    pkg.levels = 3;
    pkg.data.resize(pkg.expectedDataSize());
    for (size_t i = 0; i < pkg.data.size(); i++) pkg.data[i] = uint8_t(i * 7);
    return pkg;
}

void testMipLevelCount() {
    assert(mipLevelCount(1, 1) == 1);
    assert(mipLevelCount(2, 1) == 2);
    assert(mipLevelCount(256, 256) == 9);
    assert(mipLevelCount(300, 100) == 9);
    assert(mipLevelCount(1, 4096) == 13);
}

void testLayout() {
    auto pkg = makeCubemapWithLevels();
    assert(pkg.layers() == 6);
    assert(pkg.allocatedLevels() == 3);
    assert(pkg.imageSize(0) == 4 * 4 * 16);
    assert(pkg.imageSize(1) == 2 * 2 * 16);
    assert(pkg.imageSize(2) == 1 * 1 * 16);
    assert(pkg.expectedDataSize() == 6 * (256 + 64 + 16));

    // Level-major: all faces of level 0, then all faces of level 1, ...
    assert(pkg.image(0, 0).data() == pkg.data.data());
    assert(pkg.image(0, 5).data() == pkg.data.data() + 5 * 256);
    assert(pkg.image(1, 0).data() == pkg.data.data() + 6 * 256);
    assert(pkg.image(2, 3).data() == pkg.data.data() + 6 * 256 + 6 * 64 + 3 * 16);
    assert(pkg.image(2, 5).size() == 16);

    TexturePackage generated;
    generated.width = 300;
    generated.height = 100;
    generated.generateMipmaps = true;
    assert(generated.allocatedLevels() == 9);
}

void testRoundTrip() {
    auto cube = makeCubemapWithLevels();
    std::string error;
    auto restored = TexturePackage::deserialize(cube.serialize(), error);
    assert(restored && error.empty());
    assert(restored->type == TextureType::Cubemap);
    assert(restored->format == TextureFormat::RGBA32F);
    assert(restored->width == 4 && restored->height == 4);
    assert(restored->levels == 3);
    assert(!restored->generateMipmaps);
    assert(restored->data == cube.data);

    TexturePackage flat;
    flat.format = TextureFormat::RGBA8;
    flat.width = 3;
    flat.height = 2;
    flat.generateMipmaps = true;
    flat.data = solid(3, 2, { 1, 2, 3, 4 });
    restored = TexturePackage::deserialize(flat.serialize(), error);
    assert(restored);
    assert(restored->type == TextureType::Texture2D);
    assert(restored->format == TextureFormat::RGBA8);
    assert(restored->generateMipmaps);
    assert(restored->data == flat.data);
}

void testDeserializeRejectsBadPackages() {
    auto good = makeCubemapWithLevels().serialize();
    std::string error;

    auto badMagic = good;
    badMagic[0] = 'X';
    assert(!TexturePackage::deserialize(badMagic, error));
    assert(contains(error, "not a texture package"));

    auto badVersion = good;
    badVersion[4] = 99;
    assert(!TexturePackage::deserialize(badVersion, error));
    assert(contains(error, "version 99"));

    auto truncated = good;
    truncated.resize(truncated.size() - 1);
    assert(!TexturePackage::deserialize(truncated, error));
    assert(contains(error, "truncated"));

    assert(!TexturePackage::deserialize(std::span<const uint8_t>(), error));

    auto wrongSize = makeCubemapWithLevels();
    wrongSize.data.pop_back();
    assert(!TexturePackage::deserialize(wrongSize.serialize(), error));
    assert(contains(error, "expected"));

    auto mipsWithLevels = makeCubemapWithLevels();
    mipsWithLevels.generateMipmaps = true;
    assert(!TexturePackage::deserialize(mipsWithLevels.serialize(), error));
    assert(contains(error, "generateMipmaps"));

    auto tooManyLevels = makeCubemapWithLevels();
    tooManyLevels.levels = 4;
    tooManyLevels.data.resize(tooManyLevels.expectedDataSize());
    assert(!TexturePackage::deserialize(tooManyLevels.serialize(), error));
    assert(contains(error, "at most 3"));

    TexturePackage notSquare;
    notSquare.type = TextureType::Cubemap;
    notSquare.width = 4;
    notSquare.height = 2;
    notSquare.data.resize(notSquare.expectedDataSize());
    assert(!TexturePackage::deserialize(notSquare.serialize(), error));
    assert(contains(error, "square"));

    TexturePackage empty;
    assert(!TexturePackage::deserialize(empty.serialize(), error));
    assert(contains(error, "no pixels"));
}

void testDeserializeSkipsUnknownChunks() {
    auto data = makeCubemapWithLevels().serialize();
    const uint8_t extra[] = { 'X', 'T', 'R', 'A', 3, 0, 0, 0, 1, 2, 3 };
    data.insert(data.end(), std::begin(extra), std::end(extra));

    std::string error;
    auto restored = TexturePackage::deserialize(data, error);
    assert(restored && restored->data == makeCubemapWithLevels().data);
}

void testPackFromPixels() {
    const std::vector<uint8_t> bgra { 10, 20, 30, 40,   50, 60, 70, 80 };
    std::string error;

    auto pkg = packTextureFromPixels(bgra.data(), 2, 1, PixelLayout::BGRA8, {}, error);
    assert(pkg);
    assert(pkg->format == TextureFormat::RGBA8_SRGB);
    assert(pkg->generateMipmaps);
    assert(pkg->data == (std::vector<uint8_t> { 30, 20, 10, 40,   70, 60, 50, 80 }));

    pkg = packTextureFromPixels(bgra.data(), 2, 1, PixelLayout::RGBA8, { .srgb = false, .mipmaps = false }, error);
    assert(pkg);
    assert(pkg->format == TextureFormat::RGBA8);
    assert(!pkg->generateMipmaps);
    assert(pkg->data == bgra);

    // A 1x1 texture has no mip chain to generate.
    pkg = packTextureFromPixels(bgra.data(), 1, 1, PixelLayout::RGBA8, {}, error);
    assert(pkg && !pkg->generateMipmaps);

    assert(!packTextureFromPixels(nullptr, 2, 1, PixelLayout::RGBA8, {}, error));
    assert(!packTextureFromPixels(bgra.data(), 0, 1, PixelLayout::RGBA8, {}, error));
}

void testPackFromMemory() {
    std::string error;

    // RGB source: alpha is filled in. PNG is lossless, so the pixels come back exactly.
    const std::vector<uint8_t> rgb { 255, 0, 0,   0, 255, 0,   0, 0, 255,   9, 8, 7 };
    auto pkg = packTextureFromMemory(encodePng(2, 2, 3, rgb), { .srgb = false }, error);
    assert(pkg);
    assert(pkg->type == TextureType::Texture2D);
    assert(pkg->format == TextureFormat::RGBA8);
    assert(pkg->width == 2 && pkg->height == 2 && pkg->levels == 1);
    assert(pkg->data == (std::vector<uint8_t> { 255, 0, 0, 255,   0, 255, 0, 255,   0, 0, 255, 255,   9, 8, 7, 255 }));

    // HDR source: RGBA32F whatever srgb says.
    const std::vector<float> hdr { 0.5f, 2.0f, 8.0f,   1.0f, 1.0f, 1.0f };
    pkg = packTextureFromMemory(encodeHdr(2, 1, hdr), { .srgb = true }, error);
    assert(pkg);
    assert(pkg->format == TextureFormat::RGBA32F);
    assert(pkg->data.size() == 2 * 16);
    float texel[4];
    std::memcpy(texel, pkg->data.data(), sizeof(texel));
    assert(std::abs(texel[0] - 0.5f) < 0.01f && std::abs(texel[1] - 2.0f) < 0.02f && std::abs(texel[2] - 8.0f) < 0.1f);
    assert(texel[3] == 1.0f);

    const std::vector<uint8_t> garbage { 1, 2, 3, 4, 5, 6, 7, 8 };
    assert(!packTextureFromMemory(garbage, {}, error));
    assert(contains(error, "cannot decode image"));

    assert(!packTextureFromMemory(std::span<const uint8_t>(), {}, error));
}

void testPackFile() {
    auto dir = scratchDir();
    std::string error;

    auto missing = dir / "missing.png";
    assert(!packTexture(missing, {}, error));
    assert(contains(error, "missing.png") && contains(error, "cannot read"));

    auto path = dir / "red.png";
    writeFile(path, encodePng(4, 2, 4, solid(4, 2, { 255, 0, 0, 255 })));
    auto pkg = packTexture(path, {}, error);
    assert(pkg);
    assert(pkg->width == 4 && pkg->height == 2);
    assert(pkg->format == TextureFormat::RGBA8_SRGB);
    assert(pkg->generateMipmaps);
    assert(pkg->data == solid(4, 2, { 255, 0, 0, 255 }));

    auto broken = dir / "broken.png";
    writeFile(broken, { 1, 2, 3 });
    assert(!packTexture(broken, {}, error));
    assert(contains(error, "broken.png") && contains(error, "cannot decode"));
}

void testCubemapFacePaths() {
    auto faces = cubemapFacePaths("sky/yokohama.jpg");
    assert(faces[0] == fs::path("sky/yokohama_px.jpg"));
    assert(faces[1] == fs::path("sky/yokohama_nx.jpg"));
    assert(faces[2] == fs::path("sky/yokohama_py.jpg"));
    assert(faces[3] == fs::path("sky/yokohama_ny.jpg"));
    assert(faces[4] == fs::path("sky/yokohama_pz.jpg"));
    assert(faces[5] == fs::path("sky/yokohama_nz.jpg"));
}

void testPackCubemap() {
    auto dir = scratchDir();
    auto faces = cubemapFacePaths(dir / "sky.png");
    for (size_t i = 0; i < faces.size(); i++) {
        writeFile(faces[i], encodePng(2, 2, 4, solid(2, 2, { uint8_t(i), uint8_t(10 * i), 0, 255 })));
    }

    std::string error;
    auto pkg = packCubemap(faces, { .srgb = true, .mipmaps = true }, error);
    assert(pkg);
    assert(pkg->type == TextureType::Cubemap);
    assert(pkg->format == TextureFormat::RGBA8_SRGB);
    assert(pkg->width == 2 && pkg->height == 2 && pkg->levels == 1);
    assert(pkg->generateMipmaps);
    for (uint32_t face = 0; face < 6; face++) {
        auto expected = solid(2, 2, { uint8_t(face), uint8_t(10 * face), 0, 255 });
        auto image = pkg->image(0, face);
        assert(std::equal(image.begin(), image.end(), expected.begin(), expected.end()));
    }

    // A face of another size is rejected and named.
    writeFile(faces[3], encodePng(4, 4, 4, solid(4, 4, { 0, 0, 0, 255 })));
    assert(!packCubemap(faces, {}, error));
    assert(contains(error, "sky_ny.png") && contains(error, "4x4") && contains(error, "sky_px.png"));

    // So is a face that isn't square.
    writeFile(faces[0], encodePng(4, 2, 4, solid(4, 2, { 0, 0, 0, 255 })));
    assert(!packCubemap(faces, {}, error));
    assert(contains(error, "sky_px.png") && contains(error, "square"));

    fs::remove(faces[5]);
    writeFile(faces[0], encodePng(2, 2, 4, solid(2, 2, { 0, 0, 0, 255 })));
    writeFile(faces[3], encodePng(2, 2, 4, solid(2, 2, { 0, 0, 0, 255 })));
    assert(!packCubemap(faces, {}, error));
    assert(contains(error, "sky_nz.png") && contains(error, "cannot read"));

    fs::remove_all(dir);
}

}

int main() {
    testMipLevelCount();
    testLayout();
    testRoundTrip();
    testDeserializeRejectsBadPackages();
    testDeserializeSkipsUnknownChunks();
    testPackFromPixels();
    testPackFromMemory();
    testPackFile();
    testCubemapFacePaths();
    testPackCubemap();

    std::cout << "All texture tests passed" << std::endl;
    return 0;
}
