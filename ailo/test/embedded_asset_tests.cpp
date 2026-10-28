#include "assets/AssetServer.h"
#include "assets/MemoryAssetReader.h"
#include "Testing.h"

#include "embedded/test_assets.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace ailo;

namespace {

struct Text {
    explicit Text(std::string value) : value(std::move(value)) {}
    std::string value;
};

constexpr std::string_view kTextExtensions[] = { ".txt" };

class TextLoader : public AssetLoader<Text> {
public:
    constexpr std::span<const std::string_view> extensions() const override { return kTextExtensions; }
    void load(const std::string&, std::span<const std::uint8_t> bytes, LoadContext<Text>& context) override {
        context.construct(std::string(bytes.begin(), bytes.end()));
    }
};

std::string_view text(std::span<const std::uint8_t> bytes) {
    return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
}

class NoReader : public AssetReader {
public:
    std::vector<std::uint8_t> read(const std::string& key) override {
        throw std::runtime_error("unexpected read of '" + key + "'");
    }
};

MemoryAssetReader testReader() {
    return {
        { "hello.txt", embedded::test_assets::kHello },
        { "nested/world.txt", embedded::test_assets::kNestedWorld },
        { "materials/unlit.mat", embedded::test_assets::kMaterialsUnlit },
    };
}

void testPackageIsAligned() {
    using namespace embedded::test_assets;
    for (const auto data : { kHello, kNestedWorld, kMaterialsUnlit }) {
        CHECK_EQ(reinterpret_cast<std::uintptr_t>(data.data()) % 16, 0);
        CHECK(data.data() >= kPackage.data() && data.data() + data.size() <= kPackage.data() + kPackage.size());
    }
    CHECK_EQ(text(kHello), "hello");
}

void testCompiledAssetIsEmbedded() {
    const auto reader = testReader();
    const auto material = reader.find("materials/unlit.mat");
    CHECK(material && text(*material).starts_with("AMAT"));
    // Only the listed keys are found.
    CHECK(!reader.find("materials/unlit.matpack"));
    CHECK(!reader.find("unlisted.txt"));
}

void testReadCopiesBytes() {
    auto reader = testReader();
    const auto bytes = reader.read("nested/world.txt");
    CHECK_EQ(std::string(bytes.begin(), bytes.end()), "world");

    bool threw = false;
    try {
        reader.read("missing.txt");
    }
    catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

void testFindOnHandWrittenList() {
    static constexpr std::uint8_t kA[] = { 'a' };
    static constexpr std::uint8_t kB[] = { 'b' };
    // Unsorted: the reader sorts its copy.
    MemoryAssetReader reader { { "b/c", kB }, { "a", kA } };
    CHECK_EQ(reader.find("a")->data(), kA);
    CHECK_EQ(reader.find("b/c")->data(), kB);
    CHECK(!reader.find(""));
    CHECK(!reader.find("b"));
    CHECK(!reader.find("c"));

    // Keys are copied, so they may be temporaries.
    std::string key = "temp";
    MemoryAssetReader copied { { key, kA } };
    key = "gone";
    CHECK(copied.find("temp"));

    MemoryAssetReader empty { std::span<const MemoryAssetReader::Asset>{} };
    CHECK(!empty.find("a"));

    bool threw = false;
    try {
        MemoryAssetReader duplicate { { "a", kA }, { "a", kB } };
    }
    catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

void testServerLoadsFromEmbeddedSource() {
    AssetStorage<Text> texts;
    {
        AssetServer server { std::make_unique<NoReader>() };
        server.registerStorage(&texts);
        server.registerLoader<Text>(std::make_unique<TextLoader>());
        server.registerReader("embedded", std::make_unique<MemoryAssetReader>(testReader()));

        auto hello = server.load<Text>("embedded:/hello.txt");
        CHECK(hello && hello->value == "hello");
        CHECK(texts.has("embedded:/hello.txt"));
        auto world = server.load<Text>("embedded:/nested/world.txt");
        CHECK(world && world->value == "world");
        // A failed read logs and returns null.
        CHECK(!server.load<Text>("embedded:/missing.txt"));
    }
}

}

void runEmbeddedAssetTests() {
    ailo::test::run("embedded_asset/testPackageIsAligned", testPackageIsAligned);
    ailo::test::run("embedded_asset/testCompiledAssetIsEmbedded", testCompiledAssetIsEmbedded);
    ailo::test::run("embedded_asset/testReadCopiesBytes", testReadCopiesBytes);
    ailo::test::run("embedded_asset/testFindOnHandWrittenList", testFindOnHandWrittenList);
    ailo::test::run("embedded_asset/testServerLoadsFromEmbeddedSource", testServerLoadsFromEmbeddedSource);
}
