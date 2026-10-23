#include "TextureDefinition.h"

#include <cmath>

#include "Json.h"

using namespace ailo::texture;
using ailo::TextureFormat;
using ailo::TextureType;
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

uint32_t getSize(const json::Value& v) {
    expect(v, v.isNumber(), "a number", "size");
    const double n = v.asNumber();
    if (n < 1 || n > 65536 || n != std::floor(n)) {
        fail(v.line, "size must be an integer in [1, 65536], got " + std::to_string(n));
    }
    return uint32_t(n);
}

MipmapMode getMipmaps(const json::Value& v) {
    if (v.isBool()) return v.asBool() ? MipmapMode::Runtime : MipmapMode::None;
    expect(v, v.isString(), "a boolean or \"offline\"", "mipmaps");
    if (v.asString() == "offline") return MipmapMode::Offline;
    fail(v.line, "unknown mipmaps \"" + v.asString() + "\" (expected true, false or \"offline\")");
}

template<typename T>
T getName(const json::Value& v, const std::string& what, std::optional<T> (*parse)(std::string_view), const char* expected) {
    const auto& s = getString(v, what);
    if (auto value = parse(s)) return *value;
    fail(v.line, "unknown " + what + " \"" + s + "\" (expected " + expected + ")");
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
    const json::Value* layoutValue = nullptr;
    const json::Value* sizeValue = nullptr;

    for (const auto& [key, v] : root.asObject()) {
        if (key == "source") sourceValue = &v;
        else if (key == "faces") facesValue = &v;
        else if (key == "type") def.type = getType(v);
        else if (key == "srgb") def.srgb = getBool(v, "srgb");
        else if (key == "usage") def.usage = getName(v, "usage", parseUsage, "color, normal or hdr");
        else if (key == "quality") def.quality = getName(v, "quality", parseQuality, "low, normal or high");
        else if (key == "alpha") def.alpha = getBool(v, "alpha");
        else if (key == "format") {
            def.format = getName(v, "format", parseFormat, "rgba8, rgba16f, rgba32f, bc1, bc3, bc4, bc5, bc6h or bc7");
        }
        else if (key == "mipmaps") def.options.mipmaps = getMipmaps(v);
        else if (key == "layout") layoutValue = &v;
        else if (key == "size") sizeValue = &v;
        else fail(v.line, "unknown texture property \"" + key + "\" (expected source, faces, type, layout, size, usage, quality, alpha, format, srgb, mipmaps)");
    }

    if (sourceValue && facesValue) fail(facesValue->line, "\"source\" and \"faces\" are mutually exclusive");

    if (layoutValue) {
        const auto& layout = getString(*layoutValue, "layout");
        if (layout == "equirectangular") def.equirectangular = true;
        else if (layout != "faces") fail(layoutValue->line, "unknown layout \"" + layout + "\" (expected faces or equirectangular)");
        if (def.type != TextureType::Cubemap) fail(layoutValue->line, "\"layout\" requires \"type\": \"cubemap\"");
        if (def.equirectangular && facesValue) fail(facesValue->line, "an equirectangular cubemap has one \"source\", not \"faces\"");
    }
    if (sizeValue) {
        if (!def.equirectangular) fail(sizeValue->line, "\"size\" requires \"layout\": \"equirectangular\"");
        def.faceSize = getSize(*sizeValue);
    }

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
        if (def.type == TextureType::Cubemap && !def.equirectangular) {
            auto faces = cubemapFacePaths(path);
            def.sources.assign(faces.begin(), faces.end());
        } else {
            def.sources.push_back(path);
        }
    } else {
        fail(root.line, def.type == TextureType::Cubemap ? "missing \"source\" or \"faces\"" : "missing \"source\"");
    }

    // Known before decoding, for the packer: an HDR source ignores it, and an unset usage only becomes hdr for those.
    def.options.srgb = def.usage.value_or(Usage::Color) == Usage::Color && def.srgb.value_or(true);
    return def;
}

void resolveSettings(TextureDefinition& def, bool hdrSource) {
    const Usage usage = def.usage.value_or(hdrSource ? Usage::Hdr : Usage::Color);
    def.usage = usage;
    const std::string name(toString(usage));

    if (usage == Usage::Hdr && !hdrSource) fail(0, "\"usage\": \"hdr\" needs an HDR source (.hdr)");
    if (usage != Usage::Hdr && hdrSource) fail(0, "an HDR source needs \"usage\": \"hdr\", got \"" + name + "\"");
    if (usage != Usage::Color && def.srgb) fail(0, "\"srgb\" is for color textures, this one is " + name);
    if (usage != Usage::Color && def.alpha) fail(0, "\"alpha\" is for color textures, this one is " + name);

    const bool srgb = usage == Usage::Color && def.options.srgb;

    if (def.format) {
        const bool hdrFormat = *def.format == TextureFormat::BC6H || *def.format == TextureFormat::RGBA16F ||
                               *def.format == TextureFormat::RGBA32F;
        const std::string format(toString(*def.format));
        if (hdrFormat != (usage == Usage::Hdr)) {
            fail(0, "\"format\": \"" + format + "\" cannot store " + name + " textures");
        }
        if (srgb) {
            auto srgbFormat = withSrgb(*def.format);
            if (!srgbFormat) fail(0, "\"format\": \"" + format + "\" has no sRGB variant, set \"srgb\": false");
            def.format = srgbFormat;
        }
    }
}

}
