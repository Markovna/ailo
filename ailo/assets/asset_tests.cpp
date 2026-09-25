#include "Assets.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace ailo;

namespace {

std::vector<std::string> g_log;

// Runs f and returns everything it wrote to std::cerr.
template<typename F>
std::string captureStderr(F&& f) {
    std::ostringstream captured;
    auto* previous = std::cerr.rdbuf(captured.rdbuf());
    f();
    std::cerr.rdbuf(previous);
    return captured.str();
}

size_t countLines(const std::string& text) {
    return static_cast<size_t>(std::count(text.begin(), text.end(), '\n'));
}

struct Texture : Asset {
    explicit Texture(std::string name) : name(std::move(name)) {}
    ~Texture() { g_log.push_back("~Texture " + name); }
    std::string name;
};

struct Material : Asset {
    Material(std::string name, asset_ptr<Texture> texture) : name(std::move(name)), texture(std::move(texture)) {}
    ~Material() { g_log.push_back("~Material " + name); }
    std::string name;
    asset_ptr<Texture> texture;
};

// Material → Texture dependency. The Texture pool is created first, so a single pass over pools
// in creation order would see the Texture still referenced; shutdown must iterate until nothing is freed.
void testShutdownFreesInDependencyOrder() {
    g_log.clear();
    AssetManager assets;
    {
        auto texture = assets.emplaceWithPath<Texture>("tex/albedo", "albedo");
        auto material = assets.emplace<Material>("brick", texture);
    }

    auto report = captureStderr([&] { assets.shutdown(); });
    assert(report.empty());
    const std::vector<std::string> expected { "~Material brick", "~Texture albedo" };
    assert(g_log == expected);

    // Idempotent
    assert(captureStderr([&] { assets.shutdown(); }).empty());
    assert(g_log == expected);
}

void testLeaksAreReportedAndNotDestroyed() {
    g_log.clear();
    asset_ptr<Texture> holder;
    {
        AssetManager assets;
        holder = assets.emplaceWithPath<Texture>("tex/leaked", "leaked");
        assets.emplaceWithPath<Texture>("tex/unused", "unused");   // dropped immediately

        auto report = captureStderr([&] { assets.shutdown(); });
        assert(countLines(report) == 1);
        assert(report.find("Texture") != std::string::npos);
        assert(report.find("'tex/leaked'") != std::string::npos);
        assert(report.find("(1 references)") != std::string::npos);
    }
    // The manager is gone; the leaked asset was abandoned, not destroyed, so the holder is still valid.
    assert(holder->name == "leaked");
    holder.reset();   // must not touch the destroyed pool's gc queue

    const std::vector<std::string> expected { "~Texture unused" };
    assert(g_log == expected);
}

void testLeakedDependentKeepsDependencyAlive() {
    g_log.clear();
    asset_ptr<Material> holder;
    {
        AssetManager assets;
        auto texture = assets.emplaceWithPath<Texture>("tex/albedo", "albedo");
        holder = assets.emplace<Material>("brick", texture);
        texture.reset();

        auto report = captureStderr([&] { assets.shutdown(); });
        // Only the material is leaked directly; its texture is still referenced by it, so both are abandoned.
        assert(countLines(report) == 2);
        assert(report.find("'tex/albedo'") != std::string::npos);
        assert(report.find("'<no path>'") != std::string::npos);
    }
    assert(holder->texture->name == "albedo");
    assert(g_log.empty());
}

void testGcUnbindsPath() {
    g_log.clear();
    AssetManager assets;
    assets.emplaceWithPath<Texture>("tex/a", "a");   // dropped immediately
    assert(assets.get<Texture>("tex/a"));

    assert(assets.gc() == 1);
    assert(!assets.get<Texture>("tex/a"));
}

void testGcKeepsRebindingPath() {
    g_log.clear();
    AssetManager assets;
    auto first = assets.emplaceWithPath<Texture>("tex/a", "first");
    auto second = assets.emplaceWithPath<Texture>("tex/a", "second");   // path now points at `second`
    first.reset();

    assert(assets.gc() == 1);
    auto found = assets.get<Texture>("tex/a");
    assert(found && found->name == "second");
}

void testGcFreesDependenciesOnLaterCalls() {
    g_log.clear();
    AssetManager assets;
    {
        auto texture = assets.emplaceWithPath<Texture>("tex/albedo", "albedo");
        assets.emplace<Material>("brick", texture);   // dropped immediately
    }

    size_t freed = 0;
    for (int frame = 0; frame < 3; frame++) {
        freed += assets.gc();
    }
    assert(freed == 2);
    const std::vector<std::string> expected { "~Material brick", "~Texture albedo" };
    assert(g_log == expected);
}

void testDestructorShutsDown() {
    g_log.clear();
    {
        AssetManager assets;
        auto texture = assets.emplaceWithPath<Texture>("tex/albedo", "albedo");
        auto material = assets.emplace<Material>("brick", texture);
        // asset_ptrs are destroyed before the manager (declared after it)
    }
    const std::vector<std::string> expected { "~Material brick", "~Texture albedo" };
    assert(g_log == expected);
}

}

int main() {
    testShutdownFreesInDependencyOrder();
    testLeaksAreReportedAndNotDestroyed();
    testLeakedDependentKeepsDependencyAlive();
    testGcUnbindsPath();
    testGcKeepsRebindingPath();
    testGcFreesDependenciesOnLaterCalls();
    testDestructorShutsDown();

    std::cout << "All asset tests passed" << std::endl;
    return 0;
}
