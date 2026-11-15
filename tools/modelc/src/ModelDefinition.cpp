#include "ModelDefinition.h"

#include "Json.h"

namespace json = ailo::json;

namespace modelc {

namespace {

[[noreturn]] void fail(int line, const std::string& message) {
    throw DefinitionError(line, message);
}

void expect(const json::Value& v, bool ok, const char* expected, const std::string& what) {
    if (!ok) fail(v.line, what + " must be " + expected + ", got " + v.typeName());
}

const std::string& getNonEmptyString(const json::Value& v, const std::string& what) {
    expect(v, v.isString(), "a string", what);
    if (v.asString().empty()) fail(v.line, what + " is empty");
    return v.asString();
}

float getNumber(const json::Value& v, const std::string& what, float min, bool inclusive) {
    expect(v, v.isNumber(), "a number", what);
    const auto value = float(v.asNumber());
    if (inclusive ? !(value >= min) : !(value > min)) {
        fail(v.line, what + " must be " + (inclusive ? "at least " : "greater than ") + std::to_string(min));
    }
    return value;
}

FootMarkerSettings parseFootMarkers(const json::Value& root) {
    expect(root, root.isObject(), "an object", "footMarkers");
    FootMarkerSettings settings;
    bool hasFeet = false, hasThreshold = false;

    for (const auto& [key, v] : root.asObject()) {
        if (key == "feet") {
            expect(v, v.isObject(), "an object", "footMarkers.feet");
            for (const auto& [marker, bone] : v.asObject()) {
                if (marker.empty()) fail(bone.line, "a foot marker name is empty");
                settings.feet.emplace(marker, getNonEmptyString(bone, "bone of foot marker \"" + marker + "\""));
            }
            if (settings.feet.empty()) fail(v.line, "footMarkers.feet is empty");
            hasFeet = true;
        } else if (key == "clips") {
            expect(v, v.isArray(), "an array", "footMarkers.clips");
            for (const auto& clip : v.asArray()) {
                settings.clips.push_back(getNonEmptyString(clip, "a footMarkers clip"));
            }
        } else if (key == "groundThreshold") {
            settings.groundThreshold = getNumber(v, "footMarkers.groundThreshold", 0.0f, true);
            hasThreshold = true;
        } else if (key == "speedThreshold") {
            settings.speedThreshold = getNumber(v, "footMarkers.speedThreshold", 0.0f, true);
        } else if (key == "sampleRate") {
            settings.sampleRate = getNumber(v, "footMarkers.sampleRate", 0.0f, false);
        } else if (key == "up") {
            expect(v, v.isArray() && v.asArray().size() == 3, "an array of 3 numbers", "footMarkers.up");
            glm::vec3 up;
            for (int i = 0; i < 3; i++) {
                const auto& c = v.asArray()[i];
                expect(c, c.isNumber(), "a number", "a footMarkers.up component");
                up[i] = float(c.asNumber());
            }
            if (glm::length(up) == 0.0f) fail(v.line, "footMarkers.up is zero");
            settings.up = glm::normalize(up);
        } else {
            fail(v.line, "unknown footMarkers property \"" + key +
                         "\" (expected feet, clips, groundThreshold, speedThreshold, sampleRate, up)");
        }
    }

    if (!hasFeet) fail(root.line, "footMarkers is missing \"feet\"");
    if (!hasThreshold) fail(root.line, "footMarkers is missing \"groundThreshold\"");
    return settings;
}

}

ModelDefinition parseModelDefinition(std::string_view source) {
    json::Value root;
    try {
        root = json::parse(source);
    } catch (const json::ParseError& e) {
        fail(e.line, e.what());
    }
    expect(root, root.isObject(), "an object", "a model definition");

    ModelDefinition def;
    bool hasSource = false, hasMaterial = false;

    for (const auto& [key, v] : root.asObject()) {
        if (key == "source") {
            def.source = getNonEmptyString(v, "source");
            hasSource = true;
        } else if (key == "material") {
            def.material = getNonEmptyString(v, "material");
            hasMaterial = true;
        } else if (key == "materials") {
            expect(v, v.isObject(), "an object", "materials");
            for (const auto& [name, material] : v.asObject()) {
                if (!def.materials.emplace(name, getNonEmptyString(material, "material of \"" + name + "\"")).second) {
                    fail(material.line, "material \"" + name + "\" is listed twice");
                }
            }
        } else if (key == "nodeMaterials") {
            expect(v, v.isObject(), "an object", "nodeMaterials");
            for (const auto& [name, material] : v.asObject()) {
                if (!def.nodeMaterials.emplace(name, getNonEmptyString(material, "material of node \"" + name + "\"")).second) {
                    fail(material.line, "node \"" + name + "\" is listed twice");
                }
            }
        } else if (key == "textures") {
            expect(v, v.isObject(), "an object", "textures");
            for (const auto& [uri, path] : v.asObject()) {
                if (!def.textures.emplace(uri, getNonEmptyString(path, "texture \"" + uri + "\"")).second) {
                    fail(path.line, "texture \"" + uri + "\" is listed twice");
                }
            }
        } else if (key == "animations") {
            expect(v, v.isObject(), "an object", "animations");
            for (const auto& [name, path] : v.asObject()) {
                if (name.empty()) fail(path.line, "an animation name is empty");
                if (!def.animations.emplace(name, getNonEmptyString(path, "animation \"" + name + "\"")).second) {
                    fail(path.line, "animation \"" + name + "\" is listed twice");
                }
            }
        } else if (key == "optimize") {
            expect(v, v.isBool(), "a boolean", "optimize");
            def.optimize = v.asBool();
        } else if (key == "footMarkers") {
            def.footMarkers = parseFootMarkers(v);
        } else {
            fail(v.line, "unknown model property \"" + key + "\" (expected source, material, materials, nodeMaterials, textures, animations, optimize, footMarkers)");
        }
    }

    if (!hasSource) fail(root.line, "missing \"source\"");
    if (!hasMaterial) fail(root.line, "missing \"material\"");
    return def;
}

}
