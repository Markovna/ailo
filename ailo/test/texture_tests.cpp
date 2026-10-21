#include "render/texture/TexturePackage.h"
#include "render/texture/TexturePacker.h"
#include "Testing.h"

#include <algorithm>
#include <array>
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
    REQUIRE(file);
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
    CHECK_EQ(mipLevelCount(1, 1), 1);
    CHECK_EQ(mipLevelCount(2, 1), 2);
    CHECK_EQ(mipLevelCount(256, 256), 9);
    CHECK_EQ(mipLevelCount(300, 100), 9);
    CHECK_EQ(mipLevelCount(1, 4096), 13);
}

void testLayout() {
    auto pkg = makeCubemapWithLevels();
    CHECK_EQ(pkg.layers(), 6);
    CHECK_EQ(pkg.allocatedLevels(), 3);
    CHECK_EQ(pkg.imageSize(0), 4 * 4 * 16);
    CHECK_EQ(pkg.imageSize(1), 2 * 2 * 16);
    CHECK_EQ(pkg.imageSize(2), 1 * 1 * 16);
    CHECK_EQ(pkg.expectedDataSize(), 6 * (256 + 64 + 16));

    // Level-major: all faces of level 0, then all faces of level 1, ...
    CHECK_EQ(pkg.image(0, 0).data(), pkg.data.data());
    CHECK_EQ(pkg.image(0, 5).data(), pkg.data.data() + 5 * 256);
    CHECK_EQ(pkg.image(1, 0).data(), pkg.data.data() + 6 * 256);
    CHECK_EQ(pkg.image(2, 3).data(), pkg.data.data() + 6 * 256 + 6 * 64 + 3 * 16);
    CHECK_EQ(pkg.image(2, 5).size(), 16);

    TexturePackage generated;
    generated.width = 300;
    generated.height = 100;
    generated.generateMipmaps = true;
    CHECK_EQ(generated.allocatedLevels(), 9);
}

void testHalfFloat() {
    CHECK_EQ(blockBytes(TextureFormat::RGBA16F), 8);
    CHECK_EQ(toString(TextureFormat::RGBA16F), "rgba16f");

    auto cube = makeCubemapWithLevels();
    cube.format = TextureFormat::RGBA16F;
    cube.data.resize(cube.expectedDataSize());
    CHECK_EQ(cube.data.size(), 6 * 8 * (16 + 4 + 1));
    CHECK(cube.validate().empty());

    std::string error;
    auto restored = TexturePackage::deserialize(cube.serialize(), error);
    REQUIRE(restored && error.empty());
    CHECK_EQ(restored->format, TextureFormat::RGBA16F);
    CHECK_EQ(restored->data, cube.data);
}

void testCompressedLayout() {
    CHECK(!isCompressed(TextureFormat::RGBA8) && blockExtent(TextureFormat::RGBA8) == 1);
    CHECK(isCompressed(TextureFormat::BC7_SRGB) && blockExtent(TextureFormat::BC7_SRGB) == 4);
    CHECK(isSrgb(TextureFormat::BC1_SRGB) && !isSrgb(TextureFormat::BC5));

    // 10x6 bc1: 3x2 blocks, then 5x3 (2x1), 2x1 (1x1) and 1x1 (1x1): levels below a block still take one.
    TexturePackage bc1;
    bc1.format = TextureFormat::BC1;
    bc1.width = 10;
    bc1.height = 6;
    bc1.levels = 4;
    CHECK_EQ(bc1.imageSize(0), 3 * 2 * 8);
    CHECK_EQ(bc1.imageSize(1), 2 * 1 * 8);
    CHECK(bc1.imageSize(2) == 8 && bc1.imageSize(3) == 8);
    bc1.data.resize(bc1.expectedDataSize());
    CHECK_EQ(bc1.data.size(), (6 + 2 + 1 + 1) * 8);

    std::string error;
    auto restored = TexturePackage::deserialize(bc1.serialize(), error);
    CHECK(restored && restored->format == TextureFormat::BC1);

    TexturePackage cube;
    cube.type = TextureType::Cubemap;
    cube.format = TextureFormat::BC6H;
    cube.width = cube.height = 8;
    cube.levels = 4;
    cube.data.resize(cube.expectedDataSize());
    CHECK_EQ(cube.data.size(), 6 * 16 * (4 + 1 + 1 + 1));
    CHECK_EQ(cube.image(1, 2).data(), cube.data.data() + 6 * 4 * 16 + 2 * 16);
    restored = TexturePackage::deserialize(cube.serialize(), error);
    CHECK(restored && restored->format == TextureFormat::BC6H);

    // The engine cannot generate mipmaps for block-compressed images.
    TexturePackage generated = bc1;
    generated.levels = 1;
    generated.generateMipmaps = true;
    generated.data.resize(generated.expectedDataSize());
    CHECK(!TexturePackage::deserialize(generated.serialize(), error));
    CHECK(contains(error, "cannot generate mipmaps"));

    TexturePackage mips = generated;
    mips.generateMipmaps = false;
    CHECK(!generateMipLevels(mips, error));
    CHECK(contains(error, "bc1"));
}

void testRoundTrip() {
    auto cube = makeCubemapWithLevels();
    std::string error;
    auto restored = TexturePackage::deserialize(cube.serialize(), error);
    REQUIRE(restored && error.empty());
    CHECK_EQ(restored->type, TextureType::Cubemap);
    CHECK_EQ(restored->format, TextureFormat::RGBA32F);
    CHECK(restored->width == 4 && restored->height == 4);
    CHECK_EQ(restored->levels, 3);
    CHECK(!restored->generateMipmaps);
    CHECK_EQ(restored->data, cube.data);

    TexturePackage flat;
    flat.format = TextureFormat::RGBA8;
    flat.width = 3;
    flat.height = 2;
    flat.generateMipmaps = true;
    flat.data = solid(3, 2, { 1, 2, 3, 4 });
    restored = TexturePackage::deserialize(flat.serialize(), error);
    REQUIRE(restored);
    CHECK_EQ(restored->type, TextureType::Texture2D);
    CHECK_EQ(restored->format, TextureFormat::RGBA8);
    CHECK(restored->generateMipmaps);
    CHECK_EQ(restored->data, flat.data);
}

void testDeserializeRejectsBadPackages() {
    auto good = makeCubemapWithLevels().serialize();
    std::string error;

    auto badMagic = good;
    badMagic[0] = 'X';
    CHECK(!TexturePackage::deserialize(badMagic, error));
    CHECK(contains(error, "not a texture package"));

    auto badVersion = good;
    badVersion[4] = 99;
    CHECK(!TexturePackage::deserialize(badVersion, error));
    CHECK(contains(error, "version 99"));

    auto truncated = good;
    truncated.resize(truncated.size() - 1);
    CHECK(!TexturePackage::deserialize(truncated, error));
    CHECK(contains(error, "truncated"));

    CHECK(!TexturePackage::deserialize(std::span<const uint8_t>(), error));

    auto wrongSize = makeCubemapWithLevels();
    wrongSize.data.pop_back();
    CHECK(!TexturePackage::deserialize(wrongSize.serialize(), error));
    CHECK(contains(error, "expected"));

    auto mipsWithLevels = makeCubemapWithLevels();
    mipsWithLevels.generateMipmaps = true;
    CHECK(!TexturePackage::deserialize(mipsWithLevels.serialize(), error));
    CHECK(contains(error, "generateMipmaps"));

    auto tooManyLevels = makeCubemapWithLevels();
    tooManyLevels.levels = 4;
    tooManyLevels.data.resize(tooManyLevels.expectedDataSize());
    CHECK(!TexturePackage::deserialize(tooManyLevels.serialize(), error));
    CHECK(contains(error, "at most 3"));

    TexturePackage notSquare;
    notSquare.type = TextureType::Cubemap;
    notSquare.width = 4;
    notSquare.height = 2;
    notSquare.data.resize(notSquare.expectedDataSize());
    CHECK(!TexturePackage::deserialize(notSquare.serialize(), error));
    CHECK(contains(error, "square"));

    TexturePackage empty;
    CHECK(!TexturePackage::deserialize(empty.serialize(), error));
    CHECK(contains(error, "no pixels"));
}

void testDeserializeSkipsUnknownChunks() {
    auto data = makeCubemapWithLevels().serialize();
    const uint8_t extra[] = { 'X', 'T', 'R', 'A', 3, 0, 0, 0, 1, 2, 3 };
    data.insert(data.end(), std::begin(extra), std::end(extra));

    std::string error;
    auto restored = TexturePackage::deserialize(data, error);
    CHECK(restored && restored->data == makeCubemapWithLevels().data);
}

void testPackFromPixels() {
    const std::vector<uint8_t> bgra { 10, 20, 30, 40,   50, 60, 70, 80 };
    std::string error;

    auto pkg = packTextureFromPixels(bgra.data(), 2, 1, PixelLayout::BGRA8, {}, error);
    REQUIRE(pkg);
    CHECK_EQ(pkg->format, TextureFormat::RGBA8_SRGB);
    CHECK(pkg->generateMipmaps);
    CHECK_EQ(pkg->data, (std::vector<uint8_t> { 30, 20, 10, 40,   70, 60, 50, 80 }));

    pkg = packTextureFromPixels(bgra.data(), 2, 1, PixelLayout::RGBA8, { .srgb = false, .mipmaps = MipmapMode::None }, error);
    REQUIRE(pkg);
    CHECK_EQ(pkg->format, TextureFormat::RGBA8);
    CHECK(!pkg->generateMipmaps);
    CHECK_EQ(pkg->data, bgra);

    // A 1x1 texture has no mip chain to generate.
    pkg = packTextureFromPixels(bgra.data(), 1, 1, PixelLayout::RGBA8, {}, error);
    CHECK(pkg && !pkg->generateMipmaps);

    CHECK(!packTextureFromPixels(nullptr, 2, 1, PixelLayout::RGBA8, {}, error));
    CHECK(!packTextureFromPixels(bgra.data(), 0, 1, PixelLayout::RGBA8, {}, error));
}

void testPackFromMemory() {
    std::string error;

    // RGB source: alpha is filled in. PNG is lossless, so the pixels come back exactly.
    const std::vector<uint8_t> rgb { 255, 0, 0,   0, 255, 0,   0, 0, 255,   9, 8, 7 };
    auto pkg = packTextureFromMemory(encodePng(2, 2, 3, rgb), { .srgb = false }, error);
    REQUIRE(pkg);
    CHECK_EQ(pkg->type, TextureType::Texture2D);
    CHECK_EQ(pkg->format, TextureFormat::RGBA8);
    CHECK(pkg->width == 2 && pkg->height == 2 && pkg->levels == 1);
    CHECK_EQ(pkg->data, (std::vector<uint8_t> { 255, 0, 0, 255,   0, 255, 0, 255,   0, 0, 255, 255,   9, 8, 7, 255 }));

    // HDR source: RGBA32F whatever srgb says.
    const std::vector<float> hdr { 0.5f, 2.0f, 8.0f,   1.0f, 1.0f, 1.0f };
    pkg = packTextureFromMemory(encodeHdr(2, 1, hdr), { .srgb = true }, error);
    REQUIRE(pkg);
    CHECK_EQ(pkg->format, TextureFormat::RGBA32F);
    REQUIRE_EQ(pkg->data.size(), 2 * 16);
    float texel[4];
    std::memcpy(texel, pkg->data.data(), sizeof(texel));
    CHECK(std::abs(texel[0] - 0.5f) < 0.01f && std::abs(texel[1] - 2.0f) < 0.02f && std::abs(texel[2] - 8.0f) < 0.1f);
    CHECK_EQ(texel[3], 1.0f);

    const std::vector<uint8_t> garbage { 1, 2, 3, 4, 5, 6, 7, 8 };
    CHECK(!packTextureFromMemory(garbage, {}, error));
    CHECK(contains(error, "cannot decode image"));

    CHECK(!packTextureFromMemory(std::span<const uint8_t>(), {}, error));
}

void testPackFile() {
    auto dir = scratchDir();
    std::string error;

    auto missing = dir / "missing.png";
    CHECK(!packTexture(missing, {}, error));
    CHECK(contains(error, "missing.png") && contains(error, "cannot read"));

    auto path = dir / "red.png";
    writeFile(path, encodePng(4, 2, 4, solid(4, 2, { 255, 0, 0, 255 })));
    auto pkg = packTexture(path, {}, error);
    REQUIRE(pkg);
    CHECK(pkg->width == 4 && pkg->height == 2);
    CHECK_EQ(pkg->format, TextureFormat::RGBA8_SRGB);
    CHECK(pkg->generateMipmaps);
    CHECK_EQ(pkg->data, solid(4, 2, { 255, 0, 0, 255 }));

    auto broken = dir / "broken.png";
    writeFile(broken, { 1, 2, 3 });
    CHECK(!packTexture(broken, {}, error));
    CHECK(contains(error, "broken.png") && contains(error, "cannot decode"));
}

void testCubemapFacePaths() {
    auto faces = cubemapFacePaths("sky/yokohama.jpg");
    CHECK_EQ(faces[0], fs::path("sky/yokohama_px.jpg"));
    CHECK_EQ(faces[1], fs::path("sky/yokohama_nx.jpg"));
    CHECK_EQ(faces[2], fs::path("sky/yokohama_py.jpg"));
    CHECK_EQ(faces[3], fs::path("sky/yokohama_ny.jpg"));
    CHECK_EQ(faces[4], fs::path("sky/yokohama_pz.jpg"));
    CHECK_EQ(faces[5], fs::path("sky/yokohama_nz.jpg"));
}

void testPackCubemap() {
    auto dir = scratchDir();
    auto faces = cubemapFacePaths(dir / "sky.png");
    for (size_t i = 0; i < faces.size(); i++) {
        writeFile(faces[i], encodePng(2, 2, 4, solid(2, 2, { uint8_t(i), uint8_t(10 * i), 0, 255 })));
    }

    std::string error;
    auto pkg = packCubemap(faces, { .srgb = true, .mipmaps = MipmapMode::Runtime }, error);
    REQUIRE(pkg);
    CHECK_EQ(pkg->type, TextureType::Cubemap);
    CHECK_EQ(pkg->format, TextureFormat::RGBA8_SRGB);
    CHECK(pkg->width == 2 && pkg->height == 2 && pkg->levels == 1);
    CHECK(pkg->generateMipmaps);
    for (uint32_t face = 0; face < 6; face++) {
        auto expected = solid(2, 2, { uint8_t(face), uint8_t(10 * face), 0, 255 });
        auto image = pkg->image(0, face);
        CHECK(std::equal(image.begin(), image.end(), expected.begin(), expected.end()));
    }

    // A face of another size is rejected and named.
    writeFile(faces[3], encodePng(4, 4, 4, solid(4, 4, { 0, 0, 0, 255 })));
    CHECK(!packCubemap(faces, {}, error));
    CHECK(contains(error, "sky_ny.png") && contains(error, "4x4") && contains(error, "sky_px.png"));

    // So is a face that isn't square.
    writeFile(faces[0], encodePng(4, 2, 4, solid(4, 2, { 0, 0, 0, 255 })));
    CHECK(!packCubemap(faces, {}, error));
    CHECK(contains(error, "sky_px.png") && contains(error, "square"));

    fs::remove(faces[5]);
    writeFile(faces[0], encodePng(2, 2, 4, solid(2, 2, { 0, 0, 0, 255 })));
    writeFile(faces[3], encodePng(2, 2, 4, solid(2, 2, { 0, 0, 0, 255 })));
    CHECK(!packCubemap(faces, {}, error));
    CHECK(contains(error, "sky_nz.png") && contains(error, "cannot read"));

    fs::remove_all(dir);
}

void testPackCubemapFromEquirect() {
    auto dir = scratchDir();
    auto path = dir / "sky.hdr";

    // Upper half red, lower half blue.
    std::vector<float> rgb;
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 16; x++) {
            rgb.insert(rgb.end(), { y < 4 ? 1.0f : 0.0f, 0.0f, y < 4 ? 0.0f : 1.0f });
        }
    }
    writeFile(path, encodeHdr(16, 8, rgb));

    std::string error;
    auto pkg = packCubemapFromEquirect(path, 0, { .mipmaps = MipmapMode::None }, error);
    REQUIRE(pkg);
    CHECK_EQ(pkg->type, TextureType::Cubemap);
    CHECK_EQ(pkg->format, TextureFormat::RGBA32F);
    CHECK(pkg->width == 4 && pkg->height == 4 && pkg->levels == 1);
    CHECK(!pkg->generateMipmaps);

    auto texel = [&](uint32_t face, uint32_t x, uint32_t y) {
        std::array<float, 4> out;
        std::memcpy(out.data(), pkg->image(0, face).data() + (y * 4 + x) * 16, sizeof(out));
        return out;
    };
    // +y is all sky, -y all ground; the side faces have the horizon across the middle (row 0 is up).
    CHECK(texel(2, 1, 1)[0] == 1.0f && texel(2, 1, 1)[2] == 0.0f);
    CHECK(texel(3, 2, 2)[0] == 0.0f && texel(3, 2, 2)[2] == 1.0f);
    for (uint32_t face : { 0u, 1u, 4u, 5u }) {
        CHECK(texel(face, 1, 0)[0] == 1.0f && texel(face, 1, 3)[2] == 1.0f);
        CHECK_EQ(texel(face, 1, 0)[3], 1.0f);
    }

    pkg = packCubemapFromEquirect(path, 8, {}, error);
    CHECK(pkg && pkg->width == 8 && pkg->generateMipmaps);

    CHECK(!packCubemapFromEquirect(dir / "missing.hdr", 0, {}, error));
    CHECK(contains(error, "missing.hdr") && contains(error, "cannot read"));

    fs::remove_all(dir);
}

}

std::vector<uint8_t> red(std::initializer_list<uint8_t> values) {
    std::vector<uint8_t> pixels;
    for (uint8_t v : values) pixels.insert(pixels.end(), { v, 0, 0, 255 });
    return pixels;
}

void testOfflineMipmaps() {
    std::string error;
    const TextureImportOptions linear { .srgb = false, .mipmaps = MipmapMode::Offline };

    auto pixels = red({ 0, 40, 80, 120,   4, 8, 12, 16 });
    auto pkg = packTextureFromPixels(pixels.data(), 4, 2, PixelLayout::RGBA8, linear, error);
    REQUIRE(pkg);
    REQUIRE(pkg->levels == 3 && pkg->allocatedLevels() == 3);
    CHECK(!pkg->generateMipmaps);
    auto level0 = pkg->image(0, 0);
    CHECK(std::equal(level0.begin(), level0.end(), pixels.begin(), pixels.end()));
    auto level1 = pkg->image(1, 0);
    CHECK(std::ranges::equal(level1, red({ 13, 57 })));
    // Filtered from level 1 before it was rounded to 8 bits.
    CHECK(std::ranges::equal(pkg->image(2, 0), red({ 35 })));

    // sRGB color is averaged in linear space, alpha as is.
    const std::vector<uint8_t> blackWhite { 0, 0, 0, 0,   255, 255, 255, 255 };
    pkg = packTextureFromPixels(blackWhite.data(), 2, 1, PixelLayout::RGBA8, { .mipmaps = MipmapMode::Offline }, error);
    REQUIRE(pkg && pkg->levels == 2);
    CHECK_EQ(pkg->format, TextureFormat::RGBA8_SRGB);
    CHECK(std::ranges::equal(pkg->image(1, 0), std::vector<uint8_t> { 188, 188, 188, 128 }));

    // An odd size blends its last column in.
    pixels = red({ 0, 90, 180 });
    pkg = packTextureFromPixels(pixels.data(), 3, 1, PixelLayout::RGBA8, linear, error);
    REQUIRE(pkg && pkg->levels == 2);
    CHECK(std::ranges::equal(pkg->image(1, 0), red({ 90 })));

    pixels = solid(5, 3, { 1, 2, 3, 4 });
    pkg = packTextureFromPixels(pixels.data(), 5, 3, PixelLayout::RGBA8, linear, error);
    REQUIRE(pkg && pkg->levels == 3);
    CHECK(pkg->levelWidth(1) == 2 && pkg->levelHeight(1) == 1);
    CHECK(std::ranges::equal(pkg->image(1, 0), solid(2, 1, { 1, 2, 3, 4 })));
    CHECK(std::ranges::equal(pkg->image(2, 0), solid(1, 1, { 1, 2, 3, 4 })));

    // A 1x1 texture has no chain.
    pkg = packTextureFromPixels(pixels.data(), 1, 1, PixelLayout::RGBA8, linear, error);
    CHECK(pkg && pkg->levels == 1);

    // Every cubemap face gets its own chain.
    auto dir = scratchDir();
    auto faces = cubemapFacePaths(dir / "sky.png");
    for (size_t i = 0; i < faces.size(); i++) {
        writeFile(faces[i], encodePng(4, 4, 4, solid(4, 4, { uint8_t(i), uint8_t(10 * i), 0, 255 })));
    }
    pkg = packCubemap(faces, { .mipmaps = MipmapMode::Offline }, error);
    REQUIRE(pkg && pkg->levels == 3);
    CHECK(!pkg->generateMipmaps);
    for (uint32_t face = 0; face < 6; face++) {
        CHECK(std::ranges::equal(pkg->image(2, face), solid(1, 1, { uint8_t(face), uint8_t(10 * face), 0, 255 })));
    }
    fs::remove_all(dir);

    // HDR values are kept as floats.
    const std::vector<float> hdr { 1.0f, 2.0f, 4.0f, 1.0f,   3.0f, 6.0f, 12.0f, 1.0f };
    TexturePackage floats;
    floats.format = TextureFormat::RGBA32F;
    floats.width = 2;
    floats.height = 1;
    floats.data.resize(sizeof(float) * hdr.size());
    std::memcpy(floats.data.data(), hdr.data(), floats.data.size());
    CHECK(generateMipLevels(floats, error));
    float texel[4];
    std::memcpy(texel, floats.image(1, 0).data(), sizeof(texel));
    CHECK(texel[0] == 2.0f && texel[1] == 4.0f && texel[2] == 8.0f && texel[3] == 1.0f);

    // A package already set up for runtime generation is left alone.
    pkg = packTextureFromPixels(pixels.data(), 2, 1, PixelLayout::RGBA8, {}, error);
    REQUIRE(pkg);
    CHECK(pkg->generateMipmaps);
    CHECK(!generateMipLevels(*pkg, error));
}

void runTextureTests() {
    ailo::test::run("texture/testMipLevelCount", testMipLevelCount);
    ailo::test::run("texture/testLayout", testLayout);
    ailo::test::run("texture/testRoundTrip", testRoundTrip);
    ailo::test::run("texture/testHalfFloat", testHalfFloat);
    ailo::test::run("texture/testCompressedLayout", testCompressedLayout);
    ailo::test::run("texture/testDeserializeRejectsBadPackages", testDeserializeRejectsBadPackages);
    ailo::test::run("texture/testDeserializeSkipsUnknownChunks", testDeserializeSkipsUnknownChunks);
    ailo::test::run("texture/testPackFromPixels", testPackFromPixels);
    ailo::test::run("texture/testPackFromMemory", testPackFromMemory);
    ailo::test::run("texture/testPackFile", testPackFile);
    ailo::test::run("texture/testCubemapFacePaths", testCubemapFacePaths);
    ailo::test::run("texture/testPackCubemap", testPackCubemap);
    ailo::test::run("texture/testPackCubemapFromEquirect", testPackCubemapFromEquirect);
    ailo::test::run("texture/testOfflineMipmaps", testOfflineMipmaps);
}
