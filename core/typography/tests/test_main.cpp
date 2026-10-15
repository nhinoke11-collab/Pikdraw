#include <iostream>
#include <string_view>
#include <vector>
#include <functional>

namespace pikdraw::typography::test {

struct TestCase {
    std::string_view name;
    std::function<void()> func;
};

std::vector<TestCase>& registry() {
    static std::vector<TestCase> test_cases;
    return test_cases;
}

void register_test(std::string_view name, std::function<void()> func) {
    registry().push_back({name, std::move(func)});
}

} // namespace pikdraw::typography::test

int main() {
    int passed = 0;
    int failed = 0;
    const auto& tests = pikdraw::typography::test::registry();

    std::cout << "=== Running PikDraw Typography Tests (" << tests.size() << " tests) ===\n";

    for (const auto& t : tests) {
        try {
            t.func();
            std::cout << "[PASS] " << t.name << "\n";
            passed++;
        } catch (const std::exception& e) {
            std::cout << "[FAIL] " << t.name << " : " << e.what() << "\n";
            failed++;
        } catch (...) {
            std::cout << "[FAIL] " << t.name << " : unknown exception\n";
            failed++;
        }
    }

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed.\n";
    return failed == 0 ? 0 : 1;
}
