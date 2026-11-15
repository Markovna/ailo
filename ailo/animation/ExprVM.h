#pragma once

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// A small expression language compiled to stack-machine bytecode.
//
// Syntax
//   numbers      1, 2.5, .5, 1e-3
//   variables    names passed to compile(), [A-Za-z_][A-Za-z0-9_]*
//   arithmetic   + - * / ^      unary - +
//   comparison   > < >= <= == !=        (result 1 or 0)
//   logic        && ||                  (non-zero is true; both sides are always evaluated)
//   functions    sin(x) cos(x) abs(x) max(a, b) iff(cond, a, b)
//   precedence   || < && < == != < > < >= <= < + - < * / < unary - < ^
//
// Bytecode layout (host byte order, 4-byte aligned):
//
//   Header   { uint8 error; uint8 maxStack; uint16 variableCount; uint32 constantCount; uint32 instructionCount; }
//   float    constants[constantCount]
//   uint16   code[instructionCount]          // kind in bits 0-1, data in bits 2-15
//   padding  to a multiple of 4 bytes
//
// or, for a failed compile:
//
//   Header   { error = 1, rest 0 }
//   char     message[]  (zero-terminated), padded to a multiple of 4 bytes

namespace ailo::expr {

enum class Op : uint16_t {
    Add, Sub, Mul, Div, Pow, Neg,
    Sin, Cos, Abs, Max, Iff,
    Gt, Lt, Ge, Le, Eq, Ne, And, Or,
    Count
};

struct OpInfo {
    const char* name;
    uint8_t arity;
    uint8_t precedence;  // higher binds tighter; 0 for functions, whose parentheses group them
    bool rightAssoc;
    bool isFunction;     // called as name(args)
};

inline constexpr OpInfo kOps[] = {
    /* Add */ { "+",   2, 12, false, false },
    /* Sub */ { "-",   2, 12, false, false },
    /* Mul */ { "*",   2, 13, false, false },
    /* Div */ { "/",   2, 13, false, false },
    /* Pow */ { "^",   2, 15, true,  false },
    /* Neg */ { "neg", 1, 14, true,  false },
    /* Sin */ { "sin", 1, 0,  false, true  },
    /* Cos */ { "cos", 1, 0,  false, true  },
    /* Abs */ { "abs", 1, 0,  false, true  },
    /* Max */ { "max", 2, 0,  false, true  },
    /* Iff */ { "iff", 3, 0,  false, true  },
    /* Gt  */ { ">",   2, 10, false, false },
    /* Lt  */ { "<",   2, 10, false, false },
    /* Ge  */ { ">=",  2, 10, false, false },
    /* Le  */ { "<=",  2, 10, false, false },
    /* Eq  */ { "==",  2, 9,  false, false },
    /* Ne  */ { "!=",  2, 9,  false, false },
    /* And */ { "&&",  2, 5,  false, false },
    /* Or  */ { "||",  2, 4,  false, false },
};
static_assert(std::size(kOps) == static_cast<size_t>(Op::Count), "kOps out of sync with Op");

inline constexpr const OpInfo& info(Op op) { return kOps[static_cast<size_t>(op)]; }

// Applies `op` to the value stack. `sp` points one past the top element; returns the new `sp`.
// Shared by the interpreter and the constant folder so both produce bit-identical results.
inline float* apply(Op op, float* sp) noexcept {
    switch (op) {
    case Op::Add: sp[-2] = sp[-2] + sp[-1]; return sp - 1;
    case Op::Sub: sp[-2] = sp[-2] - sp[-1]; return sp - 1;
    case Op::Mul: sp[-2] = sp[-2] * sp[-1]; return sp - 1;
    case Op::Div: sp[-2] = sp[-2] / sp[-1]; return sp - 1;
    case Op::Pow: sp[-2] = std::pow(sp[-2], sp[-1]); return sp - 1;
    case Op::Neg: sp[-1] = -sp[-1]; return sp;
    case Op::Sin: sp[-1] = std::sin(sp[-1]); return sp;
    case Op::Cos: sp[-1] = std::cos(sp[-1]); return sp;
    case Op::Abs: sp[-1] = std::fabs(sp[-1]); return sp;
    case Op::Max: sp[-2] = std::max(sp[-2], sp[-1]); return sp - 1;
    case Op::Iff: sp[-3] = sp[-3] != 0.0f ? sp[-2] : sp[-1]; return sp - 2;
    case Op::Gt:  sp[-2] = static_cast<float>(sp[-2] >  sp[-1]); return sp - 1;
    case Op::Lt:  sp[-2] = static_cast<float>(sp[-2] <  sp[-1]); return sp - 1;
    case Op::Ge:  sp[-2] = static_cast<float>(sp[-2] >= sp[-1]); return sp - 1;
    case Op::Le:  sp[-2] = static_cast<float>(sp[-2] <= sp[-1]); return sp - 1;
    case Op::Eq:  sp[-2] = static_cast<float>(sp[-2] == sp[-1]); return sp - 1;
    case Op::Ne:  sp[-2] = static_cast<float>(sp[-2] != sp[-1]); return sp - 1;
    case Op::And: sp[-2] = static_cast<float>(sp[-2] != 0.0f && sp[-1] != 0.0f); return sp - 1;
    case Op::Or:  sp[-2] = static_cast<float>(sp[-2] != 0.0f || sp[-1] != 0.0f); return sp - 1;
    case Op::Count: break;
    }
    return sp;
}

inline constexpr uint32_t kMaxStack = 32;
inline constexpr uint32_t kMaxData = 0x3FFF; // 14-bit operand: max constant / variable index

enum class Kind : uint16_t { PushConst = 0, PushVar = 1, Call = 2 };

inline constexpr uint16_t encode(Kind kind, uint32_t data) {
    return static_cast<uint16_t>(static_cast<uint32_t>(kind) | (data << 2));
}
inline constexpr Kind kindOf(uint16_t instruction) { return static_cast<Kind>(instruction & 3u); }
inline constexpr uint16_t dataOf(uint16_t instruction) { return static_cast<uint16_t>(instruction >> 2); }

struct Header {
    uint8_t error;
    uint8_t maxStack;
    uint16_t variableCount; // highest variable index read + 1
    uint32_t constantCount;
    uint32_t instructionCount;
};
static_assert(sizeof(Header) == 12, "unexpected Header size");

namespace detail {

inline const Header& header(const void* bc) { return *static_cast<const Header*>(bc); }

inline const float* constants(const void* bc) {
    return reinterpret_cast<const float*>(static_cast<const char*>(bc) + sizeof(Header));
}

inline const uint16_t* code(const void* bc) {
    return reinterpret_cast<const uint16_t*>(constants(bc) + header(bc).constantCount);
}

inline constexpr size_t roundUp4(size_t n) { return (n + 3) & ~size_t(3); }

}

// `variables[i]` is the value of the i-th name passed to compile(). Returns 0 for bytecode that holds a compile
// error. Only debug builds check that `variables` is large enough.
inline float run(const void* bc, std::span<const float> variables) noexcept {
    const Header& h = detail::header(bc);
    if (h.error) {
        return 0.0f;
    }
    assert(variables.size() >= h.variableCount);

    const float* k = detail::constants(bc);
    const uint16_t* it = detail::code(bc);
    const uint16_t* end = it + h.instructionCount;

    float stack[kMaxStack];
    float* sp = stack;
    for (; it != end; ++it) {
        const uint16_t w = *it;
        switch (kindOf(w)) {
        case Kind::PushConst: *sp++ = k[dataOf(w)]; break;
        case Kind::PushVar:   *sp++ = variables[dataOf(w)]; break;
        default:              sp = apply(static_cast<Op>(dataOf(w)), sp); break;
        }
    }
    return stack[0];
}

// The error message for failed bytecode, or nullptr.
inline const char* error(const void* bc) {
    return detail::header(bc).error ? static_cast<const char*>(bc) + sizeof(Header) : nullptr;
}

// True (and the value) if the expression folded to a single constant.
inline bool isConstant(const void* bc, float* value = nullptr) {
    const Header& h = detail::header(bc);
    if (h.error || h.instructionCount != 1 || kindOf(detail::code(bc)[0]) != Kind::PushConst) {
        return false;
    }
    if (value) {
        *value = detail::constants(bc)[dataOf(detail::code(bc)[0])];
    }
    return true;
}

// Total size in bytes, always a multiple of 4.
inline size_t size(const void* bc) {
    const Header& h = detail::header(bc);
    if (h.error) {
        return detail::roundUp4(sizeof(Header) + std::strlen(error(bc)) + 1);
    }
    return detail::roundUp4(sizeof(Header) + h.constantCount * sizeof(float) +
                            h.instructionCount * sizeof(uint16_t));
}

// Sorted, unique indices of the variables the expression reads.
inline std::vector<uint16_t> usedVariables(const void* bc) {
    std::vector<uint16_t> result;
    const Header& h = detail::header(bc);
    if (h.error) {
        return result;
    }
    const uint16_t* code = detail::code(bc);
    for (uint32_t i = 0; i < h.instructionCount; ++i) {
        if (kindOf(code[i]) == Kind::PushVar) {
            result.push_back(dataOf(code[i]));
        }
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

inline std::string disassemble(const void* bc, std::span<const std::string_view> variableNames = {}) {
    if (const char* e = error(bc)) {
        return std::string("error: ") + e + "\n";
    }
    const Header& h = detail::header(bc);
    const float* k = detail::constants(bc);
    const uint16_t* code = detail::code(bc);
    std::string s = "constants: " + std::to_string(h.constantCount) +
                    ", instructions: " + std::to_string(h.instructionCount) +
                    ", max stack: " + std::to_string(h.maxStack) +
                    ", size: " + std::to_string(size(bc)) + " bytes\n";
    for (uint32_t i = 0; i < h.instructionCount; ++i) {
        const uint16_t d = dataOf(code[i]);
        switch (kindOf(code[i])) {
        case Kind::PushConst:
            s += "  push_const  " + std::to_string(k[d]) + "\n";
            break;
        case Kind::PushVar:
            s += "  push_var    " +
                 (d < variableNames.size() ? std::string(variableNames[d]) : "#" + std::to_string(d)) + "\n";
            break;
        default:
            s += std::string("  call        ") + info(static_cast<Op>(d)).name + "\n";
            break;
        }
    }
    return s;
}

// Owns a compiled bytecode blob. The storage is uint32_t, so data() is 4-byte aligned.
class Bytecode {
public:
    const void* data() const { return m_words.data(); }
    size_t size() const { return expr::size(data()); }
    bool ok() const { return !expr::error(data()); }
    const char* error() const { return expr::error(data()); }
    float run(std::span<const float> variables = {}) const { return expr::run(data(), variables); }

    static Bytecode fromBytes(const void* bytes, size_t n) {
        Bytecode b;
        b.m_words.assign(detail::roundUp4(n) / 4, 0u);
        std::memcpy(b.m_words.data(), bytes, n);
        return b;
    }

private:
    std::vector<uint32_t> m_words;
};

namespace detail {

enum class Tok : uint8_t { Number, Variable, Function, LParen, RParen, Comma };

struct Token {
    Tok type;
    Op op;          // Function
    uint16_t var;   // Variable
    float value;    // Number
    size_t pos;     // source offset, for error messages
};

inline bool isIdentStart(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
inline bool isIdentChar(char c) { return isIdentStart(c) || (c >= '0' && c <= '9'); }
inline bool isDigit(char c) { return c >= '0' && c <= '9'; }

struct CompileError {
    std::string message;
};

class Compiler {
public:
    Compiler(std::string_view source, std::span<const std::string_view> variables)
        : m_source(source), m_variables(variables) {}

    Bytecode compile() {
        try {
            if (m_variables.size() > kMaxData + 1) {
                fail("Too many variables");
            }
            tokenize();
            toRpn();
            checkStack();
            generate();
            return emit();
        } catch (const CompileError& e) {
            return emitError(e.message);
        }
    }

private:
    struct Frame {
        bool call;      // the parentheses belong to a function call
        Op op;          // the function, if `call`
        uint32_t args;  // completed arguments
        bool hasTokens; // the current argument is non-empty
        size_t pos;
    };

    struct Instruction {
        Kind kind;
        uint16_t data;  // variable index or opcode
        float value;    // PushConst
    };

    struct Slot {
        bool isConst;
        float value;
        size_t codeStart; // first instruction that produced this stack slot
    };

    [[noreturn]] static void fail(const std::string& message) { throw CompileError { message }; }

    [[noreturn]] void failAt(const std::string& message, size_t pos) const {
        fail(message + " at: `" + std::string(m_source.substr(pos, 16)) + "`");
    }

    void tokenize() {
        static constexpr std::pair<std::string_view, Op> kTwoChar[] = {
            { ">=", Op::Ge }, { "<=", Op::Le }, { "==", Op::Eq },
            { "!=", Op::Ne }, { "&&", Op::And }, { "||", Op::Or },
        };
        static constexpr std::pair<char, Op> kOneChar[] = {
            { '*', Op::Mul }, { '/', Op::Div }, { '^', Op::Pow }, { '>', Op::Gt }, { '<', Op::Lt },
        };

        bool expectOperand = true; // where a `+` / `-` would be unary
        size_t i = 0;
        while (i < m_source.size()) {
            const char c = m_source[i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++i;
                continue;
            }

            if (isDigit(c) || (c == '.' && i + 1 < m_source.size() && isDigit(m_source[i + 1]))) {
                float v = 0.0f;
                const auto r = std::from_chars(m_source.data() + i, m_source.data() + m_source.size(), v);
                if (r.ec != std::errc()) {
                    failAt("Failed to parse number", i);
                }
                m_tokens.push_back({ Tok::Number, Op::Count, 0, v, i });
                i = static_cast<size_t>(r.ptr - m_source.data());
                expectOperand = false;
                continue;
            }

            if (isIdentStart(c)) {
                const size_t start = i;
                while (i < m_source.size() && isIdentChar(m_source[i])) {
                    ++i;
                }
                const std::string_view name = m_source.substr(start, i - start);
                bool found = false;
                for (size_t f = 0; f < static_cast<size_t>(Op::Count) && !found; ++f) {
                    if (kOps[f].isFunction && name == kOps[f].name) {
                        m_tokens.push_back({ Tok::Function, static_cast<Op>(f), 0, 0.0f, start });
                        expectOperand = true;
                        found = true;
                    }
                }
                for (size_t v = 0; v < m_variables.size() && !found; ++v) {
                    if (name == m_variables[v]) {
                        m_tokens.push_back({ Tok::Variable, Op::Count, static_cast<uint16_t>(v), 0.0f, start });
                        expectOperand = false;
                        found = true;
                    }
                }
                if (!found) {
                    fail("Unknown identifier: `" + std::string(name) + "`");
                }
                continue;
            }

            const size_t start = i;
            switch (c) {
            case '(':
                m_tokens.push_back({ Tok::LParen, Op::Count, 0, 0.0f, start });
                ++i;
                expectOperand = true;
                continue;
            case ')':
                m_tokens.push_back({ Tok::RParen, Op::Count, 0, 0.0f, start });
                ++i;
                expectOperand = false;
                continue;
            case ',':
                m_tokens.push_back({ Tok::Comma, Op::Count, 0, 0.0f, start });
                ++i;
                expectOperand = true;
                continue;
            case '+':
            case '-':
                ++i;
                if (!expectOperand) {
                    m_tokens.push_back({ Tok::Function, c == '+' ? Op::Add : Op::Sub, 0, 0.0f, start });
                } else if (c == '-') {
                    m_tokens.push_back({ Tok::Function, Op::Neg, 0, 0.0f, start });
                }
                // unary `+` is a no-op and produces no token
                expectOperand = true;
                continue;
            default:
                break;
            }

            bool found = false;
            for (const auto& [text, op] : kTwoChar) {
                if (m_source.substr(i, 2) == text) {
                    m_tokens.push_back({ Tok::Function, op, 0, 0.0f, start });
                    i += 2;
                    found = true;
                    break;
                }
            }
            if (!found) {
                for (const auto& [ch, op] : kOneChar) {
                    if (c == ch) {
                        m_tokens.push_back({ Tok::Function, op, 0, 0.0f, start });
                        ++i;
                        found = true;
                        break;
                    }
                }
            }
            if (!found) {
                fail(std::string("Unknown operator: `") + c + "`");
            }
            expectOperand = true;
        }
    }

    // Shunting-yard.
    void toRpn() {
        // Either an operator token or a '(' marker; each '(' has a matching entry in `frames`.
        struct Entry {
            bool paren;
            Token token;
        };
        std::vector<Entry> ops;
        std::vector<Frame> frames;

        auto markContent = [&] {
            if (!frames.empty()) {
                frames.back().hasTokens = true;
            }
        };
        auto popToParen = [&] {
            while (!ops.empty() && !ops.back().paren) {
                m_rpn.push_back(ops.back().token);
                ops.pop_back();
            }
        };

        for (size_t t = 0; t < m_tokens.size(); ++t) {
            const Token& token = m_tokens[t];
            switch (token.type) {
            case Tok::Number:
            case Tok::Variable:
                markContent();
                m_rpn.push_back(token);
                break;

            case Tok::Function: {
                markContent();
                const OpInfo& oi = info(token.op);
                if (oi.isFunction) {
                    if (t + 1 >= m_tokens.size() || m_tokens[t + 1].type != Tok::LParen) {
                        failAt(std::string("Expected `(` after `") + oi.name + "`", token.pos);
                    }
                    ops.push_back({ false, token });
                    break;
                }
                // Unary prefix operators never pop anything.
                if (oi.arity == 2) {
                    while (!ops.empty() && !ops.back().paren) {
                        const OpInfo& top = info(ops.back().token.op);
                        if (top.isFunction) {
                            break;
                        }
                        const bool pop = oi.rightAssoc ? top.precedence > oi.precedence
                                                       : top.precedence >= oi.precedence;
                        if (!pop) {
                            break;
                        }
                        m_rpn.push_back(ops.back().token);
                        ops.pop_back();
                    }
                }
                ops.push_back({ false, token });
                break;
            }

            case Tok::LParen: {
                markContent();
                const bool call = !ops.empty() && !ops.back().paren && info(ops.back().token.op).isFunction &&
                                  t > 0 && m_tokens[t - 1].type == Tok::Function;
                frames.push_back({ call, call ? ops.back().token.op : Op::Count, 0, false, token.pos });
                ops.push_back({ true, token });
                break;
            }

            case Tok::Comma: {
                if (frames.empty() || !frames.back().call) {
                    failAt("`,` outside of a function call", token.pos);
                }
                if (!frames.back().hasTokens) {
                    failAt("Empty argument", token.pos);
                }
                popToParen();
                frames.back().args++;
                frames.back().hasTokens = false;
                break;
            }

            case Tok::RParen: {
                if (frames.empty()) {
                    failAt("`)` without corresponding `(`", token.pos);
                }
                popToParen();
                ops.pop_back();
                Frame f = frames.back();
                frames.pop_back();
                if (f.hasTokens) {
                    f.args++;
                } else if (f.args > 0) {
                    failAt("Empty argument", token.pos);
                }
                if (f.call) {
                    const OpInfo& oi = info(f.op);
                    if (f.args != oi.arity) {
                        fail(std::string("`") + oi.name + "` expects " + std::to_string(oi.arity) +
                             " argument(s), got " + std::to_string(f.args));
                    }
                    m_rpn.push_back(ops.back().token);
                    ops.pop_back();
                } else if (f.args != 1) {
                    failAt("Empty parentheses", f.pos);
                }
                break;
            }
            }
        }

        if (!frames.empty()) {
            failAt("Unmatched `(`", frames.back().pos);
        }
        while (!ops.empty()) {
            m_rpn.push_back(ops.back().token);
            ops.pop_back();
        }
    }

    void checkStack() const {
        uint32_t depth = 0;
        for (const Token& t : m_rpn) {
            if (t.type != Tok::Function) {
                if (++depth > kMaxStack) {
                    failAt("Expression too deep (stack overflow)", t.pos);
                }
                continue;
            }
            const OpInfo& oi = info(t.op);
            if (depth < oi.arity) {
                failAt(std::string("Not enough arguments for `") + oi.name + "`", t.pos);
            }
            depth = depth - oi.arity + 1;
        }
        if (depth == 0) {
            fail("Expression doesn't produce a value");
        }
        if (depth > 1) {
            fail("Expression produces more than one value");
        }
    }

    // Constant-folds as it goes: a call whose arguments are all constants replaces their code with the result.
    void generate() {
        std::vector<Slot> stack;
        for (const Token& t : m_rpn) {
            switch (t.type) {
            case Tok::Number:
                stack.push_back({ true, t.value, m_code.size() });
                m_code.push_back({ Kind::PushConst, 0, t.value });
                break;
            case Tok::Variable:
                stack.push_back({ false, 0.0f, m_code.size() });
                m_code.push_back({ Kind::PushVar, t.var, 0.0f });
                break;
            default: {
                const uint32_t n = info(t.op).arity;
                const size_t first = stack.size() - n;
                const bool allConst = std::all_of(stack.begin() + first, stack.end(),
                                                  [](const Slot& s) { return s.isConst; });
                const size_t codeStart = stack[first].codeStart;
                float args[3];
                for (uint32_t a = 0; a < n; ++a) {
                    args[a] = stack[first + a].value;
                }
                stack.resize(first);
                if (allConst) {
                    apply(t.op, args + n);
                    m_code.resize(codeStart);
                    stack.push_back({ true, args[0], m_code.size() });
                    m_code.push_back({ Kind::PushConst, 0, args[0] });
                } else {
                    stack.push_back({ false, 0.0f, codeStart });
                    m_code.push_back({ Kind::Call, static_cast<uint16_t>(t.op), 0.0f });
                }
                break;
            }
            }
        }
    }

    Bytecode emit() const {
        std::vector<float> constants;
        std::unordered_map<uint32_t, uint16_t> constantIndex; // keyed by bit pattern
        std::vector<uint16_t> words;
        uint32_t depth = 0;
        uint32_t maxDepth = 0;
        uint32_t variableCount = 0;

        for (const Instruction& in : m_code) {
            switch (in.kind) {
            case Kind::PushConst: {
                uint32_t bits;
                std::memcpy(&bits, &in.value, sizeof(bits));
                auto it = constantIndex.find(bits);
                if (it == constantIndex.end()) {
                    if (constants.size() > kMaxData) {
                        fail("Too many constants");
                    }
                    it = constantIndex.emplace(bits, static_cast<uint16_t>(constants.size())).first;
                    constants.push_back(in.value);
                }
                words.push_back(encode(Kind::PushConst, it->second));
                maxDepth = std::max(maxDepth, ++depth);
                break;
            }
            case Kind::PushVar:
                words.push_back(encode(Kind::PushVar, in.data));
                maxDepth = std::max(maxDepth, ++depth);
                variableCount = std::max(variableCount, in.data + 1u);
                break;
            case Kind::Call:
                words.push_back(encode(Kind::Call, in.data));
                depth = depth - info(static_cast<Op>(in.data)).arity + 1;
                break;
            }
        }

        Header h {};
        h.maxStack = static_cast<uint8_t>(maxDepth);
        h.variableCount = static_cast<uint16_t>(variableCount);
        h.constantCount = static_cast<uint32_t>(constants.size());
        h.instructionCount = static_cast<uint32_t>(words.size());

        const size_t constantBytes = constants.size() * sizeof(float);
        const size_t codeBytes = words.size() * sizeof(uint16_t);
        std::vector<char> bytes(sizeof(Header) + constantBytes + codeBytes);
        char* p = bytes.data();
        std::memcpy(p, &h, sizeof(h));
        p += sizeof(h);
        if (constantBytes) {
            std::memcpy(p, constants.data(), constantBytes);
        }
        p += constantBytes;
        if (codeBytes) {
            std::memcpy(p, words.data(), codeBytes);
        }
        return Bytecode::fromBytes(bytes.data(), bytes.size());
    }

    static Bytecode emitError(const std::string& message) {
        Header h {};
        h.error = 1;
        std::vector<char> bytes(sizeof(Header) + message.size() + 1, '\0');
        std::memcpy(bytes.data(), &h, sizeof(h));
        std::memcpy(bytes.data() + sizeof(h), message.c_str(), message.size() + 1);
        return Bytecode::fromBytes(bytes.data(), bytes.size());
    }

    std::string_view m_source;
    std::span<const std::string_view> m_variables;
    std::vector<Token> m_tokens;
    std::vector<Token> m_rpn;
    std::vector<Instruction> m_code;
};

}

// Variable i in the expression reads variables[i] at run(). Never throws: on error the returned Bytecode holds the
// message (check ok() / error()).
inline Bytecode compile(std::string_view source, std::span<const std::string_view> variables = {}) {
    return detail::Compiler(source, variables).compile();
}

}
