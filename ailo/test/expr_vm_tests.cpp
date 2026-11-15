#include "animation/ExprVM.h"
#include "Testing.h"

#include <string>
#include <string_view>
#include <vector>

using namespace ailo;

namespace {

float eval(std::string_view source, const std::vector<std::string_view>& names = {},
           const std::vector<float>& values = {}) {
    const expr::Bytecode bc = expr::compile(source, names);
    CHECK(bc.ok());
    return bc.run(values);
}

std::string compileError(std::string_view source, const std::vector<std::string_view>& names = {}) {
    const expr::Bytecode bc = expr::compile(source, names);
    return bc.ok() ? std::string() : std::string(bc.error());
}

void testPrecedence() {
    CHECK_NEAR(eval("1 + 2 * 3"), 7.0f, 1e-6f);
    CHECK_NEAR(eval("(1 + 2) * 3"), 9.0f, 1e-6f);
    CHECK_NEAR(eval("2 ^ 3 ^ 2"), 512.0f, 1e-3f);
    CHECK_NEAR(eval("-2 ^ 2"), -4.0f, 1e-6f);
    CHECK_NEAR(eval("10 - 4 - 3"), 3.0f, 1e-6f);
    CHECK_NEAR(eval("+.5 + 1e-1"), 0.6f, 1e-6f);
    CHECK_EQ(eval("1 < 2 && 3 >= 3 || 0"), 1.0f);
    CHECK_EQ(eval("1 == 2 != 1"), 1.0f);
}

void testFunctions() {
    CHECK_NEAR(eval("max(1, 2) + abs(-3)"), 5.0f, 1e-6f);
    CHECK_NEAR(eval("sin(0) + cos(0)"), 1.0f, 1e-6f);
    CHECK_EQ(eval("iff(0, 1, 2)"), 2.0f);
    CHECK_EQ(eval("iff(1, max(2, 3), 4)"), 3.0f);
}

void testVariables() {
    const std::vector<std::string_view> names = { "speed", "grounded" };
    CHECK_EQ(eval("speed > 0.5 && grounded", names, { 1.0f, 1.0f }), 1.0f);
    CHECK_EQ(eval("speed > 0.5 && grounded", names, { 1.0f, 0.0f }), 0.0f);
    CHECK_NEAR(eval("speed * 2 - grounded", names, { 3.0f, 1.0f }), 5.0f, 1e-6f);

    const expr::Bytecode bc = expr::compile("grounded + grounded * 2", names);
    REQUIRE(bc.ok());
    CHECK(expr::usedVariables(bc.data()) == std::vector<uint16_t> { 1 });
    CHECK_EQ(static_cast<const expr::Header*>(bc.data())->variableCount, 2);
}

void testConstantFolding() {
    float value = 0.0f;
    const expr::Bytecode folded = expr::compile("max(2, 3) * (1 + 1)");
    REQUIRE(folded.ok());
    CHECK(expr::isConstant(folded.data(), &value));
    CHECK_EQ(value, 6.0f);

    const std::vector<std::string_view> names = { "x" };
    const expr::Bytecode partial = expr::compile("x + 2 * 3", names);
    REQUIRE(partial.ok());
    CHECK(!expr::isConstant(partial.data()));
    const float x[] = { 1.0f };
    CHECK_EQ(partial.run(x), 7.0f);
}

void testErrors() {
    CHECK(compileError("1 +").find("Not enough arguments") != std::string::npos);
    CHECK(compileError("foo").find("Unknown identifier") != std::string::npos);
    CHECK(compileError("(1").find("Unmatched") != std::string::npos);
    CHECK(compileError("1)").find("without corresponding") != std::string::npos);
    CHECK(compileError("max(1)").find("expects 2") != std::string::npos);
    CHECK(compileError("max(1,)").find("Empty argument") != std::string::npos);
    CHECK(compileError("()").find("Empty parentheses") != std::string::npos);
    CHECK(compileError("1 2").find("more than one value") != std::string::npos);
    CHECK(compileError("").find("doesn't produce") != std::string::npos);
    CHECK(compileError("1 $ 2").find("Unknown operator") != std::string::npos);

    const expr::Bytecode bc = expr::compile("1 +");
    CHECK_EQ(bc.run(), 0.0f);
    CHECK_EQ(bc.size() % 4, size_t(0));
}

}

void runExprVMTests() {
    ailo::test::run("expr_vm/testPrecedence", testPrecedence);
    ailo::test::run("expr_vm/testFunctions", testFunctions);
    ailo::test::run("expr_vm/testVariables", testVariables);
    ailo::test::run("expr_vm/testConstantFolding", testConstantFolding);
    ailo::test::run("expr_vm/testErrors", testErrors);
}
