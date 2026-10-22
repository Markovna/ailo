#include "assets/AssetServer.h"
#include "Testing.h"

#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

using namespace ailo;

namespace {

std::vector<std::string> g_log;

struct Texture {
    explicit Texture(std::string name) : name(std::move(name)) {}
    ~Texture() { g_log.push_back("~Texture " + name); }
    std::string name;
};

struct Material {
    Material(std::string name, AssetPtr<Texture> texture) : name(std::move(name)), texture(std::move(texture)) {}
    ~Material() { g_log.push_back("~Material " + name); }
    std::string name;
    AssetPtr<Texture> texture;
};

// Holds another asset of its own type, so releasing it re-enters its own storage.
struct Node {
    Node(std::string name, AssetPtr<Node> next) : name(std::move(name)), next(std::move(next)) {}
    ~Node() { g_log.push_back("~Node " + name); }
    std::string name;
    AssetPtr<Node> next;
};

// A model owning a sub-asset it builds in place.
struct Model {
    Model(AssetPtr<Material> material, AssetPtr<Texture> embedded) : material(std::move(material)), embedded(std::move(embedded)) {}
    AssetPtr<Material> material;
    AssetPtr<Texture> embedded;
};

struct Throwing {
    explicit Throwing(bool shouldThrow) {
        if (shouldThrow) throw std::runtime_error("ctor");
    }
};

// Returns each key's own characters as its bytes and logs the keys it reads; keys in `missing` fail.
class TestReader : public AssetReader {
public:
    std::vector<std::uint8_t> read(const std::string& key) override {
        reads.push_back(key);
        if (missing.contains(key)) {
            throw std::runtime_error("cannot read '" + key + "'");
        }
        return { key.begin(), key.end() };
    }

    std::vector<std::string> reads;
    std::unordered_set<std::string> missing;
};

// Names each texture after the bytes it was given.
class BytesTextureLoader : public AssetLoader<Texture> {
public:
    explicit BytesTextureLoader(int* calls) : m_calls(calls) {}
    constexpr std::span<const std::string_view> extensions() const override { return kBytesExtensions; }
    void load(const std::string&, std::span<const std::uint8_t> bytes, LoadContext<Texture>& context) override {
        ++*m_calls;
        context.construct(std::string(bytes.begin(), bytes.end()));
    }
private:
    static constexpr std::string_view kBytesExtensions[] = { ".tex" };
    int* m_calls;
};

constexpr std::string_view kTexExtensions[] = { ".tex" };
constexpr std::string_view kMatExtensions[] = { ".mat" };
constexpr std::string_view kModelExtensions[] = { ".model" };

template<class T>
class TexLoader : public AssetLoader<T> {
public:
    constexpr std::span<const std::string_view> extensions() const override { return kTexExtensions; }
};

class TextureLoader : public TexLoader<Texture> {
public:
    explicit TextureLoader(int* calls) : m_calls(calls) {}
    void load(const std::string& key, std::span<const std::uint8_t>, LoadContext<Texture>& context) override {
        ++*m_calls;
        context.construct(key);
    }
private:
    int* m_calls;
};

// "brick.mat" -> texture "brick.mat.tex"
class MaterialLoader : public AssetLoader<Material> {
public:
    constexpr std::span<const std::string_view> extensions() const override { return kMatExtensions; }
    void load(const std::string& key, std::span<const std::uint8_t>, LoadContext<Material>& context) override {
        auto texture = context.load<Texture>(key + ".tex");
        context.construct(key, std::move(texture));
    }
};

// "house.model" -> material "house.model.mat" (a dependency, loaded through the context) and an anonymous
// texture "embedded" (a sub-asset, emplaced directly into its storage)
class ModelLoader : public AssetLoader<Model> {
public:
    explicit ModelLoader(AssetStorage<Texture>* textures) : m_textures(textures) {}
    constexpr std::span<const std::string_view> extensions() const override { return kModelExtensions; }
    void load(const std::string& key, std::span<const std::uint8_t>, LoadContext<Model>& context) override {
        auto material = context.load<Material>(key + ".mat");
        auto embedded = m_textures->emplace(noname_t{}, "embedded");
        context.construct(std::move(material), std::move(embedded));
    }
private:
    AssetStorage<Texture>* m_textures;
};

class RenamingTextureLoader : public TexLoader<Texture> {
public:
    void load(const std::string& key, std::span<const std::uint8_t>, LoadContext<Texture>& context) override {
        Texture& texture = context.construct("unnamed");
        texture.name = key;
    }
};

template<class T>
class NoConstructLoader : public TexLoader<T> {
public:
    void load(const std::string&, std::span<const std::uint8_t>, LoadContext<T>&) override {}
};

class CountingNoConstructLoader : public TexLoader<Texture> {
public:
    explicit CountingNoConstructLoader(int* calls) : m_calls(calls) {}
    void load(const std::string&, std::span<const std::uint8_t>, LoadContext<Texture>&) override { ++*m_calls; }
private:
    int* m_calls;
};

template<class T>
class ThrowAfterConstructLoader : public TexLoader<T> {
public:
    void load(const std::string& key, std::span<const std::uint8_t>, LoadContext<T>& context) override {
        context.construct(key);
        throw std::runtime_error("loader failed");
    }
};

template<class T>
class DoubleConstructLoader : public TexLoader<T> {
public:
    void load(const std::string& key, std::span<const std::uint8_t>, LoadContext<T>& context) override {
        context.construct(key);
        context.construct(key);
    }
};

// Names each texture after the loader that made it.
class TaggingTextureLoader : public AssetLoader<Texture> {
public:
    TaggingTextureLoader(std::string tag, std::vector<std::string_view> extensions)
        : m_tag(std::move(tag)), m_extensions(std::move(extensions)) {}
    std::span<const std::string_view> extensions() const override { return m_extensions; }
    void load(const std::string& key, std::span<const std::uint8_t>, LoadContext<Texture>& context) override {
        context.construct(m_tag + ":" + key);
    }
private:
    std::string m_tag;
    std::vector<std::string_view> m_extensions;
};

template<class E, class F>
bool throws(F&& f) {
    try {
        f();
    } catch (const E&) {
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// AssetIndex
// ---------------------------------------------------------------------------

void testAssetIndexDefaultIsInvalid() {
    AssetIndex<Texture> index;
    CHECK(!index.isValid());
    CHECK_EQ(index, AssetIndex<Texture>{});
}

// ---------------------------------------------------------------------------
// AssetStorage / AssetPtr
// ---------------------------------------------------------------------------

void testEmplaceAndGet() {
    g_log.clear();
    AssetStorage<Texture> storage;
    {
        auto ptr = storage.emplace("a", "albedo");
        REQUIRE(ptr);
        CHECK(ptr.index().isValid());
        CHECK_EQ(ptr->name, "albedo");
        CHECK_EQ((*ptr).name, "albedo");
        CHECK_EQ(ptr.get(), storage.get(ptr.index()));
        CHECK_EQ(ptr.useCount(), 1);
        CHECK(storage.has("a"));
        CHECK_EQ(storage.size(), 1);

        auto found = storage.get("a");
        CHECK(found && *found == ptr);
        CHECK_EQ(ptr.useCount(), 2);
    }
    CHECK(storage.empty());
    CHECK(!storage.has("a"));
    CHECK(!storage.get("a"));
    CHECK_EQ(g_log, std::vector<std::string>{ "~Texture albedo" });
}

void testEmplaceDuplicateKeyThrows() {
    AssetStorage<Texture> storage;
    auto ptr = storage.emplace("a", "first");
    CHECK((throws<std::invalid_argument>([&] { storage.emplace("a", "second"); })));
    CHECK_EQ(storage.size(), 1);
    CHECK_EQ(ptr->name, "first");
}

void testEmplaceThrowingConstructorLeavesStorageClean() {
    AssetStorage<Throwing> storage;
    CHECK((throws<std::runtime_error>([&] { storage.emplace("a", true); })));
    CHECK(storage.empty());
    CHECK(!storage.has("a"));

    auto ptr = storage.emplace("a", false);
    CHECK(ptr && storage.size() == 1);
}

void testGetMissingKey() {
    AssetStorage<Texture> storage;
    CHECK(!storage.has("missing"));
    CHECK(!storage.get("missing"));
}

void testCopyMoveAndReset() {
    AssetStorage<Texture> storage;
    auto a = storage.emplace("a", "a");

    AssetPtr<Texture> copy = a;
    CHECK(copy == a && a.useCount() == 2);

    AssetPtr<Texture> moved = std::move(copy);
    CHECK(!copy && copy.useCount() == 0 && !copy.index().isValid());
    CHECK(moved == a && a.useCount() == 2);

    AssetPtr<Texture> assigned;
    assigned = a;
    CHECK_EQ(a.useCount(), 3);

    assigned = std::move(moved);
    CHECK(!moved);
    CHECK_EQ(a.useCount(), 2);

    assigned.reset();
    CHECK(!assigned && assigned.get() == nullptr);
    CHECK_EQ(a.useCount(), 1);

    assigned.reset();   // idempotent
    CHECK_EQ(a.useCount(), 1);
}

void testSelfAssignment() {
    AssetStorage<Texture> storage;
    auto a = storage.emplace("a", "a");
    auto& alias = a;
    a = alias;
    CHECK(a && a.useCount() == 1);
    a = std::move(alias);
    CHECK(a && a.useCount() == 1);
}

void testSwap() {
    AssetStorage<Texture> storage;
    auto a = storage.emplace("a", "a");
    auto b = storage.emplace("b", "b");
    a.swap(b);
    CHECK(a->name == "b" && b->name == "a");
    CHECK(a.useCount() == 1 && b.useCount() == 1);
}

void testAssigningOverLastReferenceDestroysOldAsset() {
    g_log.clear();
    AssetStorage<Texture> storage;
    auto ptr = storage.emplace("a", "a");
    ptr = storage.emplace("b", "b");
    CHECK_EQ(g_log, std::vector<std::string>{ "~Texture a" });
    CHECK(!storage.has("a") && storage.has("b"));
}

void testNullPtr() {
    AssetPtr<Texture> ptr;
    CHECK(!ptr);
    CHECK_EQ(ptr.get(), nullptr);
    CHECK_EQ(ptr.useCount(), 0);
    CHECK_EQ(ptr, AssetPtr<Texture>{});
}

void testStaleIndexResolvesToNothing() {
    AssetStorage<Texture> storage;
    AssetIndex<Texture> stale;
    {
        auto ptr = storage.emplace("a", "a");
        stale = ptr.index();
        CHECK_NE(storage.get(stale), nullptr);
    }
    CHECK_EQ(storage.get(stale), nullptr);

    // Churn enough slots that the freed index gets recycled with a newer version
    std::vector<AssetPtr<Texture>> keep;
    for (int i = 0; i < 200; ++i) {
        storage.emplace("tmp" + std::to_string(i), "tmp");
    }
    for (int i = 0; i < 200; ++i) {
        keep.push_back(storage.emplace("k" + std::to_string(i), "k"));
    }
    CHECK_EQ(storage.get(stale), nullptr);
    for (const auto& p : keep) {
        CHECK(!(p.index() == stale));
    }
    const AssetStorage<Texture>& constStorage = storage;
    CHECK_EQ(constStorage.get(stale), nullptr);
}

void testKeyCanBeReusedAfterRelease() {
    AssetStorage<Texture> storage;
    AssetIndex<Texture> first;
    {
        auto ptr = storage.emplace("a", "first");
        first = ptr.index();
    }
    auto ptr = storage.emplace("a", "second");
    CHECK_EQ(ptr->name, "second");
    CHECK(!(ptr.index() == first));
    CHECK_EQ(storage.get(first), nullptr);
}

void testDependentReleasesDependency() {
    g_log.clear();
    AssetStorage<Texture> textures;
    AssetStorage<Material> materials;
    {
        auto material = materials.emplace("brick", "brick", textures.emplace("t", "albedo"));
        CHECK(textures.has("t"));
        CHECK_EQ(material->texture.useCount(), 1);
    }
    CHECK(materials.empty() && textures.empty());
    CHECK((g_log == std::vector<std::string>{ "~Material brick", "~Texture albedo" }));
}

void testNestedSameTypeRelease() {
    g_log.clear();
    AssetStorage<Node> storage;
    {
        auto tail = storage.emplace("c", "c", AssetPtr<Node>{});
        auto middle = storage.emplace("b", "b", std::move(tail));
        auto head = storage.emplace("a", "a", std::move(middle));
        CHECK_EQ(storage.size(), 3);
    }
    CHECK(storage.empty());
    CHECK((g_log == std::vector<std::string>{ "~Node a", "~Node b", "~Node c" }));
}

void testLiveAssetCount() {
    AssetStorage<Texture> textures;
    AssetStorage<Material> materials;
    AssetServer server { std::make_unique<TestReader>() };
    server.registerStorage(&textures);
    server.registerStorage(&materials);

    CHECK(textures.empty());
    CHECK(materials.empty());

    auto texture = textures.emplace("t", "t");
    auto copy = texture;
    auto material = materials.emplace("m", "m", AssetPtr<Texture>{});
    CHECK_EQ(textures.size(), 1u);
    CHECK_EQ(materials.size(), 1u);

    texture = {};
    CHECK_EQ(textures.size(), 1u);
    copy = {};
    material = {};
    CHECK(textures.empty());
    CHECK(materials.empty());
}

// ---------------------------------------------------------------------------
// AssetServer
// ---------------------------------------------------------------------------

void registerTextureLoader(AssetServer& server, int* calls) {
    server.registerLoader<Texture>(std::make_unique<TextureLoader>(calls));
}

void testServerLoad() {
    g_log.clear();
    int calls = 0;
    AssetStorage<Texture> textures;
    AssetServer server { std::make_unique<TestReader>() };
    server.registerStorage(&textures);
    registerTextureLoader(server, &calls);
    {
        auto a = server.load<Texture>("a.tex");
        CHECK(a && a->name == "a.tex");
        CHECK_EQ(a.useCount(), 1);
        CHECK(textures.has("a.tex"));
        CHECK_EQ(calls, 1);

        auto again = server.load<Texture>("a.tex");
        CHECK_EQ(again, a);
        CHECK_EQ(a.useCount(), 2);
        CHECK_EQ(calls, 1);

        auto b = server.load<Texture>("b.tex");
        CHECK(!(b == a));
        CHECK_EQ(calls, 2);
    }
    CHECK((g_log == std::vector<std::string>{ "~Texture b.tex", "~Texture a.tex" }));
    CHECK(textures.empty());

    // Assets are not cached past their last reference
    auto a = server.load<Texture>("a.tex");
    CHECK_EQ(calls, 3);
}

void testServerLoadsDependencies() {
    g_log.clear();
    int calls = 0;
    AssetStorage<Texture> textures;
    AssetStorage<Material> materials;
    AssetServer server { std::make_unique<TestReader>() };
    server.registerStorage(&textures);
    server.registerStorage(&materials);
    registerTextureLoader(server, &calls);
    server.registerLoader<Material>(std::make_unique<MaterialLoader>());
    {
        auto texture = server.load<Texture>("brick.mat.tex");
        auto material = server.load<Material>("brick.mat");
        CHECK_EQ(material->texture, texture);
        CHECK_EQ(texture.useCount(), 2);
        CHECK_EQ(calls, 1);
    }
    CHECK((g_log == std::vector<std::string>{ "~Material brick.mat", "~Texture brick.mat.tex" }));
}

void testConstructReturnsAsset() {
    AssetStorage<Texture> textures;
    AssetServer server { std::make_unique<TestReader>() };
    server.registerStorage(&textures);
    server.registerLoader<Texture>(std::make_unique<RenamingTextureLoader>());
    auto texture = server.load<Texture>("renamed.tex");
    CHECK_EQ(texture->name, "renamed.tex");
}

void testLoaderWithDependencyAndSubAsset() {
    int calls = 0;
    AssetStorage<Texture> textures;
    AssetStorage<Material> materials;
    AssetStorage<Model> models;
    AssetServer server { std::make_unique<TestReader>() };
    server.registerStorage(&textures);
    server.registerStorage(&materials);
    server.registerStorage(&models);
    registerTextureLoader(server, &calls);
    server.registerLoader<Material>(std::make_unique<MaterialLoader>());
    server.registerLoader<Model>(std::make_unique<ModelLoader>(&textures));

    AssetPtr<Texture> keptSubAsset;
    {
        auto model = server.load<Model>("house.model");
        CHECK_EQ(model->material->texture->name, "house.model.mat.tex");
        CHECK_EQ(model->embedded->name, "embedded");
        CHECK_EQ(textures.size(), 2);
        keptSubAsset = model->embedded;
    }
    CHECK(models.empty() && materials.empty());
    CHECK_EQ(textures.size(), 1);

    // Re-importing while the old anonymous sub-asset is still alive creates a new one
    auto model = server.load<Model>("house.model");
    CHECK(!(model->embedded == keptSubAsset));
    CHECK_EQ(textures.size(), 3);
}

void testServerReturnsAssetsAlreadyInStorage() {
    int calls = 0;
    AssetStorage<Texture> textures;
    AssetServer server { std::make_unique<TestReader>() };
    server.registerStorage(&textures);
    registerTextureLoader(server, &calls);

    auto preloaded = textures.emplace("pre.tex", "preloaded");
    auto found = server.load<Texture>("pre.tex");
    CHECK_EQ(found, preloaded);
    CHECK_EQ(calls, 0);
}

void testServerWithoutStorageThrows() {
    int calls = 0;
    AssetServer server { std::make_unique<TestReader>() };
    registerTextureLoader(server, &calls);
    CHECK((throws<std::runtime_error>([&] { server.load<Texture>("a.tex"); })));
    CHECK_EQ(calls, 0);
}

void testUnregisteredDependencyStorageFailsLoad() {
    g_log.clear();
    int calls = 0;
    AssetStorage<Material> materials;
    AssetServer server { std::make_unique<TestReader>() };
    server.registerStorage(&materials);
    registerTextureLoader(server, &calls);
    server.registerLoader<Material>(std::make_unique<MaterialLoader>());

    CHECK(!server.load<Material>("brick.mat"));
    CHECK_EQ(calls, 0);
    CHECK(materials.empty());
    CHECK(g_log.empty());
}

void testUnregisterStorage() {
    int calls = 0;
    AssetStorage<Texture> textures;
    AssetServer server { std::make_unique<TestReader>() };
    server.registerStorage(&textures);
    registerTextureLoader(server, &calls);
    server.load<Texture>("a.tex");

    server.registerStorage<Texture>(nullptr);
    CHECK((throws<std::runtime_error>([&] { server.load<Texture>("a.tex"); })));
}

void testServerWithoutLoaderThrows() {
    AssetStorage<Texture> textures;
    AssetServer server { std::make_unique<TestReader>() };
    server.registerStorage(&textures);
    CHECK((throws<std::runtime_error>([&] { server.load<Texture>("a.tex"); })));
    CHECK(textures.empty());
}

void testRegisteringLoaderTwiceThrows() {
    int first = 0;
    int second = 0;
    AssetStorage<Texture> textures;
    AssetServer server { std::make_unique<TestReader>() };
    server.registerStorage(&textures);
    registerTextureLoader(server, &first);
    CHECK((throws<std::invalid_argument>([&] { registerTextureLoader(server, &second); })));

    // The original loader stays registered
    auto a = server.load<Texture>("a.tex");
    CHECK(first == 1 && second == 0);
}

void testLoaderThatDoesNotConstructReturnsNull() {
    AssetStorage<Texture> textures;
    AssetServer server { std::make_unique<TestReader>() };
    server.registerStorage(&textures);
    server.registerLoader<Texture>(std::make_unique<NoConstructLoader<Texture>>());
    CHECK(!server.load<Texture>("a.tex"));
    CHECK(textures.empty());
}

void testLoaderThrowingAfterConstructDoesNotLeak() {
    g_log.clear();
    AssetStorage<Texture> textures;
    AssetServer server { std::make_unique<TestReader>() };
    server.registerStorage(&textures);
    server.registerLoader<Texture>(std::make_unique<ThrowAfterConstructLoader<Texture>>());
    CHECK(!server.load<Texture>("a.tex"));
    CHECK_EQ(g_log, std::vector<std::string>{ "~Texture a.tex" });
    CHECK(textures.empty());
}

void testDoubleConstructFailsLoad() {
    g_log.clear();
    AssetStorage<Texture> textures;
    AssetServer server { std::make_unique<TestReader>() };
    server.registerStorage(&textures);
    server.registerLoader<Texture>(std::make_unique<DoubleConstructLoader<Texture>>());
    CHECK(!server.load<Texture>("a.tex"));
    CHECK_EQ(g_log, std::vector<std::string>{ "~Texture a.tex" });
    CHECK(textures.empty());
}

void testFailedLoadIsRetried() {
    int calls = 0;
    AssetStorage<Texture> textures;
    AssetServer server { std::make_unique<TestReader>() };
    server.registerStorage(&textures);
    server.registerLoader<Texture>(std::make_unique<CountingNoConstructLoader>(&calls));
    CHECK(!server.load<Texture>("a.tex"));
    CHECK(!server.load<Texture>("a.tex"));
    CHECK_EQ(calls, 2);
}

void testFailedDependencyDoesNotPropagate() {
    g_log.clear();
    AssetStorage<Texture> textures;
    AssetStorage<Material> materials;
    AssetServer server { std::make_unique<TestReader>() };
    server.registerStorage(&textures);
    server.registerStorage(&materials);
    server.registerLoader<Texture>(std::make_unique<ThrowAfterConstructLoader<Texture>>());
    server.registerLoader<Material>(std::make_unique<MaterialLoader>());
    {
        auto material = server.load<Material>("brick.mat");
        REQUIRE(material);
        CHECK_EQ(material->name, "brick.mat");
        CHECK(!material->texture);
        CHECK(textures.empty());
    }
    CHECK((g_log == std::vector<std::string>{ "~Texture brick.mat.tex", "~Material brick.mat" }));
    CHECK(materials.empty());
}

void testLoaderChosenByExtension() {
    AssetStorage<Texture> textures;
    AssetServer server { std::make_unique<TestReader>() };
    server.registerStorage(&textures);
    server.registerLoader<Texture>(std::make_unique<TaggingTextureLoader>("pack", std::vector<std::string_view>{ ".texpack" }));
    server.registerLoader<Texture>(std::make_unique<TaggingTextureLoader>("raw", std::vector<std::string_view>{ ".png", ".JPG" }));

    CHECK_EQ(server.load<Texture>("dir/rock.png.texpack")->name, "pack:dir/rock.png.texpack");
    CHECK_EQ(server.load<Texture>("dir/rock.png")->name, "raw:dir/rock.png");
    // Extensions are compared case-insensitively.
    CHECK_EQ(server.load<Texture>("dir/ROCK.PNG")->name, "raw:dir/ROCK.PNG");
    CHECK_EQ(server.load<Texture>("dir/rock.jpg")->name, "raw:dir/rock.jpg");
}

void testUnknownExtensionThrows() {
    AssetStorage<Texture> textures;
    AssetServer server { std::make_unique<TestReader>() };
    server.registerStorage(&textures);
    server.registerLoader<Texture>(std::make_unique<TaggingTextureLoader>("raw", std::vector<std::string_view>{ ".png" }));

    CHECK((throws<std::runtime_error>([&] { server.load<Texture>("rock.jpg"); })));
    CHECK((throws<std::runtime_error>([&] { server.load<Texture>("rock"); })));
    // Only the file name's extension counts, not a dot in a folder name.
    CHECK((throws<std::runtime_error>([&] { server.load<Texture>("dir.png/rock"); })));
    CHECK(textures.empty());

    // A key that is already in storage needs no loader.
    auto preloaded = textures.emplace("builtin://rock", "rock");
    CHECK_EQ(server.load<Texture>("builtin://rock"), preloaded);
}

void testLoaderGetsReaderBytes() {
    int calls = 0;
    auto reader = std::make_unique<TestReader>();
    TestReader& log = *reader;
    AssetStorage<Texture> textures;
    AssetServer server { std::move(reader) };
    server.registerStorage(&textures);
    server.registerLoader<Texture>(std::make_unique<BytesTextureLoader>(&calls));

    auto a = server.load<Texture>("dir/a.tex");
    CHECK_EQ(a->name, "dir/a.tex");
    CHECK_EQ(log.reads, std::vector<std::string>{ "dir/a.tex" });

    // A live asset is not read again.
    auto again = server.load<Texture>("dir/a.tex");
    CHECK_EQ(again, a);
    CHECK(log.reads.size() == 1 && calls == 1);
}

void testReadFailureFailsLoad() {
    int calls = 0;
    auto reader = std::make_unique<TestReader>();
    TestReader& log = *reader;
    log.missing.insert("gone.tex");
    AssetStorage<Texture> textures;
    AssetServer server { std::move(reader) };
    server.registerStorage(&textures);
    registerTextureLoader(server, &calls);

    CHECK(!server.load<Texture>("gone.tex"));
    CHECK_EQ(calls, 0);
    CHECK(textures.empty());
    CHECK_EQ(log.reads, std::vector<std::string>{ "gone.tex" });
}

void testNothingReadWithoutLoaderOrForStoredAssets() {
    auto reader = std::make_unique<TestReader>();
    TestReader& log = *reader;
    AssetStorage<Texture> textures;
    AssetServer server { std::move(reader) };
    server.registerStorage(&textures);

    CHECK((throws<std::runtime_error>([&] { server.load<Texture>("a.tex"); })));
    auto preloaded = textures.emplace("builtin://rock", "rock");
    CHECK_EQ(server.load<Texture>("builtin://rock"), preloaded);
    CHECK(log.reads.empty());
}

void testAssetPathParse() {
    static_assert(AssetPath::parse("engine:/textures/rock.png").source == "engine");
    static_assert(AssetPath::parse("engine:/textures/rock.png").path == "textures/rock.png");
    static_assert(AssetPath::parse("textures/rock.png").source.empty());
    static_assert(AssetPath::parse("textures/rock.png").path == "textures/rock.png");
    // A ":/" after a '/' belongs to the path.
    static_assert(AssetPath::parse("dir/a:/b").source.empty());
    static_assert(AssetPath::parse("dir/a:/b").path == "dir/a:/b");
}

void testReaderChosenBySource() {
    int calls = 0;
    auto defaultReader = std::make_unique<TestReader>();
    TestReader& defaultLog = *defaultReader;
    auto engineReader = std::make_unique<TestReader>();
    TestReader& engineLog = *engineReader;
    AssetStorage<Texture> textures;
    AssetServer server { std::move(defaultReader) };
    server.registerStorage(&textures);
    server.registerLoader<Texture>(std::make_unique<BytesTextureLoader>(&calls));
    server.registerReader("engine", std::move(engineReader));

    // The reader gets the path without its source; the asset keeps the whole key.
    auto engine = server.load<Texture>("engine:/textures/rock.tex");
    CHECK_EQ(engine->name, "textures/rock.tex");
    CHECK(textures.has("engine:/textures/rock.tex"));
    auto plain = server.load<Texture>("textures/rock.tex");
    CHECK_EQ(plain->name, "textures/rock.tex");
    CHECK_NE(plain, engine);

    CHECK_EQ(engineLog.reads, std::vector<std::string>{ "textures/rock.tex" });
    CHECK_EQ(defaultLog.reads, std::vector<std::string>{ "textures/rock.tex" });
}

void testDependencyKeepsSource() {
    auto engineReader = std::make_unique<TestReader>();
    TestReader& engineLog = *engineReader;
    AssetStorage<Texture> textures;
    AssetStorage<Material> materials;
    AssetServer server { std::make_unique<TestReader>() };
    server.registerStorage(&textures);
    server.registerStorage(&materials);
    int calls = 0;
    registerTextureLoader(server, &calls);
    server.registerLoader<Material>(std::make_unique<MaterialLoader>());
    server.registerReader("engine", std::move(engineReader));

    auto brick = server.load<Material>("engine:/brick.mat");
    CHECK(brick && brick->texture->name == "engine:/brick.mat.tex");
    CHECK((engineLog.reads == std::vector<std::string>{ "brick.mat", "brick.mat.tex" }));
}

void testUnknownSourceThrows() {
    auto reader = std::make_unique<TestReader>();
    TestReader& log = *reader;
    AssetStorage<Texture> textures;
    AssetServer server { std::move(reader) };
    server.registerStorage(&textures);
    int calls = 0;
    registerTextureLoader(server, &calls);

    CHECK((throws<std::runtime_error>([&] { server.load<Texture>("engine:/rock.tex"); })));
    CHECK(log.reads.empty() && calls == 0 && textures.empty());

    // A key already in storage needs no reader.
    auto preloaded = textures.emplace("engine:/rock.tex", "rock");
    CHECK_EQ(server.load<Texture>("engine:/rock.tex"), preloaded);
}

void testRegisterReaderValidates() {
    AssetServer server { std::make_unique<TestReader>() };
    server.registerReader("engine", std::make_unique<TestReader>());
    CHECK((throws<std::invalid_argument>([&] { server.registerReader("engine", std::make_unique<TestReader>()); })));
    CHECK((throws<std::invalid_argument>([&] { server.registerReader("", std::make_unique<TestReader>()); })));
    CHECK((throws<std::invalid_argument>([&] { server.registerReader("a/b", std::make_unique<TestReader>()); })));
    CHECK((throws<std::invalid_argument>([&] { server.registerReader("a:b", std::make_unique<TestReader>()); })));
    CHECK((throws<std::invalid_argument>([&] { server.registerReader("game", nullptr); })));
}

void testLoaderExtensionsMustBeDisjoint() {
    AssetStorage<Texture> textures;
    AssetStorage<Material> materials;
    AssetServer server { std::make_unique<TestReader>() };
    server.registerStorage(&textures);
    server.registerStorage(&materials);
    server.registerLoader<Texture>(std::make_unique<TaggingTextureLoader>("a", std::vector<std::string_view>{ ".png" }));
    CHECK((throws<std::invalid_argument>([&] {
        server.registerLoader<Texture>(std::make_unique<TaggingTextureLoader>("b", std::vector<std::string_view>{ ".jpg", ".PNG" }));
    })));
    CHECK((throws<std::invalid_argument>([&] {
        server.registerLoader<Texture>(std::make_unique<TaggingTextureLoader>("c", std::vector<std::string_view>{}));
    })));
    // The rejected loader registered none of its extensions.
    CHECK((throws<std::runtime_error>([&] { server.load<Texture>("rock.jpg"); })));
    CHECK_EQ(server.load<Texture>("rock.png")->name, "a:rock.png");

    // Loaders of different asset types may share an extension.
    server.registerLoader<Material>(std::make_unique<MaterialLoader>());
    server.registerLoader<Texture>(std::make_unique<TaggingTextureLoader>("mat", std::vector<std::string_view>{ ".mat" }));
}

}

void runAssetServerTests() {
    ailo::test::run("asset_server/testAssetIndexDefaultIsInvalid", testAssetIndexDefaultIsInvalid);

    ailo::test::run("asset_server/testEmplaceAndGet", testEmplaceAndGet);
    ailo::test::run("asset_server/testEmplaceDuplicateKeyThrows", testEmplaceDuplicateKeyThrows);
    ailo::test::run("asset_server/testEmplaceThrowingConstructorLeavesStorageClean", testEmplaceThrowingConstructorLeavesStorageClean);
    ailo::test::run("asset_server/testGetMissingKey", testGetMissingKey);
    ailo::test::run("asset_server/testCopyMoveAndReset", testCopyMoveAndReset);
    ailo::test::run("asset_server/testSelfAssignment", testSelfAssignment);
    ailo::test::run("asset_server/testSwap", testSwap);
    ailo::test::run("asset_server/testAssigningOverLastReferenceDestroysOldAsset", testAssigningOverLastReferenceDestroysOldAsset);
    ailo::test::run("asset_server/testNullPtr", testNullPtr);
    ailo::test::run("asset_server/testStaleIndexResolvesToNothing", testStaleIndexResolvesToNothing);
    ailo::test::run("asset_server/testKeyCanBeReusedAfterRelease", testKeyCanBeReusedAfterRelease);
    ailo::test::run("asset_server/testDependentReleasesDependency", testDependentReleasesDependency);
    ailo::test::run("asset_server/testNestedSameTypeRelease", testNestedSameTypeRelease);
    ailo::test::run("asset_server/testLiveAssetCount", testLiveAssetCount);

    ailo::test::run("asset_server/testServerLoad", testServerLoad);
    ailo::test::run("asset_server/testServerLoadsDependencies", testServerLoadsDependencies);
    ailo::test::run("asset_server/testConstructReturnsAsset", testConstructReturnsAsset);
    ailo::test::run("asset_server/testLoaderWithDependencyAndSubAsset", testLoaderWithDependencyAndSubAsset);
    ailo::test::run("asset_server/testServerReturnsAssetsAlreadyInStorage", testServerReturnsAssetsAlreadyInStorage);
    ailo::test::run("asset_server/testServerWithoutStorageThrows", testServerWithoutStorageThrows);
    ailo::test::run("asset_server/testUnregisteredDependencyStorageFailsLoad", testUnregisteredDependencyStorageFailsLoad);
    ailo::test::run("asset_server/testUnregisterStorage", testUnregisterStorage);
    ailo::test::run("asset_server/testServerWithoutLoaderThrows", testServerWithoutLoaderThrows);
    ailo::test::run("asset_server/testRegisteringLoaderTwiceThrows", testRegisteringLoaderTwiceThrows);
    ailo::test::run("asset_server/testLoaderThatDoesNotConstructReturnsNull", testLoaderThatDoesNotConstructReturnsNull);
    ailo::test::run("asset_server/testLoaderThrowingAfterConstructDoesNotLeak", testLoaderThrowingAfterConstructDoesNotLeak);
    ailo::test::run("asset_server/testDoubleConstructFailsLoad", testDoubleConstructFailsLoad);
    ailo::test::run("asset_server/testFailedLoadIsRetried", testFailedLoadIsRetried);
    ailo::test::run("asset_server/testFailedDependencyDoesNotPropagate", testFailedDependencyDoesNotPropagate);
    ailo::test::run("asset_server/testLoaderChosenByExtension", testLoaderChosenByExtension);
    ailo::test::run("asset_server/testUnknownExtensionThrows", testUnknownExtensionThrows);
    ailo::test::run("asset_server/testLoaderExtensionsMustBeDisjoint", testLoaderExtensionsMustBeDisjoint);
    ailo::test::run("asset_server/testLoaderGetsReaderBytes", testLoaderGetsReaderBytes);
    ailo::test::run("asset_server/testReadFailureFailsLoad", testReadFailureFailsLoad);
    ailo::test::run("asset_server/testNothingReadWithoutLoaderOrForStoredAssets", testNothingReadWithoutLoaderOrForStoredAssets);
    ailo::test::run("asset_server/testAssetPathParse", testAssetPathParse);
    ailo::test::run("asset_server/testReaderChosenBySource", testReaderChosenBySource);
    ailo::test::run("asset_server/testDependencyKeepsSource", testDependencyKeepsSource);
    ailo::test::run("asset_server/testUnknownSourceThrows", testUnknownSourceThrows);
    ailo::test::run("asset_server/testRegisterReaderValidates", testRegisterReaderValidates);
}
