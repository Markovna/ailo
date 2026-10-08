#include "AssetServer.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ailo::assets;

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

struct Throwing {
    explicit Throwing(bool shouldThrow) {
        if (shouldThrow) throw std::runtime_error("ctor");
    }
};

class TextureLoader : public AssetLoader<Texture> {
public:
    explicit TextureLoader(int* calls) : m_calls(calls) {}
    void load(const std::string& key, LoadContext<Texture>& context) override {
        ++*m_calls;
        context.construct(key);
    }
private:
    int* m_calls;
};

// "brick" -> texture "brick.tex"
class MaterialLoader : public AssetLoader<Material> {
public:
    void load(const std::string& key, LoadContext<Material>& context) override {
        auto texture = context.load<Texture>(key + ".tex");
        context.construct(key, std::move(texture));
    }
};

template<class T>
class NoConstructLoader : public AssetLoader<T> {
public:
    void load(const std::string&, LoadContext<T>&) override {}
};

template<class T>
class ThrowAfterConstructLoader : public AssetLoader<T> {
public:
    void load(const std::string& key, LoadContext<T>& context) override {
        context.construct(key);
        throw std::runtime_error("loader failed");
    }
};

template<class T>
class DoubleConstructLoader : public AssetLoader<T> {
public:
    void load(const std::string& key, LoadContext<T>& context) override {
        context.construct(key);
        context.construct(key);
    }
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

// ---------------------------------------------------------------------------
// AssetServer
// ---------------------------------------------------------------------------

void registerTextureLoader(AssetServer& server, int* calls) {
    server.registerLoader<Texture>([calls] { return std::make_unique<TextureLoader>(calls); });
}

void testServerLoad() {
    g_log.clear();
    int calls = 0;
    AssetStorage<Texture> textures;
    AssetServer server;
    server.registerStorage(&textures);
    registerTextureLoader(server, &calls);
    {
        auto a = server.load<Texture>("a");
        assert(a && a->name == "a");
        assert(a.useCount() == 1);
        assert(textures.has("a"));
        assert(calls == 1);

        auto again = server.load<Texture>("a");
        assert(again == a);
        assert(a.useCount() == 2);
        assert(calls == 1);

        auto b = server.load<Texture>("b");
        assert(!(b == a));
        assert(calls == 2);
    }
    assert((g_log == std::vector<std::string>{ "~Texture b", "~Texture a" }));
    assert(textures.empty());

    // Assets are not cached past their last reference
    auto a = server.load<Texture>("a");
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
    server.registerLoader<Material>([] { return std::make_unique<MaterialLoader>(); });
    {
        auto texture = server.load<Texture>("brick.tex");
        auto material = server.load<Material>("brick");
        assert(material->texture == texture);
        assert(texture.useCount() == 2);
        assert(calls == 1);
    }
    assert((g_log == std::vector<std::string>{ "~Material brick", "~Texture brick.tex" }));
}

void testServerReturnsAssetsAlreadyInStorage() {
    int calls = 0;
    AssetStorage<Texture> textures;
    AssetServer server;
    server.registerStorage(&textures);
    registerTextureLoader(server, &calls);

    auto preloaded = textures.emplace("pre", "preloaded");
    auto found = server.load<Texture>("pre");
    assert(found == preloaded);
    assert(calls == 0);
}

void testServerWithoutStorageThrows() {
    int calls = 0;
    AssetServer server;
    registerTextureLoader(server, &calls);
    assert((throws<std::runtime_error>([&] { server.load<Texture>("a"); })));
    assert(calls == 0);
}

void testUnregisteredDependencyStorageThrows() {
    g_log.clear();
    int calls = 0;
    AssetStorage<Material> materials;
    AssetServer server;
    server.registerStorage(&materials);
    registerTextureLoader(server, &calls);
    server.registerLoader<Material>([] { return std::make_unique<MaterialLoader>(); });

    assert((throws<std::runtime_error>([&] { server.load<Material>("brick"); })));
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
    server.load<Texture>("a");

    server.registerStorage<Texture>(nullptr);
    assert((throws<std::runtime_error>([&] { server.load<Texture>("a"); })));
}

void testServerWithoutLoaderThrows() {
    AssetStorage<Texture> textures;
    AssetServer server;
    server.registerStorage(&textures);
    assert((throws<std::runtime_error>([&] { server.load<Texture>("a"); })));
    assert(textures.empty());
}

void testLoaderThatDoesNotConstructThrows() {
    AssetStorage<Texture> textures;
    AssetServer server;
    server.registerStorage(&textures);
    server.registerLoader<Texture>([] { return std::make_unique<NoConstructLoader<Texture>>(); });
    assert((throws<std::runtime_error>([&] { server.load<Texture>("a"); })));
}

void testLoaderThrowingAfterConstructDoesNotLeak() {
    g_log.clear();
    AssetStorage<Texture> textures;
    AssetServer server;
    server.registerStorage(&textures);
    server.registerLoader<Texture>([] { return std::make_unique<ThrowAfterConstructLoader<Texture>>(); });
    assert((throws<std::runtime_error>([&] { server.load<Texture>("a"); })));
    assert(g_log == std::vector<std::string>{ "~Texture a" });
    assert(textures.empty());
}

void testDoubleConstructThrows() {
    g_log.clear();
    AssetStorage<Texture> textures;
    AssetServer server;
    server.registerStorage(&textures);
    server.registerLoader<Texture>([] { return std::make_unique<DoubleConstructLoader<Texture>>(); });
    assert((throws<std::logic_error>([&] { server.load<Texture>("a"); })));
    assert(g_log == std::vector<std::string>{ "~Texture a" });
    assert(textures.empty());
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

    testServerLoad();
    testServerLoadsDependencies();
    testServerReturnsAssetsAlreadyInStorage();
    testServerWithoutStorageThrows();
    testUnregisteredDependencyStorageThrows();
    testUnregisterStorage();
    testServerWithoutLoaderThrows();
    testLoaderThatDoesNotConstructThrows();
    testLoaderThrowingAfterConstructDoesNotLeak();
    testDoubleConstructThrows();

    std::cout << "All asset server tests passed" << std::endl;
    return 0;
}
