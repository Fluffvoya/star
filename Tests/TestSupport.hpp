#pragma once

#include <exception>
#include <iostream>
#include <string_view>
#include <vector>

namespace star::test {

using TestFunction = void (*)();

struct TestCase {
    std::string_view name;
    TestFunction function;
};

inline std::vector<TestCase> &Registry() {
    static std::vector<TestCase> tests;
    return tests;
}

class Registrar final {
  public:
    Registrar(std::string_view name, TestFunction function) { Registry().push_back(TestCase{name, function}); }
};

inline int RunAll() {
    int failed = 0;
    for (const TestCase &test : Registry()) {
        try {
            test.function();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception &exception) {
            ++failed;
            std::cerr << "[FAIL] " << test.name << ": " << exception.what() << '\n';
        } catch (...) {
            ++failed;
            std::cerr << "[FAIL] " << test.name << ": unknown exception\n";
        }
    }

    if (failed != 0) {
        std::cerr << failed << " test(s) failed\n";
        return 1;
    }
    return 0;
}

[[noreturn]] inline void Fail(std::string_view expression, std::string_view file, int line) {
    throw std::runtime_error(std::string(file) + ":" + std::to_string(line) +
                             ": check failed: " + std::string(expression));
}

} // namespace star::test

#define STAR_TEST(name)                                                                                                \
    static void name();                                                                                                \
    static const ::star::test::Registrar name##Registrar(#name, &name);                                                \
    static void name()

#define STAR_CHECK(expression)                                                                                         \
    do {                                                                                                               \
        if (!(expression)) {                                                                                           \
            ::star::test::Fail(#expression, __FILE__, __LINE__);                                                       \
        }                                                                                                              \
    } while (false)
