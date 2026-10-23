#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "FileIO.h"
#include "TextureDefinition.h"
#include "TextureEncoder.h"
#include "render/RenderApiEnums.h"
#include "render/texture/TexturePacker.h"

namespace fs = std::filesystem;
using namespace ailo::texture;
using ailo::TextureFormat;
using ailo::TextureType;
using namespace ailo::fileio;
using namespace texc;

namespace {

void printUsage() {
    std::cerr <<
        "usage: texc -o <output.texpack> [--depfile <file>] [--root <assets folder>] [--target desktop]\n"
        "            <input.tex>\n"
        "       texc --dump <package.texpack>\n";
}

// "/x/y.png" is relative to the assets folder (root), anything else to the .tex file's folder. On Windows "/x" has a
// root directory but no root name, so it can't be joined with operator/ (that would keep only root's drive).
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
    fs::path input, output, depfile, root;
    Target target = Target::Desktop;

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
        else if (arg == "--root") root = value();
        else if (arg == "--target") {
            const std::string name = value();
            auto parsed = parseTarget(name);
            if (!parsed) { std::cerr << "texc: unknown target " << name << " (expected desktop)\n"; return 2; }
            target = *parsed;
        }
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
        if (!isRootRelative(path)) {
            path = input.parent_path() / path;
        } else if (!root.empty()) {
            path = root / path.relative_path();
        } else {
            std::cerr << sourceName << ": error: " << path.generic_string() << " is relative to the assets folder, "
                      << "which texc needs as --root\n";
            return 1;
        }
    }

    std::string error;
    std::optional<TexturePackage> pkg;
    if (def.equirectangular) {
        pkg = packCubemapFromEquirect(def.sources[0], def.faceSize, def.options, error);
    } else if (def.type == TextureType::Cubemap) {
        pkg = packCubemap(std::span<const fs::path, 6>(def.sources.data(), 6), def.options, error);
    } else {
        pkg = packTexture(def.sources[0], def.options, error);
    }
    if (!pkg) {
        std::cerr << sourceName << ": error: " << error << "\n";
        return 1;
    }

    try {
        resolveSettings(def, pkg->format == TextureFormat::RGBA32F);
    } catch (const DefinitionError& e) {
        std::cerr << sourceName << ": error: " << e.what() << "\n";
        return 1;
    }

    TextureFormat format;
    if (def.format) {
        format = *def.format;
    } else {
        const bool alpha = def.usage == Usage::Color && def.alpha.value_or(hasAlpha(*pkg));
        format = chooseFormat(target, *def.usage, def.quality, alpha);
        if (def.usage == Usage::Color && def.options.srgb) format = *withSrgb(format);
    }
    if (!supports(target, format)) {
        std::cerr << sourceName << ": error: the " << toString(target) << " target has no " << toString(format) << "\n";
        return 1;
    }

    // The engine generates mipmaps only for uncompressed images.
    if (isCompressed(format) && pkg->generateMipmaps) {
        pkg->generateMipmaps = false;
        if (!generateMipLevels(*pkg, error)) {
            std::cerr << sourceName << ": error: " << error << "\n";
            return 1;
        }
    }

    pkg = encodeTexture(*pkg, format, def.quality, error);
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
