#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "FileIO.h"
#include "Ibl.h"
#include "LightDefinition.h"
#include "SphericalHarmonics.h"
#include "render/lighting/LightingPackage.h"

namespace fs = std::filesystem;
using namespace ailo::lighting;
using namespace ailo::texture;
using namespace ailo::fileio;
using namespace lightc;

namespace {

void printUsage() {
    std::cerr <<
        "usage: lightc -o <output.light> [--depfile <file>] [--root <assets folder>] <input.light>\n"
        "       lightc --dfg -o <output.tex>\n";
}

// "/x/y.hdr" is relative to the assets folder (root), anything else to the .light file's folder. On Windows "/x" has
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

bool writeData(const fs::path& output, const std::vector<uint8_t>& data) {
    if (!writeFile(output, data.data(), data.size())) {
        std::cerr << output.generic_string() << ": error: cannot write file\n";
        return false;
    }
    return true;
}

}

int main(int argc, char** argv) {
    fs::path input, output, depfile, root;
    bool dfg = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        auto value = [&]() -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "lightc: missing value for " << arg << "\n";
                std::exit(2);
            }
            return argv[++i];
        };

        if (arg == "-o") output = value();
        else if (arg == "--dfg") dfg = true;
        else if (arg == "--depfile") depfile = value();
        else if (arg == "--root") root = value();
        else if (arg == "-h" || arg == "--help") { printUsage(); return 0; }
        else if (arg.starts_with("-")) { std::cerr << "lightc: unknown option " << arg << "\n"; printUsage(); return 2; }
        else if (input.empty()) input = arg;
        else { std::cerr << "lightc: more than one input file\n"; return 2; }
    }

    if (output.empty() || dfg == !input.empty()) {
        printUsage();
        return 2;
    }

    if (dfg) {
        return writeData(output, dfgLut().serialize()) ? 0 : 1;
    }

    const std::string sourceName = input.generic_string();

    std::string source;
    if (!readFile(input, source)) {
        std::cerr << sourceName << ": error: cannot read file\n";
        return 1;
    }

    LightDefinition def;
    try {
        def = parseLightDefinition(source);
    } catch (const DefinitionError& e) {
        std::cerr << sourceName << ":" << e.line << ": error: " << e.what() << "\n";
        return 1;
    }

    if (!isRootRelative(def.source)) {
        def.source = input.parent_path() / def.source;
    } else if (!root.empty()) {
        def.source = root / def.source.relative_path();
    } else {
        std::cerr << sourceName << ": error: " << def.source.generic_string() << " is relative to the assets folder, "
                  << "which lightc needs as --root\n";
        return 1;
    }

    std::string error;
    std::optional<TexturePackage> reflections = prefilterEnvironment(def.source, def.prefilter, error);
    if (!reflections) {
        std::cerr << sourceName << ": error: " << error << "\n";
        return 1;
    }

    LightingPackage pkg;
    // Projected before the conversion, so the SH keep full precision.
    pkg.irradianceSH = projectIrradianceSH(*reflections);
    pkg.reflections = toHalfFloat(*reflections);
    if (error = pkg.validate(); !error.empty()) {
        std::cerr << sourceName << ": error: " << error << "\n";
        return 1;
    }

    if (!writeData(output, pkg.serialize())) return 1;

    if (!depfile.empty()) {
        std::string deps = escapeDepfilePath(output) + ": " + escapeDepfilePath(fs::weakly_canonical(input)) + " " +
                           escapeDepfilePath(fs::weakly_canonical(def.source)) + "\n";
        if (!writeFile(depfile, deps.data(), deps.size())) {
            std::cerr << depfile.generic_string() << ": error: cannot write file\n";
            return 1;
        }
    }

    return 0;
}
