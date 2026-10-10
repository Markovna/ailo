#include "TextureDefinition.h"

#include "Json.h"

using namespace ailo::texture;
namespace json = ailo::json;
namespace fs = std::filesystem;

namespace texc {

namespace {

[[noreturn]] void fail(int line, const std::string& message) {
    throw DefinitionError(line, message);
}

void expect(const json::Value& v, bool ok, const char* expected, const std::string& what) {
    if (!ok) fail(v.line, what + " must be " + expected + ", got " + v.typeName());
}

const std::string& getString(const json::Value& v, const std::string& what) {
    expect(v, v.isString(), "a string", what);
    return v.asString();
}

bool getBool(const json::Value& v, const std::string& what) {
    expect(v, v.isBool(), "a boolean", what);
    return v.asBool();
}

fs::path getPath(const json::Value& v, const std::string& what) {
    const auto& s = getString(v, what);
    if (s.empty()) fail(v.line, what + " is empty");
    return fs::path(s);
}

TextureType getType(const json::Value& v) {
    const auto& s = getString(v, "type");
    if (s == "2d") return TextureType::Texture2D;
    if (s == "cubemap") return TextureType::Cubemap;
    fail(v.line, "unknown type \"" + s + "\" (expected 2d or cubemap)");
}

}

TextureDefinition parseTextureDefinition(std::string_view source) {
    json::Value root;
    try {
        root = json::parse(source);
    } catch (const json::ParseError& e) {
        fail(e.line, e.what());
    }
    expect(root, root.isObject(), "an object", "a texture definition");

    TextureDefinition def;
    const json::Value* sourceValue = nullptr;
    const json::Value* facesValue = nullptr;

    for (const auto& [key, v] : root.asObject()) {
        if (key == "source") sourceValue = &v;
        else if (key == "faces") facesValue = &v;
        else if (key == "type") def.type = getType(v);
        else if (key == "srgb") def.options.srgb = getBool(v, "srgb");
        else if (key == "mipmaps") def.options.mipmaps = getBool(v, "mipmaps");
        else fail(v.line, "unknown texture property \"" + key + "\" (expected source, faces, type, srgb, mipmaps)");
    }

    if (sourceValue && facesValue) fail(facesValue->line, "\"source\" and \"faces\" are mutually exclusive");

    if (facesValue) {
        if (def.type != TextureType::Cubemap) fail(facesValue->line, "\"faces\" requires \"type\": \"cubemap\"");
        expect(*facesValue, facesValue->isArray(), "an array", "faces");
        const auto& faces = facesValue->asArray();
        if (faces.size() != 6) {
            fail(facesValue->line, "a cubemap has 6 faces (+x -x +y -y +z -z), got " + std::to_string(faces.size()));
        }
        for (const auto& face : faces) def.sources.push_back(getPath(face, "cubemap face"));
    } else if (sourceValue) {
        fs::path path = getPath(*sourceValue, "source");
        if (def.type == TextureType::Cubemap) {
            auto faces = cubemapFacePaths(path);
            def.sources.assign(faces.begin(), faces.end());
        } else {
            def.sources.push_back(path);
        }
    } else {
        fail(root.line, def.type == TextureType::Cubemap ? "missing \"source\" or \"faces\"" : "missing \"source\"");
    }

    return def;
}

}
