#include "pikdraw/production/measurement/ciede2000.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pikdraw::production::measurement {
namespace {
constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kDegrees = 180.0 / kPi;
constexpr double kRadians = kPi / 180.0;

double normalized_hue(double radians) noexcept {
    double degrees = std::fmod(radians * kDegrees, 360.0);
    if (degrees < 0.0) degrees += 360.0;
    return degrees;
}

double seventh(double value) noexcept {
    const double square = value * value;
    const double fourth = square * square;
    return fourth * square * value;
}
}

double delta_e_2000(const LabValue& first, const LabValue& second) noexcept {
    if (!std::isfinite(first.l) || !std::isfinite(first.a) || !std::isfinite(first.b) ||
        !std::isfinite(second.l) || !std::isfinite(second.a) || !std::isfinite(second.b))
        return std::numeric_limits<double>::quiet_NaN();
    const double c1 = std::hypot(first.a, first.b);
    const double c2 = std::hypot(second.a, second.b);
    const double c_bar = (c1 + c2) / 2.0;
    const double c_bar_7 = seventh(c_bar);
    const double g = 0.5 * (1.0 - std::sqrt(c_bar_7 / (c_bar_7 + seventh(25.0))));
    const double a1p = (1.0 + g) * first.a;
    const double a2p = (1.0 + g) * second.a;
    const double c1p = std::hypot(a1p, first.b);
    const double c2p = std::hypot(a2p, second.b);
    const double h1p = c1p == 0.0 ? 0.0 : normalized_hue(std::atan2(first.b, a1p));
    const double h2p = c2p == 0.0 ? 0.0 : normalized_hue(std::atan2(second.b, a2p));

    const double delta_lp = second.l - first.l;
    const double delta_cp = c2p - c1p;
    double delta_hp = 0.0;
    if (c1p * c2p != 0.0) {
        const double difference = h2p - h1p;
        if (std::abs(difference) <= 180.0) delta_hp = difference;
        else if (difference > 180.0) delta_hp = difference - 360.0;
        else delta_hp = difference + 360.0;
    }
    const double delta_big_hp = 2.0 * std::sqrt(c1p * c2p) *
        std::sin((delta_hp / 2.0) * kRadians);
    const double l_bar_p = (first.l + second.l) / 2.0;
    const double c_bar_p = (c1p + c2p) / 2.0;
    double h_bar_p = h1p + h2p;
    if (c1p * c2p == 0.0) h_bar_p = h1p + h2p;
    else if (std::abs(h1p - h2p) <= 180.0) h_bar_p = (h1p + h2p) / 2.0;
    else if (h1p + h2p < 360.0) h_bar_p = (h1p + h2p + 360.0) / 2.0;
    else h_bar_p = (h1p + h2p - 360.0) / 2.0;

    const double t = 1.0 - 0.17 * std::cos((h_bar_p - 30.0) * kRadians) +
        0.24 * std::cos((2.0 * h_bar_p) * kRadians) +
        0.32 * std::cos((3.0 * h_bar_p + 6.0) * kRadians) -
        0.20 * std::cos((4.0 * h_bar_p - 63.0) * kRadians);
    const double l_offset = l_bar_p - 50.0;
    const double s_l = 1.0 + 0.015 * l_offset * l_offset /
        std::sqrt(20.0 + l_offset * l_offset);
    const double s_c = 1.0 + 0.045 * c_bar_p;
    const double s_h = 1.0 + 0.015 * c_bar_p * t;
    const double delta_theta = 30.0 * std::exp(-std::pow((h_bar_p - 275.0) / 25.0, 2.0));
    const double c_bar_p_7 = seventh(c_bar_p);
    const double r_c = 2.0 * std::sqrt(c_bar_p_7 / (c_bar_p_7 + seventh(25.0)));
    const double r_t = -r_c * std::sin(2.0 * delta_theta * kRadians);
    const double l_term = delta_lp / s_l;
    const double c_term = delta_cp / s_c;
    const double h_term = delta_big_hp / s_h;
    const double squared = l_term * l_term + c_term * c_term + h_term * h_term +
        r_t * c_term * h_term;
    return squared >= 0.0 ? std::sqrt(squared)
                          : std::numeric_limits<double>::quiet_NaN();
}

} // namespace pikdraw::production::measurement
