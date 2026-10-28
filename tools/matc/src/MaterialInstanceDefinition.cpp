#include "MaterialInstanceDefinition.h"

#include <algorithm>

#include "Json.h"

using namespace ailo::material;
namespace json = ailo::json;

namespace matc {

namespace {

[[noreturn]] void fail(int line, const std::string& message) {
    throw DefinitionError(line, message);
}

const json::Object& getObject(const json::Value& v, std::string_view what) {
    if (!v.isObject()) fail(v.line, std::string(what) + " must be an object, got " + v.typeName());
    return v.asObject();
}

const std::string& getNonEmptyString(const json::Value& v, const std::string& what) {
    if (!v.isString()) fail(v.line, what + " must be a string, got " + v.typeName());
    if (v.asString().empty()) fail(v.line, what + " must not be empty");
    return v.asString();
}

std::string listParameters(const MaterialPackage& pkg, bool samplers) {
    std::string names;
    for (const auto& p : pkg.parameters) {
        if (isSampler(p.type) != samplers) continue;
        names += (names.empty() ? "" : ", ") + p.name;
    }
    return names.empty() ? "none" : names;
}

}

MaterialInstanceDefinition parseMaterialInstanceDefinition(std::string_view source, const MaterialResolver& resolveMaterial) {
    json::Value root;
    try {
        root = json::parse(source);
    } catch (const json::ParseError& e) {
        fail(e.line, e.what());
    }

    const json::Value* material = nullptr;
    const json::Value* parameters = nullptr;
    const json::Value* textures = nullptr;
    for (const auto& [key, v] : getObject(root, "a material instance")) {
        if (key == "material") material = &v;
        else if (key == "parameters") parameters = &v;
        else if (key == "textures") textures = &v;
        else fail(v.line, "unknown material instance property \"" + key + "\" (expected material, parameters, textures)");
    }
    if (!material) fail(root.line, "missing \"material\"");

    MaterialInstanceDefinition def;
    MaterialInstancePackage& pkg = def.package;
    pkg.material = getNonEmptyString(*material, "material");
    def.materialLine = material->line;

    const MaterialPackage materialPkg = resolveMaterial(pkg.material, material->line).package;

    if (parameters) {
        for (const auto& [name, v] : getObject(*parameters, "parameters")) {
            const MaterialParameter* p = materialPkg.findParameter(name);
            if (!p || isSampler(p->type)) {
                fail(v.line, "material \"" + materialPkg.name + "\" has no parameter \"" + name + "\" (it has: " +
                             listParameters(materialPkg, false) + ")");
            }
            if (std::ranges::contains(pkg.parameters, name, &InstanceParameter::name)) {
                fail(v.line, "parameter \"" + name + "\" is listed twice");
            }
            pkg.parameters.push_back({ name, p->type, parseParameterValue(*p, v, "\"" + name + "\"") });
        }
    }

    if (textures) {
        for (const auto& [name, v] : getObject(*textures, "textures")) {
            const MaterialParameter* p = materialPkg.findParameter(name);
            if (!p || !isSampler(p->type)) {
                fail(v.line, "material \"" + materialPkg.name + "\" has no sampler \"" + name + "\" (it has: " +
                             listParameters(materialPkg, true) + ")");
            }
            if (std::ranges::contains(pkg.textures, name, &InstanceTexture::name)) {
                fail(v.line, "texture \"" + name + "\" is listed twice");
            }
            pkg.textures.push_back({ name, getNonEmptyString(v, "texture \"" + name + "\"") });
            def.textureLines.push_back(v.line);
        }
    }

    return def;
}

}
