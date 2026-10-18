#include "AssetServer.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
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
    void load(const std::string& key, LoadContext<Texture>& context) override {
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
    void load(const std::string& key, LoadContext<Material>& context) override {
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
    void load(const std::string& key, LoadContext<Model>& context) override {
        auto material = context.load<Material>(key + ".mat");
        auto embedded = m_textures->emplace(noname_t{}, "embedded");
        context.construct(std::move(material), std::move(embedded));
    }
private:
    AssetStorage<Texture>* m_textures;
};

class RenamingTextureLoader : public TexLoader<Texture> {
public:
    void load(const std::string& key, LoadContext<Texture>& context) override {
        Texture& texture = context.construct("unnamed");
        texture.name = key;
    }
};

template<class T>
class NoConstructLoader : public TexLoader<T> {
public:
    void load(const std::string&, LoadContext<T>&) override {}
};

class CountingNoConstructLoader : public TexLoader<Texture> {
public:
    explicit CountingNoConstructLoader(int* calls) : m_calls(calls) {}
    void load(const std::string&, LoadContext<Texture>&) override { ++*m_calls; }
private:
    int* m_calls;
};

template<class T>
class ThrowAfterConstructLoader : public TexLoader<T> {
public:
    void load(const std::string& key, LoadContext<T>& context) override {
        context.construct(key);
        throw std::runtime_error("loader failed");
    }
};

template<class T>
class DoubleConstructLoader : public TexLoader<T> {
public:
    void load(const std::string& key, LoadContext<T>& context) override {
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
    void load(const std::string& key, LoadContext<Texture>& context) override {
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
    assert(!index.isValid());
    assert(index == AssetIndex<Texture>{});
}

// ---------------------------------------------------------------------------
// AssetStorage / AssetPtr
// ---------------------------------------------------------------------------

void testEmplaceAndGet() {
    g_log.clear();
    AssetStorage<Texture> storage;
    {
        auto ptr = storage.emplace("a", "albedo");
        assert(ptr);
        assert(ptr.index().isValid());
        assert(ptr->name == "albedo");
        assert((*ptr).name == "albedo");
        assert(ptr.get() == storage.get(ptr.index()));
        assert(ptr.useCount() == 1);
        assert(storage.has("a"));
        assert(storage.size() == 1);

        auto found = storage.get("a");
        assert(found && *found == ptr);
        assert(ptr.useCount() == 2);
    }
    assert(storage.empty());
    assert(!storage.has("a"));
    assert(!storage.get("a"));
    assert(g_log == std::vector<std::string>{ "~Texture albedo" });
}

void testEmplaceDuplicateKeyThrows() {
    AssetStorage<Texture> storage;
    auto ptr = storage.emplace("a", "first");
    assert((throws<std::invalid_argument>([&] { storage.emplace("a", "second"); })));
    assert(storage.size() == 1);
    assert(ptr->name == "first");
}

void testEmplaceThrowingConstructorLeavesStorageClean() {
    AssetStorage<Throwing> storage;
    assert((throws<std::runtime_error>([&] { storage.emplace("a", true); })));
    assert(storage.empty());
    assert(!storage.has("a"));

    auto ptr = storage.emplace("a", false);
    assert(ptr && storage.size() == 1);
}

void testGetMissingKey() {
    AssetStorage<Texture> storage;
    assert(!storage.has("missing"));
    assert(!storage.get("missing"));
}

void testCopyMoveAndReset() {
    AssetStorage<Texture> storage;
    auto a = storage.emplace("a", "a");

    AssetPtr<Texture> copy = a;
    assert(copy == a && a.useCount() == 2);

    AssetPtr<Texture> moved = std::move(copy);
    assert(!copy && copy.useCount() == 0 && !copy.index().isValid());
    assert(moved == a && a.useCount() == 2);

    AssetPtr<Texture> assigned;
    assigned = a;
    assert(a.useCount() == 3);

    assigned = std::move(moved);
    assert(!moved);
    assert(a.useCount() == 2);

    assigned.reset();
    assert(!assigned && assigned.get() == nullptr);
    assert(a.useCount() == 1);

    assigned.reset();   // idempotent
    assert(a.useCount() == 1);
}

void testSelfAssignment() {
    AssetStorage<Texture> storage;
    auto a = storage.emplace("a", "a");
    auto& alias = a;
    a = alias;
    assert(a && a.useCount() == 1);
    a = std::move(alias);
    assert(a && a.useCount() == 1);
}

void testSwap() {
    AssetStorage<Texture> storage;
    auto a = storage.emplace("a", "a");
    auto b = storage.emplace("b", "b");
    a.swap(b);
    assert(a->name == "b" && b->name == "a");
    assert(a.useCount() == 1 && b.useCount() == 1);
}

void testAssigningOverLastReferenceDestroysOldAsset() {
    g_log.clear();
    AssetStorage<Texture> storage;
    auto ptr = storage.emplace("a", "a");
    ptr = storage.emplace("b", "b");
    assert(g_log == std::vector<std::string>{ "~Texture a" });
    assert(!storage.has("a") && storage.has("b"));
}

void testNullPtr() {
    AssetPtr<Texture> ptr;
    assert(!ptr);
    assert(ptr.get() == nullptr);
    assert(ptr.useCount() == 0);
    assert(ptr == AssetPtr<Texture>{});
}

void testStaleIndexResolvesToNothing() {
    AssetStorage<Texture> storage;
    AssetIndex<Texture> stale;
    {
        auto ptr = storage.emplace("a", "a");
        stale = ptr.index();
        assert(storage.get(stale) != nullptr);
    }
    assert(storage.get(stale) == nullptr);

    // Churn enough slots that the freed index gets recycled with a newer version
    std::vector<AssetPtr<Texture>> keep;
    for (int i = 0; i < 200; ++i) {
        storage.emplace("tmp" + std::to_string(i), "tmp");
    }
    for (int i = 0; i < 200; ++i) {
        keep.push_back(storage.emplace("k" + std::to_string(i), "k"));
    }
    assert(storage.get(stale) == nullptr);
    for (const auto& p : keep) {
        assert(!(p.index() == stale));
    }
    const AssetStorage<Texture>& constStorage = storage;
    assert(constStorage.get(stale) == nullptr);
}

void testKeyCanBeReusedAfterRelease() {
    AssetStorage<Texture> storage;
    AssetIndex<Texture> first;
    {
        auto ptr = storage.emplace("a", "first");
        first = ptr.index();
    }
    auto ptr = storage.emplace("a", "second");
    assert(ptr->name == "second");
    assert(!(ptr.index() == first));
    assert(storage.get(first) == nullptr);
}

void testDependentReleasesDependency() {
    g_log.clear();
    AssetStorage<Texture> textures;
    AssetStorage<Material> materials;
    {
        auto material = materials.emplace("brick", "brick", textures.emplace("t", "albedo"));
        assert(textures.has("t"));
        assert(material->texture.useCount() == 1);
    }
    assert(materials.empty() && textures.empty());
    assert((g_log == std::vector<std::string>{ "~Material brick", "~Texture albedo" }));
}

void testNestedSameTypeRelease() {
    g_log.clear();
    AssetStorage<Node> storage;
    {
        auto tail = storage.emplace("c", "c", AssetPtr<Node>{});
        auto middle = storage.emplace("b", "b", std::move(tail));
        auto head = storage.emplace("a", "a", std::move(middle));
        assert(storage.size() == 3);
    }
    assert(storage.empty());
    assert((g_log == std::vector<std::string>{ "~Node a", "~Node b", "~Node c" }));
}

void testReportLeaks() {
    AssetStorage<Texture> textures;
    AssetStorage<Material> materials;
    AssetServer server;
    server.registerStorage(&textures);
    server.registerStorage(&materials);

    std::ostringstream none;
    textures.reportLeaks(none);
    assert(none.str().empty());

    materials.reportLeaks(none);
    assert(materials.empty());

    auto texture = textures.emplace("t", "t");
    auto copy = texture;
    auto material = materials.emplace("m", "m", AssetPtr<Texture>{});

    std::ostringstream out;
    textures.reportLeaks(out);
    materials.reportLeaks(out);
    const std::string report = out.str();
    assert(report.find("'t' (2 references)") != std::string::npos);
    assert(report.find("'m' (1 references)") != std::string::npos);
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
    AssetServer server;
    server.registerStorage(&textures);
    registerTextureLoader(server, &calls);
    {
        auto a = server.load<Texture>("a.tex");
        assert(a && a->name == "a.tex");
        assert(a.useCount() == 1);
        assert(textures.has("a.tex"));
        assert(calls == 1);

        auto again = server.load<Texture>("a.tex");
        assert(again == a);
        assert(a.useCount() == 2);
        assert(calls == 1);

        auto b = server.load<Texture>("b.tex");
        assert(!(b == a));
        assert(calls == 2);
    }
    assert((g_log == std::vector<std::string>{ "~Texture b.tex", "~Texture a.tex" }));
    assert(textures.empty());

    // Assets are not cached past their last reference
    auto a = server.load<Texture>("a.tex");
    assert(calls == 3);
}

void testServerLoadsDependencies() {
    g_log.clear();
    int calls = 0;
    AssetStorage<Texture> textures;
    AssetStorage<Material> materials;
    AssetServer server;
    server.registerStorage(&textures);
    server.registerStorage(&materials);
    registerTextureLoader(server, &calls);
    server.registerLoader<Material>(std::make_unique<MaterialLoader>());
    {
        auto texture = server.load<Texture>("brick.mat.tex");
        auto material = server.load<Material>("brick.mat");
        assert(material->texture == texture);
        assert(texture.useCount() == 2);
        assert(calls == 1);
    }
    assert((g_log == std::vector<std::string>{ "~Material brick.mat", "~Texture brick.mat.tex" }));
}

void testConstructReturnsAsset() {
    AssetStorage<Texture> textures;
    AssetServer server;
    server.registerStorage(&textures);
    server.registerLoader<Texture>(std::make_unique<RenamingTextureLoader>());
    auto texture = server.load<Texture>("renamed.tex");
    assert(texture->name == "renamed.tex");
}

void testLoaderWithDependencyAndSubAsset() {
    int calls = 0;
    AssetStorage<Texture> textures;
    AssetStorage<Material> materials;
    AssetStorage<Model> models;
    AssetServer server;
    server.registerStorage(&textures);
    server.registerStorage(&materials);
    server.registerStorage(&models);
    registerTextureLoader(server, &calls);
    server.registerLoader<Material>(std::make_unique<MaterialLoader>());
    server.registerLoader<Model>(std::make_unique<ModelLoader>(&textures));

    AssetPtr<Texture> keptSubAsset;
    {
        auto model = server.load<Model>("house.model");
        assert(model->material->texture->name == "house.model.mat.tex");
        assert(model->embedded->name == "embedded");
        assert(textures.size() == 2);
        keptSubAsset = model->embedded;
    }
    assert(models.empty() && materials.empty());
    assert(textures.size() == 1);

    // Re-importing while the old anonymous sub-asset is still alive creates a new one
    auto model = server.load<Model>("house.model");
    assert(!(model->embedded == keptSubAsset));
    assert(textures.size() == 3);
}

void testServerReturnsAssetsAlreadyInStorage() {
    int calls = 0;
    AssetStorage<Texture> textures;
    AssetServer server;
    server.registerStorage(&textures);
    registerTextureLoader(server, &calls);

    auto preloaded = textures.emplace("pre.tex", "preloaded");
    auto found = server.load<Texture>("pre.tex");
    assert(found == preloaded);
    assert(calls == 0);
}

void testServerWithoutStorageThrows() {
    int calls = 0;
    AssetServer server;
    registerTextureLoader(server, &calls);
    assert((throws<std::runtime_error>([&] { server.load<Texture>("a.tex"); })));
    assert(calls == 0);
}

void testUnregisteredDependencyStorageFailsLoad() {
    g_log.clear();
    int calls = 0;
    AssetStorage<Material> materials;
    AssetServer server;
    server.registerStorage(&materials);
    registerTextureLoader(server, &calls);
    server.registerLoader<Material>(std::make_unique<MaterialLoader>());

    assert(!server.load<Material>("brick.mat"));
    assert(calls == 0);
    assert(materials.empty());
    assert(g_log.empty());
}

void testUnregisterStorage() {
    int calls = 0;
    AssetStorage<Texture> textures;
    AssetServer server;
    server.registerStorage(&textures);
    registerTextureLoader(server, &calls);
    server.load<Texture>("a.tex");

    server.registerStorage<Texture>(nullptr);
    assert((throws<std::runtime_error>([&] { server.load<Texture>("a.tex"); })));
}

void testServerWithoutLoaderThrows() {
    AssetStorage<Texture> textures;
    AssetServer server;
    server.registerStorage(&textures);
    assert((throws<std::runtime_error>([&] { server.load<Texture>("a.tex"); })));
    assert(textures.empty());
}

void testRegisteringLoaderTwiceThrows() {
    int first = 0;
    int second = 0;
    AssetStorage<Texture> textures;
    AssetServer server;
    server.registerStorage(&textures);
    registerTextureLoader(server, &first);
    assert((throws<std::invalid_argument>([&] { registerTextureLoader(server, &second); })));

    // The original loader stays registered
    auto a = server.load<Texture>("a.tex");
    assert(first == 1 && second == 0);
}

void testLoaderThatDoesNotConstructReturnsNull() {
    AssetStorage<Texture> textures;
    AssetServer server;
    server.registerStorage(&textures);
    server.registerLoader<Texture>(std::make_unique<NoConstructLoader<Texture>>());
    assert(!server.load<Texture>("a.tex"));
    assert(textures.empty());
}

void testLoaderThrowingAfterConstructDoesNotLeak() {
    g_log.clear();
    AssetStorage<Texture> textures;
    AssetServer server;
    server.registerStorage(&textures);
    server.registerLoader<Texture>(std::make_unique<ThrowAfterConstructLoader<Texture>>());
    assert(!server.load<Texture>("a.tex"));
    assert(g_log == std::vector<std::string>{ "~Texture a.tex" });
    assert(textures.empty());
}

void testDoubleConstructFailsLoad() {
    g_log.clear();
    AssetStorage<Texture> textures;
    AssetServer server;
    server.registerStorage(&textures);
    server.registerLoader<Texture>(std::make_unique<DoubleConstructLoader<Texture>>());
    assert(!server.load<Texture>("a.tex"));
    assert(g_log == std::vector<std::string>{ "~Texture a.tex" });
    assert(textures.empty());
}

void testFailedLoadIsRetried() {
    int calls = 0;
    AssetStorage<Texture> textures;
    AssetServer server;
    server.registerStorage(&textures);
    server.registerLoader<Texture>(std::make_unique<CountingNoConstructLoader>(&calls));
    assert(!server.load<Texture>("a.tex"));
    assert(!server.load<Texture>("a.tex"));
    assert(calls == 2);
}

void testFailedDependencyDoesNotPropagate() {
    g_log.clear();
    AssetStorage<Texture> textures;
    AssetStorage<Material> materials;
    AssetServer server;
    server.registerStorage(&textures);
    server.registerStorage(&materials);
    server.registerLoader<Texture>(std::make_unique<ThrowAfterConstructLoader<Texture>>());
    server.registerLoader<Material>(std::make_unique<MaterialLoader>());
    {
        auto material = server.load<Material>("brick.mat");
        assert(material && material->name == "brick.mat");
        assert(!material->texture);
        assert(textures.empty());
    }
    assert((g_log == std::vector<std::string>{ "~Texture brick.mat.tex", "~Material brick.mat" }));
    assert(materials.empty());
}

void testLoaderChosenByExtension() {
    AssetStorage<Texture> textures;
    AssetServer server;
    server.registerStorage(&textures);
    server.registerLoader<Texture>(std::make_unique<TaggingTextureLoader>("pack", std::vector<std::string_view>{ ".texpack" }));
    server.registerLoader<Texture>(std::make_unique<TaggingTextureLoader>("raw", std::vector<std::string_view>{ ".png", ".JPG" }));

    assert(server.load<Texture>("dir/rock.png.texpack")->name == "pack:dir/rock.png.texpack");
    assert(server.load<Texture>("dir/rock.png")->name == "raw:dir/rock.png");
    // Extensions are compared case-insensitively.
    assert(server.load<Texture>("dir/ROCK.PNG")->name == "raw:dir/ROCK.PNG");
    assert(server.load<Texture>("dir/rock.jpg")->name == "raw:dir/rock.jpg");
}

void testUnknownExtensionThrows() {
    AssetStorage<Texture> textures;
    AssetServer server;
    server.registerStorage(&textures);
    server.registerLoader<Texture>(std::make_unique<TaggingTextureLoader>("raw", std::vector<std::string_view>{ ".png" }));

    assert((throws<std::runtime_error>([&] { server.load<Texture>("rock.jpg"); })));
    assert((throws<std::runtime_error>([&] { server.load<Texture>("rock"); })));
    // Only the file name's extension counts, not a dot in a folder name.
    assert((throws<std::runtime_error>([&] { server.load<Texture>("dir.png/rock"); })));
    assert(textures.empty());

    // A key that is already in storage needs no loader.
    auto preloaded = textures.emplace("builtin://rock", "rock");
    assert(server.load<Texture>("builtin://rock") == preloaded);
}

void testLoaderExtensionsMustBeDisjoint() {
    AssetStorage<Texture> textures;
    AssetStorage<Material> materials;
    AssetServer server;
    server.registerStorage(&textures);
    server.registerStorage(&materials);
    server.registerLoader<Texture>(std::make_unique<TaggingTextureLoader>("a", std::vector<std::string_view>{ ".png" }));
    assert((throws<std::invalid_argument>([&] {
        server.registerLoader<Texture>(std::make_unique<TaggingTextureLoader>("b", std::vector<std::string_view>{ ".jpg", ".PNG" }));
    })));
    assert((throws<std::invalid_argument>([&] {
        server.registerLoader<Texture>(std::make_unique<TaggingTextureLoader>("c", std::vector<std::string_view>{}));
    })));
    // The rejected loader registered none of its extensions.
    assert((throws<std::runtime_error>([&] { server.load<Texture>("rock.jpg"); })));
    assert(server.load<Texture>("rock.png")->name == "a:rock.png");

    // Loaders of different asset types may share an extension.
    server.registerLoader<Material>(std::make_unique<MaterialLoader>());
    server.registerLoader<Texture>(std::make_unique<TaggingTextureLoader>("mat", std::vector<std::string_view>{ ".mat" }));
}

}

int main() {
    testAssetIndexDefaultIsInvalid();

    testEmplaceAndGet();
    testEmplaceDuplicateKeyThrows();
    testEmplaceThrowingConstructorLeavesStorageClean();
    testGetMissingKey();
    testCopyMoveAndReset();
    testSelfAssignment();
    testSwap();
    testAssigningOverLastReferenceDestroysOldAsset();
    testNullPtr();
    testStaleIndexResolvesToNothing();
    testKeyCanBeReusedAfterRelease();
    testDependentReleasesDependency();
    testNestedSameTypeRelease();
    testReportLeaks();

    testServerLoad();
    testServerLoadsDependencies();
    testConstructReturnsAsset();
    testLoaderWithDependencyAndSubAsset();
    testServerReturnsAssetsAlreadyInStorage();
    testServerWithoutStorageThrows();
    testUnregisteredDependencyStorageFailsLoad();
    testUnregisterStorage();
    testServerWithoutLoaderThrows();
    testRegisteringLoaderTwiceThrows();
    testLoaderThatDoesNotConstructReturnsNull();
    testLoaderThrowingAfterConstructDoesNotLeak();
    testDoubleConstructFailsLoad();
    testFailedLoadIsRetried();
    testFailedDependencyDoesNotPropagate();
    testLoaderChosenByExtension();
    testUnknownExtensionThrows();
    testLoaderExtensionsMustBeDisjoint();

    std::cout << "All asset server tests passed" << std::endl;
    return 0;
}
