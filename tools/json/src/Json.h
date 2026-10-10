#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ailo::json {

struct Value;

using Array = std::vector<Value>;
using Object = std::vector<std::pair<std::string, Value>>;   // keeps declaration order

struct Value {
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> data;
    int line = 0;

    bool isNull() const { return std::holds_alternative<std::nullptr_t>(data); }
    bool isBool() const { return std::holds_alternative<bool>(data); }
    bool isNumber() const { return std::holds_alternative<double>(data); }
    bool isString() const { return std::holds_alternative<std::string>(data); }
    bool isArray() const { return std::holds_alternative<Array>(data); }
    bool isObject() const { return std::holds_alternative<Object>(data); }

    bool asBool() const { return std::get<bool>(data); }
    double asNumber() const { return std::get<double>(data); }
    const std::string& asString() const { return std::get<std::string>(data); }
    const Array& asArray() const { return std::get<Array>(data); }
    const Object& asObject() const { return std::get<Object>(data); }

    const char* typeName() const;
};

struct ParseError : std::runtime_error {
    int line;
    ParseError(int line, const std::string& message) : std::runtime_error(message), line(line) {}
};

// Parses a single JSON value; firstLine is the line number of text[0] in the source file.
Value parse(std::string_view text, int firstLine = 1);

}
