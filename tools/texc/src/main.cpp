#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "FileIO.h"
#include "TextureDefinition.h"
#include "render/texture/TexturePacker.h"

namespace fs = std::filesystem;
using namespace ailo::texture;
using namespace ailo::fileio;
using namespace texc;

namespace {

void printUsage() {
    std::cerr <<
        "usage: texc -o <output.texpack> [--depfile <file>] <input.tex>\n"
        "       texc --dump <package.texpack>\n";
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

int dump(const fs::path& path) {
    std::string data;
    if (!readFile(path, data)) {
        std::cerr << path.generic_string() << ": error: cannot read file\n";
        return 1;
    }
    std::string error;
    auto pkg = TexturePackage::deserialize({ reinterpret_cast<const uint8_t*>(data.data()), data.size() }, error);
    if (!pkg) {
        std::cerr << path.generic_string() << ": error: " << error << "\n";
        return 1;
    }

    std::cout << "type:            " << toString(pkg->type) << "\n"
              << "format:          " << toString(pkg->format) << "\n"
              << "size:            " << pkg->width << "x" << pkg->height << "\n"
              << "layers:          " << pkg->layers() << "\n"
              << "levels:          " << pkg->levels << "\n"
              << "generateMipmaps: " << pkg->generateMipmaps << " (" << pkg->allocatedLevels() << " levels allocated)\n"
              << "data:            " << pkg->data.size() << " bytes\n";
    return 0;
}

}

int main(int argc, char** argv) {
    fs::path input, output, depfile;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        auto value = [&]() -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "texc: missing value for " << arg << "\n";
                std::exit(2);
            }
            return argv[++i];
        };

        if (arg == "--dump") return dump(value());
        if (arg == "-o") output = value();
        else if (arg == "--depfile") depfile = value();
        else if (arg == "-h" || arg == "--help") { printUsage(); return 0; }
        else if (arg.starts_with("-")) { std::cerr << "texc: unknown option " << arg << "\n"; printUsage(); return 2; }
        else if (input.empty()) input = arg;
        else { std::cerr << "texc: more than one input file\n"; return 2; }
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

    TextureDefinition def;
    try {
        def = parseTextureDefinition(source);
    } catch (const DefinitionError& e) {
        std::cerr << sourceName << ":" << e.line << ": error: " << e.what() << "\n";
        return 1;
    }

    for (auto& path : def.sources) {
        path = input.parent_path() / path;
    }

    std::string error;
    std::optional<TexturePackage> pkg = def.type == TextureType::Cubemap
        ? packCubemap(std::span<const fs::path, 6>(def.sources.data(), 6), def.options, error)
        : packTexture(def.sources[0], def.options, error);
    if (!pkg) {
        std::cerr << sourceName << ": error: " << error << "\n";
        return 1;
    }

    auto data = pkg->serialize();
    if (!writeFile(output, data.data(), data.size())) {
        std::cerr << output.generic_string() << ": error: cannot write file\n";
        return 1;
    }

    if (!depfile.empty()) {
        std::string deps = escapeDepfilePath(output) + ":";
        deps += " " + escapeDepfilePath(fs::weakly_canonical(input));
        for (const auto& path : def.sources) deps += " " + escapeDepfilePath(fs::weakly_canonical(path));
        deps += "\n";
        if (!writeFile(depfile, deps.data(), deps.size())) {
            std::cerr << depfile.generic_string() << ": error: cannot write file\n";
            return 1;
        }
    }

    return 0;
}
