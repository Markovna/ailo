#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <meshoptimizer.h>

#include "FileIO.h"
#include "ModelCompiler.h"
#include "ModelDefinition.h"

namespace fs = std::filesystem;
using namespace ailo::model;
using namespace ailo::fileio;
using namespace modelc;

namespace {

// FIFO cache size used for the ACMR/ATVR stats in --dump.
constexpr unsigned kCacheSize = 16;

void printUsage() {
    std::cerr <<
        "usage: modelc -o <output.modelpack> [--depfile <file>] [--root <assets folder>] <input.model>\n"
        "       modelc --dump <package.modelpack>\n";
}

// "/x/y.gltf" is relative to the assets folder (root), anything else to the .model file's folder. On Windows "/x" has
// a root directory but no root name, so it can't be joined with operator/ (that would keep only root's drive).
bool isRootRelative(const fs::path& path) {
    return path.has_root_directory() && !path.has_root_name();
}

// Make-style escaping, as Ninja and CMake's DEPFILE expect.
std::string escapeDepfilePath(const fs::path& path) {
    std::string out;
    for (char c : path.generic_string()) {
        if (c == ' ' || c == '#') out += '\\';
        if (c == '$') out += '$';
        out += c;
    }
    return out;
}

std::string_view toString(TextureSource source) {
    switch (source) {
        case TextureSource::Uri:      return "uri";
        case TextureSource::Embedded: return "embedded";
        case TextureSource::Key:      return "key";
    }
    return "?";
}

int dump(const fs::path& path) {
    std::string data;
    if (!readFile(path, data)) {
        std::cerr << path.generic_string() << ": error: cannot read file\n";
        return 1;
    }
    std::string error;
    auto pkg = ModelPackage::deserialize({ reinterpret_cast<const uint8_t*>(data.data()), data.size() }, error);
    if (!pkg) {
        std::cerr << path.generic_string() << ": error: " << error << "\n";
        return 1;
    }

    std::cout << "meshes: " << pkg->meshes.size() << "\n";
    for (size_t i = 0; i < pkg->meshes.size(); i++) {
        const auto& mesh = pkg->meshes[i];
        const size_t stride = vertexStride(mesh.layout);
        auto cache = meshopt_analyzeVertexCache(mesh.indices.data(), mesh.indices.size(), mesh.vertexCount,
                                                kCacheSize, 0, 0);
        auto fetch = meshopt_analyzeVertexFetch(mesh.indices.data(), mesh.indices.size(), mesh.vertexCount, stride);
        auto overdraw = meshopt_analyzeOverdraw(mesh.indices.data(), mesh.indices.size(),
                                                reinterpret_cast<const float*>(mesh.vertices.data()), mesh.vertexCount,
                                                stride);
        std::cout << "  " << i << ": " << (mesh.layout == VertexLayout::Skinned ? "skinned" : "static") << ", "
                  << mesh.vertexCount << " vertices, " << mesh.indices.size() / 3 << " triangles, acmr "
                  << cache.acmr << ", atvr " << cache.atvr << ", overfetch " << fetch.overfetch << ", overdraw "
                  << overdraw.overdraw << "\n";
    }

    std::cout << "materials: " << pkg->materials.size() << "\n";
    for (size_t i = 0; i < pkg->materials.size(); i++) {
        const auto& material = pkg->materials[i];
        std::cout << "  " << i << ": \"" << material.name << "\" -> " << material.material << "\n";
        for (const auto& p : material.floats) {
            std::cout << "      " << p.name << " = [";
            for (uint32_t c = 0; c < p.components; c++) std::cout << (c ? ", " : "") << p.value[c];
            std::cout << "]\n";
        }
        for (const auto& p : material.textures) {
            std::cout << "      " << p.name << " = " << toString(p.texture.source) << " ";
            if (p.texture.source == TextureSource::Embedded) std::cout << p.texture.embeddedIndex << "\n";
            else std::cout << p.texture.path << "\n";
        }
    }

    std::cout << "instances: " << pkg->instances.size() << "\n";

    size_t bones = 0;
    for (const auto& node : pkg->skeleton) bones += node.boneOutputIndex >= 0;
    std::cout << "skeleton: " << pkg->skeleton.size() << " nodes, " << bones << " bones\n";

    std::cout << "clips: " << pkg->clips.size() << "\n";
    for (const auto& clip : pkg->clips) {
        std::cout << "  \"" << clip.name << "\": " << clip.duration << " s, " << clip.channels.size() << " channels\n";
    }

    std::cout << "embedded textures: " << pkg->textures.size() << "\n";
    for (size_t i = 0; i < pkg->textures.size(); i++) {
        const auto& tex = pkg->textures[i];
        std::cout << "  " << i << ": " << ailo::toString(tex.format) << " " << tex.width << "x" << tex.height
                  << "\n";
    }
    return 0;
}

}

int main(int argc, char** argv) {
    fs::path input, output, depfile, root;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        auto value = [&]() -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "modelc: missing value for " << arg << "\n";
                std::exit(2);
            }
            return argv[++i];
        };

        if (arg == "--dump") return dump(value());
        if (arg == "-o") output = value();
        else if (arg == "--depfile") depfile = value();
        else if (arg == "--root") root = value();
        else if (arg == "-h" || arg == "--help") { printUsage(); return 0; }
        else if (arg.starts_with("-")) { std::cerr << "modelc: unknown option " << arg << "\n"; printUsage(); return 2; }
        else if (input.empty()) input = arg;
        else { std::cerr << "modelc: more than one input file\n"; return 2; }
    }

    if (input.empty() || output.empty()) {
        printUsage();
        return 2;
    }

    const std::string sourceName = input.generic_string();

    std::string source;
    if (!readFile(input, source)) {
        std::cerr << sourceName << ": error: cannot read file\n";
        return 1;
    }

    ModelDefinition def;
    try {
        def = parseModelDefinition(source);
    } catch (const DefinitionError& e) {
        std::cerr << sourceName << ":" << e.line << ": error: " << e.what() << "\n";
        return 1;
    }

    // The compiler and the package's texture URIs work relative to the .model file, so root-relative paths (the
    // source and the texture overrides) are rewritten as such (e.g. ../../models/x/y.gltf).
    auto makeRelative = [&](fs::path& path) {
        if (!isRootRelative(path)) return true;
        if (root.empty()) {
            std::cerr << sourceName << ": error: " << path.generic_string() << " is relative to the assets "
                      << "folder, which modelc needs as --root\n";
            return false;
        }
        fs::path absolute = (fs::absolute(root) / path.relative_path()).lexically_normal();
        fs::path relative = absolute.lexically_relative(fs::absolute(input).parent_path().lexically_normal());
        if (relative.empty()) {
            std::cerr << sourceName << ": error: " << absolute.generic_string() << " is not reachable from "
                      << "the .model file's folder\n";
            return false;
        }
        path = relative;
        return true;
    };
    if (!makeRelative(def.source)) return 1;
    for (auto& [uri, path] : def.textures) {
        if (path.has_root_name()) {
            std::cerr << sourceName << ": error: texture \"" << uri << "\": absolute paths are not supported\n";
            return 1;
        }
        if (!makeRelative(path)) return 1;
    }

    CompileOutput result;
    try {
        result = compileModel(def, input.parent_path());
    } catch (const CompileError& e) {
        std::cerr << sourceName << ": error: " << e.what() << "\n";
        return 1;
    }
    for (const auto& warning : result.warnings) {
        std::cerr << sourceName << ": warning: " << warning << "\n";
    }

    auto data = result.package.serialize();
    if (!writeFile(output, data.data(), data.size())) {
        std::cerr << output.generic_string() << ": error: cannot write file\n";
        return 1;
    }

    if (!depfile.empty()) {
        std::string deps = escapeDepfilePath(output) + ":";
        deps += " " + escapeDepfilePath(fs::weakly_canonical(input));
        for (const auto& path : result.dependencies) deps += " " + escapeDepfilePath(path);
        deps += "\n";
        if (!writeFile(depfile, deps.data(), deps.size())) {
            std::cerr << depfile.generic_string() << ": error: cannot write file\n";
            return 1;
        }
    }

    return 0;
}
