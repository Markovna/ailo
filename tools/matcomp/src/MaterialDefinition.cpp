#include "MaterialDefinition.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <regex>
#include <span>

#include "Json.h"

using namespace ailo::material;
namespace json = ailo::json;

namespace matcomp {

namespace {

[[noreturn]] void fail(int line, const std::string& message) {
    throw DefinitionError(line, message);
}

struct Block {
    std::string name;
    std::string_view body;
    int nameLine = 0;
    int bodyLine = 0;
};

class BlockScanner {
public:
    explicit BlockScanner(std::string_view text) : m_text(text) {}

    std::vector<Block> scan() {
        std::vector<Block> blocks;
        while (true) {
            skipWhitespaceAndComments();
            if (m_pos >= m_text.size()) return blocks;

            Block block;
            block.nameLine = m_line;
            block.name = readIdentifier();
            if (block.name.empty()) fail(m_line, "expected a block name (material, vertex or fragment)");

            skipWhitespaceAndComments();
            if (peek() != '{') fail(m_line, "expected '{' after \"" + block.name + "\"");
            size_t open = m_pos;
            advance();
            block.bodyLine = m_line;
            size_t close = findMatchingBrace(block.nameLine);
            block.body = m_text.substr(open + 1, close - open - 1);
            blocks.push_back(block);
        }
    }

private:
    char peek(size_t offset = 0) const { return m_pos + offset < m_text.size() ? m_text[m_pos + offset] : '\0'; }

    void advance() {
        if (m_text[m_pos] == '\n') m_line++;
        m_pos++;
    }

    void skipComment() {
        if (peek() == '/' && peek(1) == '/') {
            while (m_pos < m_text.size() && peek() != '\n') advance();
        } else {
            int startLine = m_line;
            advance(); advance();
            while (m_pos < m_text.size() && !(peek() == '*' && peek(1) == '/')) advance();
            if (m_pos >= m_text.size()) fail(startLine, "unterminated comment");
            advance(); advance();
        }
    }

    bool atComment() const { return peek() == '/' && (peek(1) == '/' || peek(1) == '*'); }

    void skipWhitespaceAndComments() {
        while (m_pos < m_text.size()) {
            if (std::isspace(static_cast<unsigned char>(peek()))) advance();
            else if (atComment()) skipComment();
            else break;
        }
    }

    std::string readIdentifier() {
        size_t start = m_pos;
        while (m_pos < m_text.size() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) advance();
        return std::string(m_text.substr(start, m_pos - start));
    }

    // Skips braces inside comments and strings.
    size_t findMatchingBrace(int blockLine) {
        int depth = 1;
        while (m_pos < m_text.size()) {
            char c = peek();
            if (atComment()) {
                skipComment();
            } else if (c == '"') {
                advance();
                while (m_pos < m_text.size() && peek() != '"' && peek() != '\n') {
                    if (peek() == '\\') advance();
                    advance();
                }
                if (m_pos < m_text.size()) advance();
            } else {
                if (c == '{') depth++;
                if (c == '}' && --depth == 0) {
                    size_t close = m_pos;
                    advance();
                    return close;
                }
                advance();
            }
        }
        fail(blockLine, "block is not closed");
    }

    std::string_view m_text;
    size_t m_pos = 0;
    int m_line = 1;
};

const std::regex kIdentifier("[A-Za-z_][A-Za-z0-9_]*");

bool isIdentifier(const std::string& s) {
    return std::regex_match(s, kIdentifier) && !s.starts_with("gl_");
}

const std::string& getString(const json::Value& v, std::string_view what) {
    if (!v.isString()) fail(v.line, std::string(what) + " must be a string, got " + v.typeName());
    return v.asString();
}

bool getBool(const json::Value& v, std::string_view what) {
    if (!v.isBool()) fail(v.line, std::string(what) + " must be true or false, got " + v.typeName());
    return v.asBool();
}

const json::Array& getArray(const json::Value& v, std::string_view what) {
    if (!v.isArray()) fail(v.line, std::string(what) + " must be an array, got " + v.typeName());
    return v.asArray();
}

template<typename E, size_t N>
E getEnum(const json::Value& v, std::string_view what, const std::pair<std::string_view, E> (&values)[N]) {
    const auto& s = getString(v, what);
    for (const auto& [name, value] : values) {
        if (s == name) return value;
    }
    std::string expected;
    for (const auto& [name, _] : values) {
        expected += expected.empty() ? "" : ", ";
        expected += name;
    }
    fail(v.line, "invalid " + std::string(what) + " \"" + s + "\" (expected one of: " + expected + ")");
}

constexpr std::pair<std::string_view, ParameterType> kParameterTypes[] = {
    { "bool", ParameterType::Bool },     { "bool2", ParameterType::Bool2 },
    { "bool3", ParameterType::Bool3 },   { "bool4", ParameterType::Bool4 },
    { "float", ParameterType::Float },   { "float2", ParameterType::Float2 },
    { "float3", ParameterType::Float3 }, { "float4", ParameterType::Float4 },
    { "int", ParameterType::Int },       { "int2", ParameterType::Int2 },
    { "int3", ParameterType::Int3 },     { "int4", ParameterType::Int4 },
    { "uint", ParameterType::UInt },     { "uint2", ParameterType::UInt2 },
    { "uint3", ParameterType::UInt3 },   { "uint4", ParameterType::UInt4 },
    { "float3x3", ParameterType::Float3x3 }, { "float4x4", ParameterType::Float4x4 },
    { "sampler2d", ParameterType::Sampler2D },
    { "samplerCubemap", ParameterType::SamplerCubemap },
};

constexpr std::pair<std::string_view, SamplerDefault> kSamplerDefaults[] = {
    { "white", SamplerDefault::White },
    { "black", SamplerDefault::Black },
    { "normal", SamplerDefault::Normal },
};

bool isMatrix(ParameterType t) { return t == ParameterType::Float3x3 || t == ParameterType::Float4x4; }

bool isBoolType(ParameterType t) {
    return t == ParameterType::Bool || t == ParameterType::Bool2 || t == ParameterType::Bool3 || t == ParameterType::Bool4;
}

bool isFloatType(ParameterType t) {
    return t == ParameterType::Float || t == ParameterType::Float2 || t == ParameterType::Float3 ||
           t == ParameterType::Float4 || isMatrix(t);
}

bool isUnsignedType(ParameterType t) {
    return t == ParameterType::UInt || t == ParameterType::UInt2 || t == ParameterType::UInt3 || t == ParameterType::UInt4;
}

// Components per column for matrices, per vector otherwise.
uint32_t rowCount(ParameterType t) {
    if (t == ParameterType::Float3x3) return 3;
    if (t == ParameterType::Float4x4) return 4;
    return componentCount(t);
}

uint32_t columnCount(ParameterType t) {
    if (t == ParameterType::Float3x3) return 3;
    if (t == ParameterType::Float4x4) return 4;
    return 1;
}

uint32_t std140Size(ParameterType t) {
    if (isMatrix(t)) return 16 * columnCount(t);
    return 4 * componentCount(t);
}

uint32_t std140Alignment(ParameterType t) {
    if (isMatrix(t)) return 16;
    switch (componentCount(t)) {
        case 1: return 4;
        case 2: return 8;
        default: return 16;
    }
}

uint32_t alignUp(uint32_t v, uint32_t a) { return (v + a - 1) / a * a; }

void writeDefault(MaterialParameter& p, const json::Value& value) {
    uint32_t elements = std::max(p.arraySize, 1u);
    uint32_t components = componentCount(p.type);

    std::vector<const json::Value*> flat;
    if (value.isArray()) {
        for (const auto& v : value.asArray()) flat.push_back(&v);
    } else {
        flat.push_back(&value);
    }
    if (flat.size() != size_t(elements) * components) {
        fail(value.line, "default for \"" + p.name + "\" needs " + std::to_string(elements * components) +
                         " value(s), got " + std::to_string(flat.size()));
    }

    uint32_t elementStride = p.arraySize ? alignUp(std140Size(p.type), 16) : std140Size(p.type);
    uint32_t rows = rowCount(p.type);

    for (uint32_t e = 0; e < elements; e++) {
        for (uint32_t i = 0; i < components; i++) {
            const json::Value& v = *flat[e * components + i];
            uint32_t column = i / rows;
            uint32_t row = i % rows;
            uint32_t byteOffset = e * elementStride + column * 16 + row * 4;

            uint32_t bits = 0;
            if (isBoolType(p.type)) {
                bits = getBool(v, "default") ? 1u : 0u;
            } else if (!v.isNumber()) {
                fail(v.line, "default for \"" + p.name + "\" must be numeric, got " + v.typeName());
            } else if (isFloatType(p.type)) {
                bits = std::bit_cast<uint32_t>(float(v.asNumber()));
            } else {
                double n = v.asNumber();
                if (n != std::floor(n)) fail(v.line, "default for \"" + p.name + "\" must be an integer");
                if (isUnsignedType(p.type) && n < 0) fail(v.line, "default for \"" + p.name + "\" must not be negative");
                bits = isUnsignedType(p.type) ? uint32_t(n) : std::bit_cast<uint32_t>(int32_t(n));
            }
            std::memcpy(p.defaultValue.data() + byteOffset, &bits, sizeof(bits));
        }
    }
}

void parseParameters(const json::Value& value, MaterialPackage& pkg) {
    uint32_t uniformOffset = 0;
    uint32_t nextBinding = kFirstSamplerBinding;

    for (const auto& entry : getArray(value, "parameters")) {
        if (!entry.isObject()) fail(entry.line, "each parameter must be an object");

        MaterialParameter p;
        const json::Value* type = nullptr;
        const json::Value* defaultValue = nullptr;
        int nameLine = entry.line;

        for (const auto& [key, v] : entry.asObject()) {
            if (key == "name") { p.name = getString(v, "parameter name"); nameLine = v.line; }
            else if (key == "type") type = &v;
            else if (key == "default") defaultValue = &v;
            else fail(v.line, "unknown parameter property \"" + key + "\" (expected name, type, default)");
        }

        if (p.name.empty()) fail(entry.line, "parameter is missing \"name\"");
        if (!isIdentifier(p.name)) fail(nameLine, "parameter name \"" + p.name + "\" is not a valid GLSL identifier");
        if (pkg.findParameter(p.name)) fail(nameLine, "duplicate parameter \"" + p.name + "\"");
        if (!type) fail(entry.line, "parameter \"" + p.name + "\" is missing \"type\"");

        std::string typeName = getString(*type, "parameter type");
        if (auto bracket = typeName.find('['); bracket != std::string::npos) {
            if (typeName.back() != ']') fail(type->line, "invalid array type \"" + typeName + "\"");
            std::string count = typeName.substr(bracket + 1, typeName.size() - bracket - 2);
            if (count.empty() || !std::ranges::all_of(count, ::isdigit) || std::stoul(count) == 0) {
                fail(type->line, "invalid array size in \"" + typeName + "\"");
            }
            p.arraySize = uint32_t(std::stoul(count));
            typeName = typeName.substr(0, bracket);
        }
        json::Value typeValue = *type;
        typeValue.data = typeName;
        p.type = getEnum(typeValue, "parameter type", kParameterTypes);

        if (isSampler(p.type)) {
            if (p.arraySize) fail(type->line, "sampler arrays are not supported");
            p.binding = nextBinding++;
            p.samplerDefault = p.type == ParameterType::SamplerCubemap ? SamplerDefault::Black : SamplerDefault::White;
            if (defaultValue) {
                p.samplerDefault = getEnum(*defaultValue, "sampler default", kSamplerDefaults);
                if (p.samplerDefault == SamplerDefault::Normal && p.type != ParameterType::Sampler2D) {
                    fail(defaultValue->line, "\"normal\" is only a valid default for sampler2d");
                }
            }
        } else {
            uint32_t alignment = p.arraySize ? 16 : std140Alignment(p.type);
            uint32_t elementSize = p.arraySize ? alignUp(std140Size(p.type), 16) : std140Size(p.type);
            p.offset = alignUp(uniformOffset, alignment);
            p.size = elementSize * std::max(p.arraySize, 1u);
            uniformOffset = p.offset + p.size;
            p.defaultValue.assign(p.size, 0);
            if (defaultValue) writeDefault(p, *defaultValue);
        }

        pkg.parameters.push_back(std::move(p));
    }

    pkg.uniformBlockSize = alignUp(uniformOffset, 16);
}

constexpr std::pair<std::string_view, ShadingModel> kShadingModels[] = {
    { "lit", ShadingModel::Lit },
    { "unlit", ShadingModel::Unlit },
};

constexpr std::pair<std::string_view, BlendingMode> kBlendingModes[] = {
    { "opaque", BlendingMode::Opaque },
};

constexpr std::pair<std::string_view, VertexDomain> kVertexDomains[] = {
    { "object", VertexDomain::Object },
};

constexpr std::pair<std::string_view, CullingMode> kCullingModes[] = {
    { "none", CullingMode::None },
    { "front", CullingMode::Front },
    { "back", CullingMode::Back },
    { "frontAndBack", CullingMode::FrontAndBack },
};

constexpr std::pair<std::string_view, VertexAttribute> kAttributes[] = {
    { "color", VertexAttribute::Color },
    { "uv0", VertexAttribute::UV0 },
    { "tangents", VertexAttribute::Tangents },
    { "normal", VertexAttribute::Normal },
};

constexpr std::pair<std::string_view, DepthFunc> kDepthFuncs[] = {
    { "never", DepthFunc::Never },
    { "less", DepthFunc::Less },
    { "equal", DepthFunc::Equal },
    { "lessEqual", DepthFunc::LessEqual },
    { "greater", DepthFunc::Greater },
    { "notEqual", DepthFunc::NotEqual },
    { "greaterEqual", DepthFunc::GreaterEqual },
    { "always", DepthFunc::Always },
};

constexpr std::pair<std::string_view, Variant::type_t> kVariantFilters[] = {
    { "skinning", Variant::SKINNING },
    { "shadowReceiver", Variant::SHADOWS },
};

void parseMaterialBlock(const Block& block, MaterialDefinition& def) {
    MaterialPackage& pkg = def.package;
    json::Value root;
    try {
        root = json::parse(std::string("{") + std::string(block.body) + "}", block.bodyLine);
    } catch (const json::ParseError& e) {
        fail(e.line, e.what());
    }

    for (const auto& [key, v] : root.asObject()) {
        if (key == "name") pkg.name = getString(v, "name");
        else if (key == "shadingModel") pkg.shadingModel = getEnum(v, "shadingModel", kShadingModels);
        else if (key == "parameters") parseParameters(v, pkg);
        else if (key == "requires") {
            for (const auto& a : getArray(v, "requires")) {
                pkg.requiredAttributes |= uint8_t(getEnum(a, "required attribute", kAttributes));
            }
        }
        else if (key == "variables") {
            const auto& vars = getArray(v, "variables");
            if (vars.size() > kMaxVariables) {
                fail(v.line, "at most " + std::to_string(kMaxVariables) + " variables are supported");
            }
            for (const auto& var : vars) {
                const auto& name = getString(var, "variable name");
                if (!isIdentifier(name)) fail(var.line, "variable name \"" + name + "\" is not a valid GLSL identifier");
                if (std::ranges::contains(pkg.variables, name)) fail(var.line, "duplicate variable \"" + name + "\"");
                pkg.variables.push_back(name);
            }
        }
        else if (key == "blending") pkg.blending = getEnum(v, "blending", kBlendingModes);
        else if (key == "vertexDomain") pkg.vertexDomain = getEnum(v, "vertexDomain", kVertexDomains);
        else if (key == "culling") pkg.raster.culling = getEnum(v, "culling", kCullingModes);
        else if (key == "colorWrite") pkg.raster.colorWrite = getBool(v, "colorWrite");
        else if (key == "depthWrite") pkg.raster.depthWrite = getBool(v, "depthWrite");
        else if (key == "depthCulling") pkg.raster.depthCulling = getBool(v, "depthCulling");
        else if (key == "depthFunc") pkg.raster.depthFunc = getEnum(v, "depthFunc", kDepthFuncs);
        else if (key == "doubleSided") pkg.raster.doubleSided = getBool(v, "doubleSided");
        else if (key == "customSurfaceShading") {
            def.customSurfaceShading = getBool(v, "customSurfaceShading");
            def.customSurfaceShadingLine = v.line;
        }
        else if (key == "variantFilter") {
            for (const auto& f : getArray(v, "variantFilter")) {
                pkg.variantFilter |= getEnum(f, "variantFilter entry", kVariantFilters);
            }
        }
        else fail(v.line, "unknown material property \"" + key + "\"");
    }

    for (const auto& var : pkg.variables) {
        if (pkg.findParameter(var)) fail(block.bodyLine, "\"" + var + "\" is both a parameter and a variable");
    }

    // Lighting variants make no sense for unlit materials.
    if (pkg.shadingModel == ShadingModel::Unlit) {
        pkg.variantFilter |= Variant::SHADOWS;
    }

    if (pkg.shadingModel == ShadingModel::Lit || (pkg.requiredAttributes & uint8_t(VertexAttribute::Tangents))) {
        pkg.requiredAttributes |= uint8_t(VertexAttribute::Normal);
    }
}

}

MaterialDefinition parseMaterialDefinition(std::string_view source) {
    MaterialDefinition def;
    bool hasMaterial = false;

    for (const auto& block : BlockScanner(source).scan()) {
        if (block.name == "material") {
            if (hasMaterial) fail(block.nameLine, "duplicate material block");
            hasMaterial = true;
            parseMaterialBlock(block, def);
        } else if (block.name == "vertex" || block.name == "fragment") {
            CodeBlock& code = block.name == "vertex" ? def.vertex : def.fragment;
            if (code.present) fail(block.nameLine, "duplicate " + block.name + " block");
            code.code = block.body;
            code.line = block.bodyLine;
            code.present = true;
        } else {
            fail(block.nameLine, "unknown block \"" + block.name + "\" (expected material, vertex or fragment)");
        }
    }

    if (!hasMaterial) fail(1, "missing material block");
    if (!def.fragment.present) fail(1, "missing fragment block");

    static const std::regex materialFunction(R"(void\s+material\s*\()");
    static const std::regex prepareCall(R"(prepareMaterial\s*\()");
    static const std::regex materialVertexFunction(R"(void\s+materialVertex\s*\()");
    static const std::regex normalWrite(R"(material\s*\.\s*normal\b)");
    static const std::regex clipPositionWrite(R"(material\s*\.\s*clipPosition\b)");

    if (!std::regex_search(def.fragment.code, materialFunction)) {
        fail(def.fragment.line, "fragment block must define void material(inout MaterialInputs material)");
    }
    if (!std::regex_search(def.fragment.code, prepareCall)) {
        fail(def.fragment.line, "material() must call prepareMaterial(material)");
    }
    if (def.vertex.present && !std::regex_search(def.vertex.code, materialVertexFunction)) {
        fail(def.vertex.line, "vertex block must define void materialVertex(inout MaterialVertexInputs material)");
    }

    if (def.customSurfaceShading) {
        static const std::regex surfaceShadingFunction(R"(vec3\s+surfaceShading\s*\()");
        if (def.package.shadingModel != ShadingModel::Lit) {
            fail(def.customSurfaceShadingLine, "customSurfaceShading requires the lit shading model");
        }
        if (!std::regex_search(def.fragment.code, surfaceShadingFunction)) {
            fail(def.fragment.line, "customSurfaceShading: the fragment block must define vec3 surfaceShading("
                                    "const MaterialInputs materialInputs, const ShadingData shadingData, const LightData lightData)");
        }
    }

    def.hasClipPosition = def.vertex.present && std::regex_search(def.vertex.code, clipPositionWrite);

    def.hasNormal = std::regex_search(def.fragment.code, normalWrite);
    if (def.hasNormal && !(def.package.requiredAttributes & uint8_t(VertexAttribute::Tangents))) {
        fail(def.fragment.line, "writing material.normal requires \"tangents\" in \"requires\"");
    }

    return def;
}

}
