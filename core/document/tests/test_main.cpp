#include <iostream>
#include "test_harness.hpp"

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    const auto& test_cases = pikdraw::test::get_registry();
    std::cout << "=== PikDraw Spike B Test Suite ===\n";
    std::cout << "Running " << test_cases.size() << " test cases...\n";

    int passed = 0;
    int failed = 0;

    for (const auto& tc : test_cases) {
        std::cout << "[ RUN      ] " << tc.name << "\n";
        try {
            tc.func();
            std::cout << "[       OK ] " << tc.name << "\n";
            passed++;
        } catch (const pikdraw::test::TestFailure& e) {
            std::cerr << "[  FAILED  ] " << tc.name << "\n  " << e.what() << "\n";
            failed++;
        } catch (const std::exception& e) {
            std::cerr << "[  FAILED  ] " << tc.name << " (Unexpected exception: " << e.what() << ")\n";
            failed++;
        } catch (...) {
            std::cerr << "[  FAILED  ] " << tc.name << " (Unknown exception)\n";
            failed++;
        }
    }

    std::cout << "\nTest Summary: " << passed << " passed, " << failed << " failed.\n";
    return failed == 0 ? 0 : 1;
}
