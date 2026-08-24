#include <iostream>
#include <string>
#include <vector>
#include "test_framework.hpp"

int main() {
    std::cout << "== FairCross C++20 Test Suite Runner ==\n";
    auto& tests = faircross::test::get_registry();
    std::cout << "Running " << tests.size() << " test cases...\n\n";

    size_t passed = 0;
    std::vector<std::string> failures;

    // Every test runs even after one fails: a suite that stops at the first
    // failure reports nothing about the tests behind it, which is how the
    // commitment domain-separation defect stayed invisible.
    for (const auto& test : tests) {
        std::cout << "  RUN: " << test.name << "... " << std::flush;
        try {
            test.func();
            std::cout << "OK\n";
            ++passed;
        } catch (const std::exception& e) {
            std::cout << "FAILED\n";
            std::cerr << "      " << e.what() << "\n";
            failures.push_back(test.name);
        }
    }

    std::cout << "\n" << passed << " passed, " << failures.size() << " failed, "
              << tests.size() << " total\n";

    if (!failures.empty()) {
        std::cout << "\nFailed tests:\n";
        for (const auto& name : failures) {
            std::cout << "  - " << name << "\n";
        }
        return 1;
    }

    std::cout << "\nAll " << passed << " tests PASSED successfully!\n";
    return 0;
}
