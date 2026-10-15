#include "test_harness.hpp"
#include "pikdraw/color/values/color_value.hpp"

using namespace pikdraw::color;

PIKDRAW_COLOR_TEST(rgb_and_cmyk_validation) {
    RGBColor valid_rgb{1.0, 0.5, 0.2, 1.0};
    PIKDRAW_ASSERT(valid_rgb.is_valid());

    RGBColor invalid_rgb{1.5, 0.0, 0.0, 1.0};
    PIKDRAW_ASSERT(!invalid_rgb.is_valid());

    CMYKColor valid_cmyk{0.0, 1.0, 1.0, 0.0, 1.0}; // Pure red in CMYK
    PIKDRAW_ASSERT(valid_cmyk.is_valid());

    CMYKColor invalid_cmyk{-0.1, 0.0, 0.0, 0.0, 1.0};
    PIKDRAW_ASSERT(!invalid_cmyk.is_valid());
}

PIKDRAW_COLOR_TEST(spot_color_and_overprint_preservation) {
    auto cut_contour = ColorValue::make_spot("CutContour", 1.0, RGBColor{1.0, 0.0, 1.0, 1.0});
    cut_contour.set_overprint(OverprintConfig{false, true});

    PIKDRAW_ASSERT(cut_contour.color_space() == ColorSpace::Spot);
    PIKDRAW_ASSERT(cut_contour.is_valid());
    PIKDRAW_ASSERT(cut_contour.overprint().stroke == true);
    PIKDRAW_ASSERT(cut_contour.overprint().fill == false);

    // Copy preservation
    ColorValue copied = cut_contour;
    PIKDRAW_ASSERT(copied.color_space() == ColorSpace::Spot);
    PIKDRAW_ASSERT(copied.overprint().stroke == true);

    const auto* spot = std::get_if<SpotColor>(&copied.payload());
    PIKDRAW_ASSERT(spot != nullptr);
    PIKDRAW_ASSERT(spot->spot_name == "CutContour");
    PIKDRAW_ASSERT_NEAR(spot->tint, 1.0, 1e-9);
}
