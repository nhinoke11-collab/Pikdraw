#include "../src/shell/native_style_dialog_math.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

using namespace pikdraw::desktop;
using namespace pikdraw::desktop::style_dialog_math;

void test_style_dialog_hsv_hsl_math() {
    // Pure Red
    const auto red = hsv_to_rgb(0.0, 1.0, 1.0);
    assert(std::abs(red.r - 1.0) < 1e-6);
    assert(std::abs(red.g - 0.0) < 1e-6);
    assert(std::abs(red.b - 0.0) < 1e-6);

    const auto hsv_red = to_hsv(red);
    assert(std::abs(hsv_red.h - 0.0) < 1e-4);
    assert(std::abs(hsv_red.s - 1.0) < 1e-4);
    assert(std::abs(hsv_red.v - 1.0) < 1e-4);

    // Pure Green in HSL
    const auto green = hsl_to_rgb(120.0, 1.0, 0.5);
    assert(std::abs(green.r - 0.0) < 1e-6);
    assert(std::abs(green.g - 1.0) < 1e-6);
    assert(std::abs(green.b - 0.0) < 1e-6);

    const auto hsl_green = to_hsl(green);
    assert(std::abs(hsl_green.h - 120.0) < 1e-4);
    assert(std::abs(hsl_green.s - 1.0) < 1e-4);
    assert(std::abs(hsl_green.l - 0.5) < 1e-4);

    // Cyan
    const auto cyan = hsv_to_rgb(180.0, 1.0, 1.0);
    assert(std::abs(cyan.r - 0.0) < 1e-6);
    assert(std::abs(cyan.g - 1.0) < 1e-6);
    assert(std::abs(cyan.b - 1.0) < 1e-6);

    // Grayscale preservation of hue
    const auto black = pikdraw::color::RGBColor{0.0, 0.0, 0.0, 1.0};
    const auto black_hsv = to_hsv(black, 191.3);
    assert(std::abs(black_hsv.h - 191.3) < 1e-4);
    assert(std::abs(black_hsv.v - 0.0) < 1e-4);

    // DIB pixel channel packaging
    const uint32_t px = dib_pixel({1.0, 0.5, 0.25, 1.0});
    const uint32_t expected = (255 << 16) | (128 << 8) | 64;
    assert(px == expected);
}

void test_style_dialog_number_formatting_and_parsing() {
    // Formatting removes trailing zeros and rounds cleanly
    assert(format_channel_number(191.2500729, 1) == L"191.3");
    assert(format_channel_number(88.00000, 1) == L"88");
    assert(format_channel_number(88.62876272, 1) == L"88.6");
    assert(format_channel_number(100.0, 0) == L"100");
    assert(format_channel_number(255.0, 0) == L"255");

    // Parsing accepts both period and comma
    const auto val1 = parse_number(L"191.3", 360.0);
    assert(val1.has_value());
    assert(std::abs(*val1 - 191.3) < 1e-6);

    const auto val2 = parse_number(L"88,6", 100.0);
    assert(val2.has_value());
    assert(std::abs(*val2 - 88.6) < 1e-6);

    // Out of range or invalid numbers return nullopt
    assert(!parse_number(L"-5", 100.0).has_value());
    assert(!parse_number(L"400", 360.0).has_value());
    assert(!parse_number(L"invalid", 100.0).has_value());
}

void test_style_dialog_lab_and_models() {
    // Red in Lab (matches CorelDRAW Image 2: L: 53-54, a: 80, b: 67-69)
    const auto red = pikdraw::color::RGBColor{1.0, 0.0, 0.0, 1.0};
    const auto red_lab = rgb_to_lab(red);
    assert(std::round(red_lab.l) >= 53.0 && std::round(red_lab.l) <= 54.0);
    assert(std::round(red_lab.a) == 80.0);
    assert(std::round(red_lab.b) >= 67.0 && std::round(red_lab.b) <= 69.0);

    const auto red_recovered = lab_to_rgb(red_lab.l, red_lab.a, red_lab.b);
    assert(std::abs(red_recovered.r - 1.0) < 0.02);
    assert(std::abs(red_recovered.g - 0.0) < 0.02);
    assert(std::abs(red_recovered.b - 0.0) < 0.02);
}

void test_style_dialog_hex_and_cmyk() {
    const auto cyan = pikdraw::color::RGBColor{0.0, 1.0, 1.0, 1.0};
    assert(format_hex_color(cyan) == L"#00FFFF");
    const auto parsed = parse_hex_color(L"#00FFFF");
    assert(parsed.has_value());
    assert(std::abs(parsed->r - 0.0) < 1e-4);
    assert(std::abs(parsed->g - 1.0) < 1e-4);
    assert(std::abs(parsed->b - 1.0) < 1e-4);

    const auto cmyk = rgb_to_cmyk(cyan);
    assert(std::abs(cmyk[0] - 1.0) < 1e-4);
    assert(std::abs(cmyk[1] - 0.0) < 1e-4);
    assert(std::abs(cmyk[2] - 0.0) < 1e-4);
    assert(std::abs(cmyk[3] - 0.0) < 1e-4);
}

void test_style_dialog_slider_coordinate_mapping() {
    constexpr int plane_left = 16, plane_right = 206;
    constexpr int plane_w = plane_right - plane_left;
    static_assert(plane_w == 190);

    // Coordinate mapping with (width - 1) gives exact 0.0 at left, exact 1.0 at right
    const auto map_x = [](int x) {
        return std::clamp(static_cast<double>(x - plane_left) / (plane_w - 1), 0.0, 1.0);
    };

    assert(std::abs(map_x(16) - 0.0) < 1e-9);
    assert(std::abs(map_x(205) - 1.0) < 1e-9);
    assert(std::abs(map_x(206) - 1.0) < 1e-9); // clamped outside right
    assert(std::abs(map_x(10) - 0.0) < 1e-9);  // clamped outside left

    // Round-trip marker coordinate
    for (int px = plane_left; px < plane_right; ++px) {
        const double sat = map_x(px);
        const int marker_x = plane_left + static_cast<int>(std::lround(sat * (plane_w - 1)));
        assert(marker_x == px); // Exact 1:1 match without coordinate slip/drift
    }

    // Hue mapping
    constexpr int hue_left = 16, hue_right = 206;
    constexpr int hue_w = hue_right - hue_left;
    const auto map_hue = [](int x) {
        const double fraction = std::clamp(static_cast<double>(x - hue_left) / (hue_w - 1), 0.0, 1.0);
        return fraction * 360.0;
    };
    assert(std::abs(map_hue(16) - 0.0) < 1e-9);
    assert(std::abs(map_hue(205) - 360.0) < 1e-9);
    for (int hx = hue_left; hx < hue_right; ++hx) {
        const double h = map_hue(hx);
        const int recovered_x = hue_left + static_cast<int>(std::lround(h / 360.0 * (hue_w - 1)));
        assert(recovered_x == hx); // 1:1 match
    }
}

int main() {
    std::cout << "=== Running Style Dialog Tests ===\n";
    test_style_dialog_hsv_hsl_math();
    test_style_dialog_number_formatting_and_parsing();
    test_style_dialog_lab_and_models();
    test_style_dialog_hex_and_cmyk();
    test_style_dialog_slider_coordinate_mapping();
    std::cout << "All Style Dialog Tests PASSED.\n";
    return 0;
}
