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
        } else {
            fail(v.line, "unknown model property \"" + key + "\" (expected source, material, materials)");
        }
    }

    if (!hasSource) fail(root.line, "missing \"source\"");
    if (!hasMaterial) fail(root.line, "missing \"material\"");
    return def;
}

}
