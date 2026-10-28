#include <algorithm>
#include <bit>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "FileIO.h"
#include "MaterialDefinition.h"
#include "MaterialInstanceDefinition.h"
#include "ShaderCompiler.h"
#include "ShaderGenerator.h"

namespace fs = std::filesystem;
using namespace ailo::material;
using namespace matc;
using namespace ailo::fileio;

namespace {

void printUsage() {
    std::cerr <<
        "usage: matc [-I <dir>]... -o <output.mat> [--depfile <file>] [--emit-glsl <dir>] <input.mat>\n"
        "       matc -o <output.matinst> [--depfile <file>] [--root <assets folder>] <input.matinst>\n"
        "       matc --dump <package.mat|package.matinst>\n";
}

std::string variantName(ShaderStage stage, Variant v) {
    std::string name = std::string(toString(stage));
    if (v.isDepth()) name += "_depth";
    if (v.hasSkinning()) name += "_skinned";
    if (v.isShadowReceiver()) name += "_shadowed";
    return name;
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

std::string formatWords(ParameterType type, std::span<const uint32_t> words) {
    const std::string_view typeName = toString(type);

    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < words.size(); i++) {
        if (i) out << ", ";
        if (typeName.starts_with("float")) out << std::bit_cast<float>(words[i]);
        else if (typeName.starts_with("int")) out << std::bit_cast<int32_t>(words[i]);
        else if (typeName.starts_with("bool")) out << (words[i] ? "true" : "false");
        else out << words[i];
    }
    out << "]";
    return out.str();
}

std::string formatDefault(const MaterialParameter& p) {
    const bool matrix = p.type == ParameterType::Float3x3 || p.type == ParameterType::Float4x4;
    const uint32_t components = componentCount(p.type);
    const uint32_t rows = p.type == ParameterType::Float3x3 ? 3 : p.type == ParameterType::Float4x4 ? 4 : components;
    const uint32_t elements = std::max(p.arraySize, 1u);
    const uint32_t stride = elements > 1 ? p.size / elements : 0;

    std::vector<uint32_t> words;
    for (uint32_t e = 0; e < elements; e++) {
        for (uint32_t i = 0; i < components; i++) {
            uint32_t byteOffset = e * stride + (matrix ? (i / rows) * 16 + (i % rows) * 4 : i * 4);
            uint32_t& bits = words.emplace_back();
            std::memcpy(&bits, p.defaultValue.data() + byteOffset, sizeof(bits));
        }
    }
    return formatWords(p.type, words);
}

int dumpInstance(const fs::path& path, std::span<const uint8_t> data) {
    std::string error;
    auto pkg = MaterialInstancePackage::deserialize(data, error);
    if (!pkg) {
        std::cerr << path.string() << ": error: " << error << "\n";
        return 1;
    }

    std::cout << "material: " << pkg->material << "\n\nparameters\n";
    for (const auto& p : pkg->parameters) {
        std::cout << "  " << p.name << ": " << toString(p.type) << " = " << formatWords(p.type, p.value) << "\n";
    }
    std::cout << "\ntextures\n";
    for (const auto& t : pkg->textures) {
        std::cout << "  " << t.name << ": " << t.texture << "\n";
    }
    return 0;
}

int dump(const fs::path& path) {
    std::string data;
    if (!readFile(path, data)) {
        std::cerr << path.string() << ": error: cannot read file\n";
        return 1;
    }
    const std::span bytes { reinterpret_cast<const uint8_t*>(data.data()), data.size() };
    if (path.extension() == ".matinst") return dumpInstance(path, bytes);

    std::string error;
    auto pkg = MaterialPackage::deserialize(bytes, error);
    if (!pkg) {
        std::cerr << path.string() << ": error: " << error << "\n";
        return 1;
    }

    std::cout << "name:          " << pkg->name << "\n"
              << "shadingModel:  " << toString(pkg->shadingModel) << "\n"
              << "blending:      " << toString(pkg->blending) << "\n"
              << "vertexDomain:  " << toString(pkg->vertexDomain) << "\n"
              << "culling:       " << toString(pkg->raster.culling) << "\n"
              << "colorWrite:    " << pkg->raster.colorWrite << "\n"
              << "depthWrite:    " << pkg->raster.depthWrite << "\n"
              << "depthCulling:  " << pkg->raster.depthCulling << "\n"
              << "depthFunc:     " << toString(pkg->raster.depthFunc) << "\n"
              << "doubleSided:   " << pkg->raster.doubleSided << "\n";

    std::cout << "requires:     ";
    if (pkg->requiredAttributes & uint8_t(VertexAttribute::Color)) std::cout << " color";
    if (pkg->requiredAttributes & uint8_t(VertexAttribute::UV0)) std::cout << " uv0";
    if (pkg->requiredAttributes & uint8_t(VertexAttribute::Tangents)) std::cout << " tangents";
    if (pkg->requiredAttributes & uint8_t(VertexAttribute::Normal)) std::cout << " normal";
    std::cout << "\nvariables:    ";
    for (const auto& v : pkg->variables) std::cout << " " << v;
    std::cout << "\nvariantFilter: 0x" << std::hex << uint32_t(pkg->variantFilter) << std::dec << "\n";

    std::cout << "\nparameters (MaterialParams block: " << pkg->uniformBlockSize << " bytes)\n";
    for (const auto& p : pkg->parameters) {
        std::cout << "  " << p.name << ": " << toString(p.type);
        if (p.arraySize) std::cout << "[" << p.arraySize << "]";
        if (isSampler(p.type)) {
            std::cout << "  binding " << p.binding << ", default " << toString(p.samplerDefault) << "\n";
        } else {
            std::cout << "  offset " << p.offset << ", size " << p.size << ", default " << formatDefault(p) << "\n";
        }
    }

    std::cout << "\nshaders\n";
    for (const auto& s : pkg->shaders) {
        std::cout << "  " << variantName(s.stage, s.variant) << " (0x" << std::hex << uint32_t(s.variant.key) << std::dec
                  << "): blob " << s.blobIndex << ", " << pkg->blobs[s.blobIndex].size() * 4 << " bytes\n";
    }
    return 0;
}

// "/x" is relative to the assets folder (root), anything else to the .matinst file's folder. On Windows "/x" has
// a root directory but no root name, so it can't be joined with operator/ (that would keep only root's drive).
bool isRootRelative(const fs::path& path) {
    return path.has_root_directory() && !path.has_root_name();
}

// The source of a "<source>:/<path>" asset key (as AssetPath::parse splits it), empty for plain paths.
std::string_view assetSource(std::string_view path) {
    const auto separator = path.find(":/");
    if (separator == std::string_view::npos || path.substr(0, separator).find('/') != std::string_view::npos) return {};
    return path.substr(0, separator);
}

struct ResolvedPath {
    fs::path file;          // on disk; empty for keys of sources other than "engine"
    std::string key;        // as stored in the package
};

int compileMaterialInstance(const fs::path& input, const std::string& source, const fs::path& output,
                            const fs::path& depfile, const fs::path& root) {
    const std::string sourceName = input.generic_string();
    const fs::path folder = fs::absolute(input).parent_path().lexically_normal();

    // Paths are stored as the asset keys the engine loads, which are relative to the assets folder (root).
    auto resolve = [&](const std::string& path, int line) -> ResolvedPath {
        if (auto src = assetSource(path); !src.empty()) {
            if (src != "engine") return { {}, path };
            return { (fs::path(MATC_ENGINE_ASSETS_DIR) / path.substr(src.size() + 2)).lexically_normal(), path };
        }
        const fs::path p(path);
        if (p.has_root_name()) throw DefinitionError(line, "\"" + path + "\": absolute paths are not supported");
        if (root.empty()) {
            throw DefinitionError(line, "\"" + path + "\": matc needs the assets folder as --root to make it an asset key");
        }

        const fs::path assets = fs::absolute(root).lexically_normal();
        const fs::path file = (isRootRelative(p) ? assets / p.relative_path() : folder / p).lexically_normal();
        const fs::path key = file.lexically_relative(assets);
        if (key.empty() || *key.begin() == "..") {
            throw DefinitionError(line, file.generic_string() + " is outside the assets folder " + assets.generic_string());
        }
        return { file, key.generic_string() };
    };

    fs::path materialFile;
    auto resolveMaterial = [&](const std::string& path, int line) {
        materialFile = resolve(path, line).file;
        if (materialFile.empty()) {
            throw DefinitionError(line, "\"" + path + "\": matc can only read materials of the engine:/ source");
        }
        std::string materialSource;
        if (!readFile(materialFile, materialSource)) {
            throw DefinitionError(line, "cannot read material " + materialFile.generic_string());
        }
        try {
            MaterialDefinition material = parseMaterialDefinition(materialSource);
            if (material.package.name.empty()) material.package.name = materialFile.stem().string();
            return material;
        } catch (const DefinitionError& e) {
            throw std::runtime_error(materialFile.generic_string() + ":" + std::to_string(e.line) + ": error: " + e.what());
        }
    };

    MaterialInstancePackage pkg;
    try {
        auto def = parseMaterialInstanceDefinition(source, resolveMaterial);
        pkg = std::move(def.package);
        pkg.material = resolve(pkg.material, def.materialLine).key;
        for (size_t i = 0; i < pkg.textures.size(); i++) {
            auto texture = resolve(pkg.textures[i].texture, def.textureLines[i]);
            if (!texture.file.empty() && !fs::exists(texture.file)) {
                throw DefinitionError(def.textureLines[i], "texture " + texture.file.generic_string() + " does not exist");
            }
            pkg.textures[i].texture = texture.key;
        }
    } catch (const DefinitionError& e) {
        std::cerr << sourceName << ":" << e.line << ": error: " << e.what() << "\n";
        return 1;
    } catch (const std::runtime_error& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    auto data = pkg.serialize();
    if (!writeFile(output, data.data(), data.size())) {
        std::cerr << output.generic_string() << ": error: cannot write file\n";
        return 1;
    }

    if (!depfile.empty()) {
        std::string deps = escapeDepfilePath(output) + ":";
        deps += " " + escapeDepfilePath(fs::weakly_canonical(input));
        deps += " " + escapeDepfilePath(fs::weakly_canonical(materialFile));
        deps += "\n";
        if (!writeFile(depfile, deps.data(), deps.size())) {
            std::cerr << depfile.generic_string() << ": error: cannot write file\n";
            return 1;
        }
    }

    return 0;
}

}

int main(int argc, char** argv) {
    std::vector<fs::path> includeDirs;
    fs::path input, output, depfile, emitGlslDir, root;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        auto value = [&]() -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "matc: missing value for " << arg << "\n";
                std::exit(2);
            }
            return argv[++i];
        };

        if (arg == "--dump") return dump(value());
        if (arg == "-I") includeDirs.emplace_back(value());
        else if (arg.starts_with("-I") && arg.size() > 2) includeDirs.emplace_back(arg.substr(2));
        else if (arg == "-o") output = value();
        else if (arg == "--depfile") depfile = value();
        else if (arg == "--root") root = value();
        else if (arg == "--emit-glsl") emitGlslDir = value();
        else if (arg == "-h" || arg == "--help") { printUsage(); return 0; }
        else if (arg.starts_with("-")) { std::cerr << "matc: unknown option " << arg << "\n"; printUsage(); return 2; }
        else if (input.empty()) input = arg;
        else { std::cerr << "matc: more than one input file\n"; return 2; }
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

    if (input.extension() == ".matinst") return compileMaterialInstance(input, source, output, depfile, root);

    MaterialDefinition def;
    try {
        def = parseMaterialDefinition(source);
    } catch (const DefinitionError& e) {
        std::cerr << sourceName << ":" << e.line << ": error: " << e.what() << "\n";
        return 1;
    }

    MaterialPackage& pkg = def.package;
    if (pkg.name.empty()) pkg.name = input.stem().string();

    includeDirs.emplace_back(MATC_SHADER_DIR);
    ShaderCompiler compiler(includeDirs);

    for (ShaderStage stage : { ShaderStage::Vertex, ShaderStage::Fragment }) {
        for (Variant variant : getStageVariants(def, stage)) {
            std::string glsl = generateShader(def, stage, variant, sourceName);

            if (!emitGlslDir.empty()) {
                fs::path path = emitGlslDir / (input.stem().string() + "." + variantName(stage, variant) +
                                               (stage == ShaderStage::Vertex ? ".vert" : ".frag"));
                writeFile(path, glsl.data(), glsl.size());
            }

            std::vector<uint32_t> spirv;
            std::string error;
            // Stop at the first failure: errors in the material's code would repeat for every variant.
            if (!compiler.compile(glsl, stage, sourceName, spirv, error)) {
                std::cerr << error;
                std::cerr << sourceName << ": error: " << variantName(stage, variant) << " variant failed to compile"
                          << (emitGlslDir.empty() ? " (use --emit-glsl <dir> to inspect the generated source)" : "") << "\n";
                return 1;
            }

            uint32_t blobIndex = 0;
            while (blobIndex < pkg.blobs.size() && pkg.blobs[blobIndex] != spirv) blobIndex++;
            if (blobIndex == pkg.blobs.size()) pkg.blobs.push_back(std::move(spirv));

            pkg.shaders.push_back({ stage, variant, blobIndex });
        }
    }

    auto data = pkg.serialize();
    if (!writeFile(output, data.data(), data.size())) {
        std::cerr << output.generic_string() << ": error: cannot write file\n";
        return 1;
    }

    if (!depfile.empty()) {
        std::string deps = escapeDepfilePath(output) + ":";
        deps += " " + escapeDepfilePath(fs::weakly_canonical(input));
        for (const auto& dep : compiler.dependencies()) deps += " " + escapeDepfilePath(dep);
        deps += "\n";
        if (!writeFile(depfile, deps.data(), deps.size())) {
            std::cerr << depfile.generic_string() << ": error: cannot write file\n";
            return 1;
        }
    }

    return 0;
}
