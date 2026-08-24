#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <cstdlib>
#include <stdexcept>

namespace faircross::test {

/// Thrown by a failed assertion.
///
/// Assertions previously called std::exit, which ended the whole process on the
/// first failure and hid every later test's result. Failures now unwind to the
/// runner so the suite reports a complete picture in one run.
struct TestFailure : std::runtime_error {
    explicit TestFailure(const std::string& what) : std::runtime_error(what) {}
};

[[noreturn]] inline void fail(const std::string& location, const std::string& detail) {
    throw TestFailure(location + ": " + detail);
}

struct TestCase {
    std::string name;
    std::function<void()> func;
};

inline std::vector<TestCase>& get_registry() {
    static std::vector<TestCase> registry;
    return registry;
}

inline void register_test(std::string name, std::function<void()> func) {
    get_registry().push_back(TestCase{std::move(name), std::move(func)});
}

#define TEST_CASE(name) \
    static void test_func_##name(); \
    static struct AutoReg_##name { \
        AutoReg_##name() { \
            ::faircross::test::register_test(#name, test_func_##name); \
        } \
    } auto_reg_##name; \
    static void test_func_##name()

#define FAIRCROSS_TEST_LOCATION \
    (std::string(__FILE__) + ":" + std::to_string(__LINE__))

#define REQUIRE(expr) \
    do { \
        if (!(expr)) { \
            ::faircross::test::fail(FAIRCROSS_TEST_LOCATION, "REQUIRE(" #expr ")"); \
        } \
    } while (0)

#define REQUIRE_EQ(a, b) \
    do { \
        if (!((a) == (b))) { \
            ::faircross::test::fail(FAIRCROSS_TEST_LOCATION, "REQUIRE_EQ(" #a ", " #b ")"); \
        } \
    } while (0)

#define REQUIRE_NE(a, b) \
    do { \
        if ((a) == (b)) { \
            ::faircross::test::fail(FAIRCROSS_TEST_LOCATION, "REQUIRE_NE(" #a ", " #b ")"); \
        } \
    } while (0)

} // namespace faircross::test
