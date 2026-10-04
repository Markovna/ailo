#include <algorithm>
#include <bit>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "MaterialDefinition.h"
#include "ShaderCompiler.h"
#include "ShaderGenerator.h"

namespace fs = std::filesystem;
using namespace ailo::material;
using namespace matcomp;

namespace {

void printUsage() {
    std::cerr <<
        "usage: matcomp [-I <dir>]... -o <output.matpack> [--depfile <file>] [--emit-glsl <dir>] <input.mat>\n"
        "       matcomp --dump <package.matpack>\n";
}

bool readFile(const fs::path& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::ostringstream content;
    content << file.rdbuf();
    out = content.str();
    return true;
}

bool writeFile(const fs::path& path, const void* data, size_t size) {
    if (path.has_parent_path()) fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(static_cast<const char*>(data), std::streamsize(size));
    return bool(file);
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

std::string formatDefault(const MaterialParameter& p) {
    const bool matrix = p.type == ParameterType::Float3x3 || p.type == ParameterType::Float4x4;
    const uint32_t components = componentCount(p.type);
    const uint32_t rows = p.type == ParameterType::Float3x3 ? 3 : p.type == ParameterType::Float4x4 ? 4 : components;
    const uint32_t elements = std::max(p.arraySize, 1u);
    const uint32_t stride = elements > 1 ? p.size / elements : 0;
    const std::string_view typeName = toString(p.type);

    std::ostringstream out;
    out << "[";
    for (uint32_t e = 0; e < elements; e++) {
        for (uint32_t i = 0; i < components; i++) {
            uint32_t byteOffset = e * stride + (matrix ? (i / rows) * 16 + (i % rows) * 4 : i * 4);
            uint32_t bits = 0;
            std::memcpy(&bits, p.defaultValue.data() + byteOffset, sizeof(bits));
            if (e || i) out << ", ";
            if (typeName.starts_with("float")) out << std::bit_cast<float>(bits);
            else if (typeName.starts_with("int")) out << std::bit_cast<int32_t>(bits);
            else if (typeName.starts_with("bool")) out << (bits ? "true" : "false");
            else out << bits;
        }
    }
    out << "]";
    return out.str();
}

int dump(const fs::path& path) {
    std::string data;
    if (!readFile(path, data)) {
        std::cerr << path.string() << ": error: cannot read file\n";
        return 1;
    }
    std::string error;
    auto pkg = MaterialPackage::deserialize({ reinterpret_cast<const uint8_t*>(data.data()), data.size() }, error);
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
              << "doubleSided:   " << pkg->raster.doubleSided << "\n";

    std::cout << "requires:     ";
    if (pkg->requiredAttributes & uint8_t(VertexAttribute::Color)) std::cout << " color";
    if (pkg->requiredAttributes & uint8_t(VertexAttribute::UV0)) std::cout << " uv0";
    if (pkg->requiredAttributes & uint8_t(VertexAttribute::Tangents)) std::cout << " tangents";
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

}

int main(int argc, char** argv) {
    std::vector<fs::path> includeDirs;
    fs::path input, output, depfile, emitGlslDir;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        auto value = [&]() -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "matcomp: missing value for " << arg << "\n";
                std::exit(2);
            }
            return argv[++i];
        };

        if (arg == "--dump") return dump(value());
        if (arg == "-I") includeDirs.emplace_back(value());
        else if (arg.starts_with("-I") && arg.size() > 2) includeDirs.emplace_back(arg.substr(2));
        else if (arg == "-o") output = value();
        else if (arg == "--depfile") depfile = value();
        else if (arg == "--emit-glsl") emitGlslDir = value();
        else if (arg == "-h" || arg == "--help") { printUsage(); return 0; }
        else if (arg.starts_with("-")) { std::cerr << "matcomp: unknown option " << arg << "\n"; printUsage(); return 2; }
        else if (input.empty()) input = arg;
        else { std::cerr << "matcomp: more than one input file\n"; return 2; }
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

    MaterialDefinition def;
    try {
        def = parseMaterialDefinition(source);
    } catch (const DefinitionError& e) {
        std::cerr << sourceName << ":" << e.line << ": error: " << e.what() << "\n";
        return 1;
    }

    MaterialPackage& pkg = def.package;
    if (pkg.name.empty()) pkg.name = input.stem().string();

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
