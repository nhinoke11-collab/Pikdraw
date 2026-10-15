#pragma once

#include <string_view>
#include <functional>
#include <stdexcept>
#include <cmath>
#include <string>

namespace pikdraw::geometry::test {

void register_test(std::string_view name, std::function<void()> func);

struct AutoRegister {
    AutoRegister(std::string_view name, std::function<void()> func) {
        register_test(name, std::move(func));
    }
};

#define PIKDRAW_TEST(name) \
    static void test_##name(); \
    static ::pikdraw::geometry::test::AutoRegister auto_reg_##name(#name, test_##name); \
    static void test_##name()

#define PIKDRAW_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            throw std::runtime_error(std::string("Assertion failed: ") + #cond + " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while (0)

#define PIKDRAW_ASSERT_NEAR(val1, val2, eps) \
    do { \
        if (std::abs((val1) - (val2)) > (eps)) { \
            throw std::runtime_error(std::string("Near assertion failed: |") + #val1 + " - " + #val2 + "| = " + \
                std::to_string(std::abs((val1) - (val2))) + " > " + std::to_string(eps) + \
                " (" + std::to_string(val1) + " vs " + std::to_string(val2) + ") at " + \
                __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while (0)

} // namespace pikdraw::geometry::test
