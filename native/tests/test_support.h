#pragma once

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace desktop_todo::test_support {

using TestFunction = void (*)();

struct TestCase {
    std::string_view name;
    TestFunction function;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

class Registrar {
public:
    Registrar(std::string_view name, TestFunction function) {
        registry().push_back({name, function});
    }
};

template <typename Actual, typename Expected>
void expect_equal(const Actual& actual, const Expected& expected, std::string_view expression) {
    if (!(actual == expected)) {
        throw std::runtime_error(std::string{"expectation failed: "} + std::string{expression});
    }
}

inline void expect_true(bool value, std::string_view expression) {
    if (!value) {
        throw std::runtime_error(std::string{"expectation failed: "} + std::string{expression});
    }
}

inline int run_all(std::string_view filter = {}) {
    int failures = 0;
    int selected = 0;
    for (const auto& test : registry()) {
        if (!filter.empty() && test.name.find(filter) == std::string_view::npos) {
            continue;
        }
        ++selected;
        try {
            test.function();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
        } catch (...) {
            ++failures;
            std::cerr << "[FAIL] " << test.name << ": unknown exception\n";
        }
    }
    if (selected == 0) {
        std::cerr << "[FAIL] no tests matched filter: " << filter << '\n';
        return 2;
    }
    return failures == 0 ? 0 : 1;
}

}  // namespace desktop_todo::test_support

#define TEST_CASE(name) \
    static void name(); \
    static ::desktop_todo::test_support::Registrar name##_registrar{#name, &name}; \
    static void name()

#define EXPECT_TRUE(value) \
    ::desktop_todo::test_support::expect_true((value), #value)

#define EXPECT_EQ(actual, expected) \
    ::desktop_todo::test_support::expect_equal((actual), (expected), #actual " == " #expected)
