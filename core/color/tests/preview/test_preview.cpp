#include "test_harness.hpp"
#include "pikdraw/color/preview/color_transform.hpp"
#include "pikdraw/color/preview/preview_cache.hpp"

using namespace pikdraw::color;

PIKDRAW_COLOR_TEST(color_transform_to_preview_rgba) {
    auto red_rgb = ColorValue::make_rgb(1.0, 0.0, 0.0);
    PreviewRGBA red_preview = to_preview_rgba(red_rgb);
    PIKDRAW_ASSERT_NEAR(red_preview.r, 1.0f, 1e-4f);
    PIKDRAW_ASSERT_NEAR(red_preview.g, 0.0f, 1e-4f);
    PIKDRAW_ASSERT_NEAR(red_preview.b, 0.0f, 1e-4f);

    DeviceRGBA dev_red = red_preview.to_device();
    PIKDRAW_ASSERT(dev_red.r == 255);
    PIKDRAW_ASSERT(dev_red.g == 0);
    PIKDRAW_ASSERT(dev_red.b == 0);

    // CMYK pure cyan: c=1, m=0, y=0, k=0 -> r=0, g=1, b=1
    auto cyan_cmyk = ColorValue::make_cmyk(1.0, 0.0, 0.0, 0.0);
    PreviewRGBA cyan_preview = to_preview_rgba(cyan_cmyk);
    PIKDRAW_ASSERT_NEAR(cyan_preview.r, 0.0f, 1e-4f);
    PIKDRAW_ASSERT_NEAR(cyan_preview.g, 1.0f, 1e-4f);
    PIKDRAW_ASSERT_NEAR(cyan_preview.b, 1.0f, 1e-4f);
}

PIKDRAW_COLOR_TEST(preview_cache_invalidation) {
    ColorPreviewCache cache;
    auto color = ColorValue::make_rgb(0.5, 0.5, 0.5);

    uint64_t gen1 = 1;
    DeviceRGBA c1 = cache.get_or_compute(color, gen1);
    PIKDRAW_ASSERT(cache.size() == 1);

    // Same generation hit
    DeviceRGBA c2 = cache.get_or_compute(color, gen1);
    PIKDRAW_ASSERT(cache.size() == 1);
    PIKDRAW_ASSERT(c1.r == c2.r);

    // Next generation
    uint64_t gen2 = 2;
    DeviceRGBA c3 = cache.get_or_compute(color, gen2);
    PIKDRAW_ASSERT(cache.size() == 2);
    PIKDRAW_ASSERT(c3.r == c1.r);
    PIKDRAW_ASSERT(c3.g == c1.g);
    PIKDRAW_ASSERT(c3.b == c1.b);
    PIKDRAW_ASSERT(c3.a == c1.a);
}
