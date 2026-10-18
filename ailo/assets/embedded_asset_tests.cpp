#include "AssetServer.h"
#include "MemoryAssetReader.h"

#include "embedded/test_assets.h"

#include <cassert>
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
        { "materials/unlit.matpack", embedded::test_assets::kMaterialsUnlit },
    };
}

void testPackageIsAligned() {
    using namespace embedded::test_assets;
    for (const auto data : { kHello, kNestedWorld, kMaterialsUnlit }) {
        assert(reinterpret_cast<std::uintptr_t>(data.data()) % 16 == 0);
        assert(data.data() >= kPackage.data() && data.data() + data.size() <= kPackage.data() + kPackage.size());
    }
    assert(text(kHello) == "hello");
}

void testCompiledAssetIsEmbedded() {
    const auto reader = testReader();
    const auto material = reader.find("materials/unlit.matpack");
    assert(material && text(*material).starts_with("AMAT"));
    // Only the listed keys are found.
    assert(!reader.find("materials/unlit.mat"));
    assert(!reader.find("unlisted.txt"));
}

void testReadCopiesBytes() {
    auto reader = testReader();
    const auto bytes = reader.read("nested/world.txt");
    assert(std::string(bytes.begin(), bytes.end()) == "world");

    bool threw = false;
    try {
        reader.read("missing.txt");
    }
    catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

void testFindOnHandWrittenList() {
    static constexpr std::uint8_t kA[] = { 'a' };
    static constexpr std::uint8_t kB[] = { 'b' };
    // Unsorted: the reader sorts its copy.
    MemoryAssetReader reader { { "b/c", kB }, { "a", kA } };
    assert(reader.find("a")->data() == kA);
    assert(reader.find("b/c")->data() == kB);
    assert(!reader.find(""));
    assert(!reader.find("b"));
    assert(!reader.find("c"));

    // Keys are copied, so they may be temporaries.
    std::string key = "temp";
    MemoryAssetReader copied { { key, kA } };
    key = "gone";
    assert(copied.find("temp"));

    MemoryAssetReader empty { std::span<const MemoryAssetReader::Asset>{} };
    assert(!empty.find("a"));

    bool threw = false;
    try {
        MemoryAssetReader duplicate { { "a", kA }, { "a", kB } };
    }
    catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);
}

void testServerLoadsFromEmbeddedSource() {
    AssetStorage<Text> texts;
    {
        AssetServer server { std::make_unique<NoReader>() };
        server.registerStorage(&texts);
        server.registerLoader<Text>(std::make_unique<TextLoader>());
        server.registerReader("embedded", std::make_unique<MemoryAssetReader>(testReader()));

        auto hello = server.load<Text>("embedded:/hello.txt");
        assert(hello && hello->value == "hello");
        assert(texts.has("embedded:/hello.txt"));
        auto world = server.load<Text>("embedded:/nested/world.txt");
        assert(world && world->value == "world");
        // A failed read logs and returns null.
        assert(!server.load<Text>("embedded:/missing.txt"));
    }
}

}

int main() {
    testPackageIsAligned();
    testCompiledAssetIsEmbedded();
    testReadCopiesBytes();
    testFindOnHandWrittenList();
    testServerLoadsFromEmbeddedSource();

    std::cout << "All embedded asset tests passed" << std::endl;
    return 0;
}
