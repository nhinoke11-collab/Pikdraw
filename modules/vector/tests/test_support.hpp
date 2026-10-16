#pragma once
#include <cmath>
#include <iostream>
#include <stdexcept>
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c))                                                                                            \
            throw std::runtime_error("Failed line " + std::to_string(__LINE__) + ": " #c);                   \
    } while (false)
inline bool near(double a, double b) {
    return std::abs(a - b) < 1e-8;
}
template <class F> int run(F tests) {
    try {
        tests();
        std::cout << "PASS (Release checks active)\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
