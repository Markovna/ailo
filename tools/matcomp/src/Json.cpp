#include "Json.h"

#include <charconv>

namespace matcomp::json {

const char* Value::typeName() const {
    switch (data.index()) {
        case 0: return "null";
        case 1: return "boolean";
        case 2: return "number";
        case 3: return "string";
        case 4: return "array";
        default: return "object";
    }
}

namespace {

class Parser {
public:
    Parser(std::string_view text, int firstLine) : m_text(text), m_line(firstLine) {}

    Value parseDocument() {
        Value v = parseValue();
        skipWhitespace();
        if (m_pos != m_text.size()) fail("unexpected trailing characters");
        return v;
    }

private:
    [[noreturn]] void fail(const std::string& message) const { throw ParseError(m_line, message); }

    char peek() const { return m_pos < m_text.size() ? m_text[m_pos] : '\0'; }

    char next() {
        char c = m_text[m_pos++];
        if (c == '\n') m_line++;
        return c;
    }

    void skipWhitespace() {
        while (m_pos < m_text.size()) {
            char c = peek();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                next();
            } else if (c == '/' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '/') {
                while (m_pos < m_text.size() && peek() != '\n') next();
            } else if (c == '/' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '*') {
                next(); next();
                while (m_pos + 1 < m_text.size() && !(peek() == '*' && m_text[m_pos + 1] == '/')) next();
                if (m_pos + 1 >= m_text.size()) fail("unterminated comment");
                next(); next();
            } else {
                break;
            }
        }
    }

    void expect(char c) {
        skipWhitespace();
        if (peek() != c) fail(std::string("expected '") + c + "'");
        next();
    }

    Value parseValue() {
        skipWhitespace();
        Value v;
        v.line = m_line;
        char c = peek();
        if (c == '{') v.data = parseObject();
        else if (c == '[') v.data = parseArray();
        else if (c == '"') v.data = parseString();
        else if (c == '-' || (c >= '0' && c <= '9')) v.data = parseNumber();
        else if (consumeWord("true")) v.data = true;
        else if (consumeWord("false")) v.data = false;
        else if (consumeWord("null")) v.data = nullptr;
        else if (c == '\0') fail("unexpected end of input");
        else fail(std::string("unexpected character '") + c + "' (keys and strings must be quoted)");
        return v;
    }

    bool consumeWord(std::string_view word) {
        if (m_text.substr(m_pos, word.size()) != word) return false;
        m_pos += word.size();
        return true;
    }

    Object parseObject() {
        Object object;
        next();
        skipWhitespace();
        if (peek() == '}') { next(); return object; }
        while (true) {
            skipWhitespace();
            if (peek() != '"') fail("expected a quoted key");
            std::string key = parseString();
            for (const auto& [k, _] : object) {
                if (k == key) fail("duplicate key \"" + key + "\"");
            }
            expect(':');
            object.emplace_back(std::move(key), parseValue());
            skipWhitespace();
            if (peek() == ',') { next(); continue; }
            if (peek() == '}') { next(); return object; }
            fail("expected ',' or '}'");
        }
    }

    Array parseArray() {
        Array array;
        next();
        skipWhitespace();
        if (peek() == ']') { next(); return array; }
        while (true) {
            array.push_back(parseValue());
            skipWhitespace();
            if (peek() == ',') { next(); continue; }
            if (peek() == ']') { next(); return array; }
            fail("expected ',' or ']'");
        }
    }

    std::string parseString() {
        std::string s;
        next();
        while (true) {
            if (m_pos >= m_text.size()) fail("unterminated string");
            char c = next();
            if (c == '"') return s;
            if (c == '\n') fail("newline in string");
            if (c != '\\') { s += c; continue; }
            if (m_pos >= m_text.size()) fail("unterminated string");
            switch (char e = next()) {
                case '"': s += '"'; break;
                case '\\': s += '\\'; break;
                case '/': s += '/'; break;
                case 'n': s += '\n'; break;
                case 't': s += '\t'; break;
                case 'r': s += '\r'; break;
                case 'b': s += '\b'; break;
                case 'f': s += '\f'; break;
                default: fail(std::string("unsupported escape '\\") + e + "'");
            }
        }
    }

    double parseNumber() {
        size_t start = m_pos;
        if (peek() == '-') m_pos++;
        while (m_pos < m_text.size()) {
            char c = peek();
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') m_pos++;
            else break;
        }
        double value = 0.0;
        auto [end, ec] = std::from_chars(m_text.data() + start, m_text.data() + m_pos, value);
        if (ec != std::errc() || end != m_text.data() + m_pos) fail("invalid number");
        return value;
    }

    std::string_view m_text;
    size_t m_pos = 0;
    int m_line;
};

}

Value parse(std::string_view text, int firstLine) {
    return Parser(text, firstLine).parseDocument();
}

}
