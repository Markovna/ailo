#include "LightDefinition.h"

#include <cmath>

#include "Json.h"

namespace json = ailo::json;
namespace fs = std::filesystem;

namespace lightc {

namespace {

[[noreturn]] void fail(int line, const std::string& message) {
    throw DefinitionError(line, message);
}

void expect(const json::Value& v, bool ok, const char* expected, const std::string& what) {
    if (!ok) fail(v.line, what + " must be " + expected + ", got " + v.typeName());
}

fs::path getPath(const json::Value& v, const std::string& what) {
    expect(v, v.isString(), "a string", what);
    if (v.asString().empty()) fail(v.line, what + " is empty");
    return fs::path(v.asString());
}

uint32_t getCount(const json::Value& v, const std::string& what) {
    expect(v, v.isNumber(), "a number", what);
    const double n = v.asNumber();
    if (n < 1 || n > 65536 || n != std::floor(n)) {
        fail(v.line, what + " must be an integer in [1, 65536], got " + std::to_string(n));
    }
    return uint32_t(n);
}

}

LightDefinition parseLightDefinition(std::string_view source) {
    json::Value root;
    try {
        root = json::parse(source);
    } catch (const json::ParseError& e) {
        fail(e.line, e.what());
    }
    expect(root, root.isObject(), "an object", "a light definition");

    LightDefinition def;
    bool hasSource = false;

    for (const auto& [key, v] : root.asObject()) {
        if (key == "source") {
            def.source = getPath(v, "source");
            hasSource = true;
        } else if (key == "size") {
            def.prefilter.size = getCount(v, "size");
            if ((def.prefilter.size & (def.prefilter.size - 1)) != 0) {
                fail(v.line, "size must be a power of two, got " + std::to_string(def.prefilter.size));
            }
        } else if (key == "samples") {
            def.prefilter.samples = getCount(v, "samples");
        } else {
            fail(v.line, "unknown light property \"" + key + "\" (expected source, size, samples)");
        }
    }

    if (!hasSource) fail(root.line, "missing \"source\"");
    return def;
}

}
