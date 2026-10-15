#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <sstream>
#include <cmath>
#include <stdexcept>

namespace pikdraw::test {

class TestFailure : public std::runtime_error {
public:
    TestFailure(const std::string& msg, const char* file, int line)
        : std::runtime_error(msg + " at " + file + ":" + std::to_string(line)) {}
};

#define PIK_CHECK(cond) \
    do { \
        if (!(cond)) { \
            throw ::pikdraw::test::TestFailure(#cond, __FILE__, __LINE__); \
        } \
    } while (0)

#define PIK_CHECK_EQUAL(a, b) \
    do { \
        if (!((a) == (b))) { \
            std::ostringstream ss; \
            ss << #a " == " #b " failed: (" << (a) << " vs " << (b) << ")"; \
            throw ::pikdraw::test::TestFailure(ss.str(), __FILE__, __LINE__); \
        } \
    } while (0)

#define PIK_CHECK_NEAR(a, b, eps) \
    do { \
        if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(eps) || (eps) < 0 || std::abs((a) - (b)) > (eps)) { \
            std::ostringstream ss; \
            ss << "|" #a " - " #b "| <= " #eps " failed: (|" << (a) << " - " << (b) << "| = " << std::abs((a) - (b)) << " > " << (eps) << ")"; \
            throw ::pikdraw::test::TestFailure(ss.str(), __FILE__, __LINE__); \
        } \
    } while (0)

#define PIK_CHECK_THROWS(expr) \
    do { \
        bool threw = false; \
        try { (void)(expr); } catch (...) { threw = true; } \
        if (!threw) { \
            throw ::pikdraw::test::TestFailure(#expr " did not throw as expected", __FILE__, __LINE__); \
        } \
    } while (0)

struct TestCase {
    std::string name;
    std::function<void()> func;
};

inline std::vector<TestCase>& get_registry() {
    static std::vector<TestCase> cases;
    return cases;
}

struct AutoRegister {
    AutoRegister(const std::string& name, std::function<void()> f) {
        get_registry().push_back({name, std::move(f)});
    }
};

#define PIK_TEST_CASE(name) \
    static void name(); \
    static ::pikdraw::test::AutoRegister reg_##name(#name, name); \
    static void name()

} // namespace pikdraw::test
