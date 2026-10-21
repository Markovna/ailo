#pragma once

#include <cmath>
#include <concepts>
#include <cstddef>
#include <functional>
#include <ostream>
#include <ranges>
#include <source_location>
#include <sstream>
#include <string>
#include <string_view>

namespace ailo::test {

struct RequireFailed {};

void run(std::string_view name, const std::function<void()>& test);

void fail(std::string_view message, std::source_location where = std::source_location::current());

int failureCount();

// Prints the summary and returns the exit code for main: 0 when nothing failed.
int finish();

namespace detail {

template<typename T>
concept Streamable = requires(std::ostream& os, const T& value) { os << value; };

template<typename T>
constexpr bool isDescribable() {
    if constexpr (Streamable<T>) {
        return true;
    } else if constexpr (std::ranges::input_range<const T>) {
        return isDescribable<std::ranges::range_value_t<const T>>();
    } else {
        return false;
    }
}

// Ranges print their first few elements, so a mismatched pixel buffer doesn't flood the log.
constexpr std::size_t kMaxDescribedElements = 16;

template<typename T>
void describeTo(std::ostream& os, const T& value) {
    if constexpr (std::same_as<T, unsigned char> || std::same_as<T, signed char>) {
        os << static_cast<int>(value);
    } else if constexpr (Streamable<T>) {
        os << value;
    } else {
        os << "{";
        std::size_t count = 0;
        bool truncated = false;
        for (const auto& element : value) {
            if (count == kMaxDescribedElements) {
                truncated = true;
                break;
            }
            os << (count++ ? ", " : "");
            describeTo(os, element);
        }
        os << (truncated ? ", ...}" : "}");
        if (truncated) {
            os << " (" << std::ranges::distance(value) << " elements)";
        }
    }
}

template<typename T>
std::string describe(const T& value) {
    if constexpr (isDescribable<T>()) {
        std::ostringstream os;
        describeTo(os, value);
        return os.str();
    } else {
        return "<unprintable>";
    }
}

bool check(bool ok, std::string_view text, std::source_location where);

template<typename A, typename B, typename Op>
bool compare(const A& a, const B& b, Op op, std::string_view text, std::source_location where) {
    if (op(a, b)) {
        return check(true, text, where);
    }
    return check(false, std::string(text) + "\n    left:  " + describe(a) + "\n    right: " + describe(b), where);
}

template<typename A, typename B, typename E>
bool near(const A& a, const B& b, const E& epsilon, std::string_view text, std::source_location where) {
    if (std::abs(a - b) <= epsilon) {
        return check(true, text, where);
    }
    return check(false, std::string(text) + "\n    left:  " + describe(a) + "\n    right: " + describe(b) +
                        "\n    epsilon: " + describe(epsilon), where);
}

}

}

#define AILO_TEST_ON_FAIL_CHECK (void)0
#define AILO_TEST_ON_FAIL_REQUIRE throw ::ailo::test::RequireFailed {}

#define AILO_TEST_BOOL(expr, onFail) \
    do { \
        if (!::ailo::test::detail::check(static_cast<bool>(expr), #expr, std::source_location::current())) onFail; \
    } while (0)

#define AILO_TEST_COMPARE(a, b, op, onFail) \
    do { \
        if (!::ailo::test::detail::compare((a), (b), [](const auto& x_, const auto& y_) { return x_ op y_; }, \
                #a " " #op " " #b, std::source_location::current())) onFail; \
    } while (0)

#define AILO_TEST_NEAR(a, b, epsilon, onFail) \
    do { \
        if (!::ailo::test::detail::near((a), (b), (epsilon), "|" #a " - " #b "| <= " #epsilon, \
                std::source_location::current())) onFail; \
    } while (0)

#define AILO_TEST_THROWS(expr, onFail) \
    do { \
        bool thrown_ = false; \
        try { (void)(expr); } catch (...) { thrown_ = true; } \
        if (!::ailo::test::detail::check(thrown_, #expr " throws", std::source_location::current())) onFail; \
    } while (0)

#define CHECK(expr) AILO_TEST_BOOL(expr, AILO_TEST_ON_FAIL_CHECK)
#define CHECK_EQ(a, b) AILO_TEST_COMPARE(a, b, ==, AILO_TEST_ON_FAIL_CHECK)
#define CHECK_NE(a, b) AILO_TEST_COMPARE(a, b, !=, AILO_TEST_ON_FAIL_CHECK)
#define CHECK_LT(a, b) AILO_TEST_COMPARE(a, b, <, AILO_TEST_ON_FAIL_CHECK)
#define CHECK_LE(a, b) AILO_TEST_COMPARE(a, b, <=, AILO_TEST_ON_FAIL_CHECK)
#define CHECK_NEAR(a, b, epsilon) AILO_TEST_NEAR(a, b, epsilon, AILO_TEST_ON_FAIL_CHECK)
#define CHECK_THROWS(expr) AILO_TEST_THROWS(expr, AILO_TEST_ON_FAIL_CHECK)

#define REQUIRE(expr) AILO_TEST_BOOL(expr, AILO_TEST_ON_FAIL_REQUIRE)
#define REQUIRE_EQ(a, b) AILO_TEST_COMPARE(a, b, ==, AILO_TEST_ON_FAIL_REQUIRE)
#define REQUIRE_NE(a, b) AILO_TEST_COMPARE(a, b, !=, AILO_TEST_ON_FAIL_REQUIRE)
#define REQUIRE_NEAR(a, b, epsilon) AILO_TEST_NEAR(a, b, epsilon, AILO_TEST_ON_FAIL_REQUIRE)
