#include "test_harness.hpp"
#include "pikdraw/color/preview/color_transform.hpp"
#include "pikdraw/color/preview/preview_cache.hpp"
#include "pikdraw/color/preview/bounded_lru_cache.hpp"
#include <string>
#include <type_traits>

using namespace pikdraw::color;

static_assert(!std::is_invocable_v<decltype(&clamp_tac), const CMYKColor&>,
              "TAC clamping requires an explicit output-condition limit");

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

PIKDRAW_COLOR_TEST(bounded_lru_cache_keeps_recent_entries_and_enforces_capacity) {
    BoundedLruCache<int, std::string> cache(3);
    cache.insert(1, "one");
    cache.insert(2, "two");
    cache.insert(3, "three");
    PIKDRAW_ASSERT(cache.size() == 3);

    const auto recently_used = cache.get(1);
    PIKDRAW_ASSERT(recently_used && *recently_used == "one");
    cache.insert(4, "four");
    PIKDRAW_ASSERT(cache.size() == 3);
    PIKDRAW_ASSERT(cache.get(2) == std::nullopt);
    PIKDRAW_ASSERT(cache.get(1) == std::optional<std::string>{"one"});
    PIKDRAW_ASSERT(cache.get(3) == std::optional<std::string>{"three"});
    PIKDRAW_ASSERT(cache.get(4) == std::optional<std::string>{"four"});

    cache.insert(1, "updated");
    PIKDRAW_ASSERT(cache.size() == 3);
    PIKDRAW_ASSERT(cache.get(1) == std::optional<std::string>{"updated"});
}

PIKDRAW_COLOR_TEST(ui_color_conversion_can_disable_black_shortcut) {
    auto near_black = ColorValue::make_rgb(0.01, 0.01, 0.01, 0.7);
    near_black.set_overprint({false, true});
    const auto converted = convert_color_to_mode(
        near_black, DocumentColorMode::Cmyk, nullptr, BlackGeneration::None);
    PIKDRAW_ASSERT(converted.color_space() == ColorSpace::Cmyk);
    PIKDRAW_ASSERT(converted.overprint().fill == false);
    PIKDRAW_ASSERT(converted.overprint().stroke == true);
    const auto& cmyk = std::get<CMYKColor>(converted.payload());
    PIKDRAW_ASSERT(cmyk.k < 1.0);
    const auto preset_report = convert_color_to_mode_report(
        near_black, DocumentColorMode::Cmyk, nullptr, BlackGeneration::PureK100);
    PIKDRAW_ASSERT(preset_report.changed);
    PIKDRAW_ASSERT(preset_report.black_policy_applied);
    PIKDRAW_ASSERT(preset_report.quality == ColorConversionQuality::PresetPolicy);

    const auto approximate_report = convert_color_to_mode_report(
        ColorValue::make_rgb(0.0, 0.5, 1.0), DocumentColorMode::Cmyk, nullptr,
        BlackGeneration::None, 100.0);
    PIKDRAW_ASSERT(approximate_report.changed);
    PIKDRAW_ASSERT(approximate_report.quality == ColorConversionQuality::Approximate);
    PIKDRAW_ASSERT(approximate_report.tac_limited);
    PIKDRAW_ASSERT(calculate_tac(std::get<CMYKColor>(approximate_report.color.payload())) <= 100.0001);

    const auto default_conversion = convert_color_to_mode_report(
        ColorValue::make_rgb(0.0, 0.5, 1.0), DocumentColorMode::Cmyk, nullptr,
        BlackGeneration::None);
    PIKDRAW_ASSERT(!default_conversion.tac_limited);
    PIKDRAW_ASSERT(default_conversion.quality == ColorConversionQuality::Approximate);

    const CMYKColor heavy_cmyk{1.0, 1.0, 1.0, 1.0};
    PIKDRAW_ASSERT(clamp_tac(heavy_cmyk, 400.0).c == heavy_cmyk.c);
    PIKDRAW_ASSERT(clamp_tac(heavy_cmyk, 320.0).k == 1.0);
    PIKDRAW_ASSERT(clamp_tac(heavy_cmyk, 0.0).c == heavy_cmyk.c);

    const auto already_rgb = ColorValue::make_rgb(0.2, 0.4, 0.6);
    const auto preserved_report = convert_color_to_mode_report(
        already_rgb, DocumentColorMode::Rgb);
    PIKDRAW_ASSERT(!preserved_report.changed);
    PIKDRAW_ASSERT(preserved_report.quality == ColorConversionQuality::Preserved);
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

PIKDRAW_COLOR_TEST(preview_cache_spot_alternate_and_alpha_are_keyed) {
    ColorPreviewCache cache;
    const auto red = ColorValue::make_spot("Brand Ink", 0.75, {1.0, 0.0, 0.0, 0.25});
    const auto blue = ColorValue::make_spot("Brand Ink", 0.75, {0.0, 0.0, 1.0, 0.8});
    const auto red_preview = cache.get_or_compute(red, 3);
    const auto blue_preview = cache.get_or_compute(blue, 3);
    PIKDRAW_ASSERT(cache.size() == 2);
    PIKDRAW_ASSERT(red_preview.r == 255 && red_preview.g == 63 && red_preview.b == 63 && red_preview.a == 63);
    PIKDRAW_ASSERT(blue_preview.r == 63 && blue_preview.g == 63 && blue_preview.b == 255 && blue_preview.a == 204);
}

PIKDRAW_COLOR_TEST(preview_cache_resolves_hash_collisions_with_full_color_equality) {
    auto normal = ColorValue::make_rgb(0.2, 0.4, 0.6);
    auto overprinting = normal;
    overprinting.set_overprint({true, false});

    ColorPreviewCache cache;
    const auto normal_preview = cache.get_or_compute(normal, 7);
    const auto overprint_preview = cache.get_or_compute(overprinting, 7);
    PIKDRAW_ASSERT(cache.size() == 2);
    PIKDRAW_ASSERT(normal_preview.r == overprint_preview.r &&
                   normal_preview.g == overprint_preview.g &&
                   normal_preview.b == overprint_preview.b &&
                   normal_preview.a == overprint_preview.a);
}

PIKDRAW_COLOR_TEST(preview_cache_has_a_bounded_entry_count) {
    ColorPreviewCache cache;
    for (std::size_t index = 0; index <= ColorPreviewCache::max_entries; ++index) {
        const double component = static_cast<double>(index) /
            static_cast<double>(ColorPreviewCache::max_entries + 1);
        (void)cache.get_or_compute(ColorValue::make_rgb(component, component, component), 1);
    }
    PIKDRAW_ASSERT(cache.size() == ColorPreviewCache::max_entries);
    cache.clear();
    PIKDRAW_ASSERT(cache.size() == 0);
}
