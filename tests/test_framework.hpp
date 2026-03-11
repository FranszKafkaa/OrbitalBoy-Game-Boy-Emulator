#pragma once

#include <algorithm>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace tests {

struct TestCase {
    std::string suite;
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

class Registrar {
public:
    Registrar(const char* suite, const char* name, std::function<void()> fn) {
        registry().push_back(TestCase{suite, name, std::move(fn)});
    }
};

inline std::string location(const char* file, int line) {
    std::ostringstream oss;
    oss << file << ':' << line;
    return oss.str();
}

template <typename T>
std::string toString(const T& value) {
    std::ostringstream oss;
    if constexpr (std::is_same_v<T, bool>) {
        oss << (value ? "true" : "false");
    } else if constexpr (std::is_integral_v<T>) {
        using U = std::conditional_t<std::is_signed_v<T>, std::int64_t, std::uint64_t>;
        const auto wide = static_cast<U>(value);
        oss << "0x" << std::hex << static_cast<std::uint64_t>(wide)
            << " (" << std::dec << static_cast<std::uint64_t>(wide) << ')';
    } else {
        oss << value;
    }
    return oss.str();
}

inline void require(bool condition, const char* expression, const char* file, int line) {
    if (condition) {
        return;
    }

    std::ostringstream oss;
    oss << location(file, line) << " | requisito falhou: " << expression;
    throw std::runtime_error(oss.str());
}

template <typename A, typename B>
void requireEq(const A& actual, const B& expected, const char* aexpr, const char* eexpr, const char* file, int line) {
    bool equal = false;
    if constexpr (std::is_arithmetic_v<A> && std::is_arithmetic_v<B>) {
        using Common = std::common_type_t<A, B>;
        equal = static_cast<Common>(actual) == static_cast<Common>(expected);
    } else {
        equal = actual == expected;
    }
    if (equal) {
        return;
    }

    std::ostringstream oss;
    oss << location(file, line) << " | igualdade falhou: " << aexpr << " != " << eexpr
        << " | atual=" << toString(actual)
        << " esperado=" << toString(expected);
    throw std::runtime_error(oss.str());
}

inline int runTests(const std::vector<std::string>& filterSuites);

inline int runAllTests() {
    return runTests({});
}

inline bool suiteMatchesFilter(const std::string& suite, const std::vector<std::string>& filterSuites) {
    if (filterSuites.empty()) {
        return true;
    }
    return std::find(filterSuites.begin(), filterSuites.end(), suite) != filterSuites.end();
}

inline std::vector<std::string> listSuites() {
    std::vector<std::string> suites;
    for (const auto& tc : registry()) {
        if (std::find(suites.begin(), suites.end(), tc.suite) == suites.end()) {
            suites.push_back(tc.suite);
        }
    }
    std::sort(suites.begin(), suites.end());
    return suites;
}

inline int runTests(const std::vector<std::string>& filterSuites) {
    auto tests = registry();
    std::sort(tests.begin(), tests.end(), [](const TestCase& lhs, const TestCase& rhs) {
        if (lhs.suite != rhs.suite) {
            return lhs.suite < rhs.suite;
        }
        return lhs.name < rhs.name;
    });

    int selected = 0;
    int passed = 0;
    int failed = 0;

    for (const auto& tc : tests) {
        if (!suiteMatchesFilter(tc.suite, filterSuites)) {
            continue;
        }
        ++selected;

        try {
            tc.fn();
            ++passed;
        } catch (const std::exception& e) {
            ++failed;
            std::cerr << "[FAIL] " << tc.suite << '.' << tc.name << " -> " << e.what() << '\n';
        } catch (...) {
            ++failed;
            std::cerr << "[FAIL] " << tc.suite << '.' << tc.name << " -> erro desconhecido\n";
        }
    }

    if (selected == 0) {
        std::cerr << "[TEST] nenhum teste selecionado\n";
        return 1;
    }

    std::cerr << "[TEST] total=" << selected << " pass=" << passed << " fail=" << failed << '\n';
    return failed == 0 ? 0 : 1;
}

} // namespace tests

#define T_CONCAT_INNER(a, b) a##b
#define T_CONCAT(a, b) T_CONCAT_INNER(a, b)

#define TEST_CASE(SUITE, NAME) \
    static void T_CONCAT(test_fn_, __LINE__)(); \
    static ::tests::Registrar T_CONCAT(test_reg_, __LINE__)(SUITE, NAME, T_CONCAT(test_fn_, __LINE__)); \
    static void T_CONCAT(test_fn_, __LINE__)()

#define T_REQUIRE(EXPR) ::tests::require((EXPR), #EXPR, __FILE__, __LINE__)
#define T_EQ(ACTUAL, EXPECTED) ::tests::requireEq((ACTUAL), (EXPECTED), #ACTUAL, #EXPECTED, __FILE__, __LINE__)
