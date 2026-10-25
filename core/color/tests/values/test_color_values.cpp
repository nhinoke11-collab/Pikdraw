#include "test_harness.hpp"
#include "pikdraw/color/values/color_value.hpp"
#include "pikdraw/color/preview/color_transform.hpp"

using namespace pikdraw::color;

PIKDRAW_COLOR_TEST(document_process_conversion_preserves_alpha_black_spot_and_overprint) {
    auto black = ColorValue::make_rgb(0, 0, 0, .45);
    black.set_overprint({true, true});
    const auto converted = convert_color_to_mode(black, DocumentColorMode::Cmyk);
    PIKDRAW_ASSERT((std::get<CMYKColor>(converted.payload()).approx_equal({0, 0, 0, 1, .45}, 0)));
    PIKDRAW_ASSERT(converted.overprint().fill && converted.overprint().stroke);
    const auto unmarked_black = convert_color_to_mode(
        ColorValue::make_rgb(0, 0, 0), DocumentColorMode::Cmyk);
    PIKDRAW_ASSERT((std::get<CMYKColor>(unmarked_black.payload()).approx_equal({0, 0, 0, 1, 1}, 0)));
    PIKDRAW_ASSERT(!unmarked_black.overprint().fill && !unmarked_black.overprint().stroke);
    const auto red = convert_color_to_mode(ColorValue::make_rgb(1, 0, 0, .7), DocumentColorMode::Cmyk);
    PIKDRAW_ASSERT((std::get<CMYKColor>(red.payload()).approx_equal({0, 1, 1, 0, .7}, 0)));
    const auto roundtrip = convert_color_to_mode(red, DocumentColorMode::Rgb);
    PIKDRAW_ASSERT((std::get<RGBColor>(roundtrip.payload()).approx_equal({1, 0, 0, .7}, 0)));
    const auto gray = convert_color_to_mode(GrayscaleColor{.25, .5}, DocumentColorMode::Cmyk);
    PIKDRAW_ASSERT((std::get<CMYKColor>(gray.payload()).approx_equal({0, 0, 0, .75, .5}, 0)));
    const auto gray_rgb = convert_color_to_mode(GrayscaleColor{.25, .5}, DocumentColorMode::Rgb);
    PIKDRAW_ASSERT((std::get<RGBColor>(gray_rgb.payload()).approx_equal({.25, .25, .25, .5}, 0)));
    auto spot = ColorValue{SpotColor{"CutContour", .32, {.13, .14, .15, .67}, true}};
    spot.set_overprint({false, true});
    for (auto mode : {DocumentColorMode::Rgb, DocumentColorMode::Cmyk}) {
        const auto value = convert_color_to_mode(spot, mode);
        const auto& result = std::get<SpotColor>(value.payload());
        PIKDRAW_ASSERT(result.spot_name == "CutContour" && result.tint == .32 && result.overprint);
        PIKDRAW_ASSERT(result.alternate_rgb.approx_equal({.13, .14, .15, .67}, 0));
        PIKDRAW_ASSERT(value.overprint().stroke && !value.overprint().fill);
    }
}

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
