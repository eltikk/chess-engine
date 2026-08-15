#pragma once

#include <cstdint>
#include <exception>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Test {

struct TestCase {
    std::string name;
    std::function<void()> function;
};

struct Counters {
    int assertionsPassed = 0;
    int assertionsFailed = 0;
    int testsPassed = 0;
    int testsFailed = 0;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline Counters& counters() {
    static Counters result;
    return result;
}

class Registrar {
public:
    Registrar(std::string name, std::function<void()> function) {
        registry().push_back({std::move(name), std::move(function)});
    }
};

template <typename T>
std::string valueToString(const T& value) {
    std::ostringstream output;
    if constexpr (std::is_same_v<std::remove_cvref_t<T>, std::uint64_t>) {
        output << "0x" << std::hex << value;
    } else if constexpr (std::is_enum_v<std::remove_cvref_t<T>>) {
        // Unary + promotes byte-sized underlying types so they print as a
        // number rather than as a character.
        output << +static_cast<std::underlying_type_t<std::remove_cvref_t<T>>>(value);
    } else {
        output << std::boolalpha << value;
    }
    return output.str();
}

template <typename Expected, typename Actual>
void expectEqual(
    const Expected& expected,
    const Actual& actual,
    const char* expectedExpression,
    const char* actualExpression,
    const char* file,
    int line
) {
    if (expected == actual) {
        ++counters().assertionsPassed;
        return;
    }

    ++counters().assertionsFailed;
    std::cerr << "\n  " << file << ':' << line << ": EXPECT_EQ failed\n"
              << "    expected (" << expectedExpression << "): "
              << valueToString(expected) << '\n'
              << "    actual   (" << actualExpression << "): "
              << valueToString(actual) << '\n';
}

inline void expectCondition(
    bool expected,
    bool actual,
    const char* expression,
    const char* assertion,
    const char* file,
    int line
) {
    if (expected == actual) {
        ++counters().assertionsPassed;
        return;
    }

    ++counters().assertionsFailed;
    std::cerr << "\n  " << file << ':' << line << ": " << assertion << " failed\n"
              << "    expression: " << expression << '\n'
              << "    expected:   " << std::boolalpha << expected << '\n'
              << "    actual:     " << std::boolalpha << actual << '\n';
}

inline int square(const char* algebraic) {
    return (algebraic[1] - '1') * 8 + (algebraic[0] - 'a');
}

inline std::uint64_t bitboard(std::initializer_list<const char*> squares) {
    std::uint64_t result = 0ULL;
    for (const char* algebraic : squares) {
        result |= 1ULL << square(algebraic);
    }
    return result;
}

inline int runAllTests() {
    std::cout << "Running " << registry().size() << " tests\n";

    for (const TestCase& test : registry()) {
        const int failuresBefore = counters().assertionsFailed;
        std::cout << "[ RUN      ] " << test.name << '\n';

        try {
            test.function();
        } catch (const std::exception& exception) {
            ++counters().assertionsFailed;
            std::cerr << "  unexpected exception: " << exception.what() << '\n';
        } catch (...) {
            ++counters().assertionsFailed;
            std::cerr << "  unexpected non-standard exception\n";
        }

        if (counters().assertionsFailed == failuresBefore) {
            ++counters().testsPassed;
            std::cout << "[       OK ] " << test.name << '\n';
        } else {
            ++counters().testsFailed;
            std::cout << "[  FAILED  ] " << test.name << '\n';
        }
    }

    std::cout << "\nTests: " << counters().testsPassed << " passed, "
              << counters().testsFailed << " failed\n"
              << "Assertions: " << counters().assertionsPassed << " passed, "
              << counters().assertionsFailed << " failed\n";

    return counters().testsFailed == 0 ? 0 : 1;
}

} // namespace Test

#define TEST_DETAIL_JOIN_IMPL(left, right) left##right
#define TEST_DETAIL_JOIN(left, right) TEST_DETAIL_JOIN_IMPL(left, right)

#define TEST_CASE(name)                                                        \
    static void TEST_DETAIL_JOIN(testFunction_, __LINE__)();                   \
    static const Test::Registrar TEST_DETAIL_JOIN(testRegistrar_, __LINE__)(   \
        name, TEST_DETAIL_JOIN(testFunction_, __LINE__));                      \
    static void TEST_DETAIL_JOIN(testFunction_, __LINE__)()

#define EXPECT_EQ(expected, actual)                                            \
    Test::expectEqual((expected), (actual), #expected, #actual, __FILE__, __LINE__)

#define EXPECT_TRUE(expression)                                                \
    Test::expectCondition(true, static_cast<bool>(expression), #expression,    \
                          "EXPECT_TRUE", __FILE__, __LINE__)

#define EXPECT_FALSE(expression)                                               \
    Test::expectCondition(false, static_cast<bool>(expression), #expression,   \
                          "EXPECT_FALSE", __FILE__, __LINE__)
