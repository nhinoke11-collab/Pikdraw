#include "pikdraw/typography/shaping/font_resolver.hpp"
#include "pikdraw/typography/shaping/shaper.hpp"
#include "test_harness.hpp"
#include <limits>
using namespace pikdraw::typography;
namespace {
FontIdentity arial(double pt = 12) {
    return {"Arial", "ArialMT", 400, false, pt};
}
template <class F> bool rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        return true;
    }
    return false;
}
} // namespace
PIKDRAW_TYPO_TEST(font_identity_conversion_and_postscript) {
    auto font = arial(72);
    PIKDRAW_ASSERT_NEAR(font.size_mm(), 25.4, 1e-9);
    auto other = font;
    other.postscript_name = "DifferentFace";
    PIKDRAW_ASSERT(font != other);
}
PIKDRAW_TYPO_TEST(real_font_resolver_and_missing_font) {
    auto &resolver = FontResolver::instance();
    PIKDRAW_ASSERT(resolver.is_font_available("Arial"));
    PIKDRAW_ASSERT(!resolver.is_font_available("PikDrawDefinitelyAbsentFontXYZ"));
    const auto exact = resolver.resolve(arial());
    PIKDRAW_ASSERT(!exact.is_fallback);
    FontIdentity missing{"PikDrawDefinitelyAbsentFontXYZ", "Absent", 400, false, 12};
    const auto fallback = resolver.resolve(missing);
    PIKDRAW_ASSERT(fallback.requested == missing);
    PIKDRAW_ASSERT(fallback.is_fallback);
    PIKDRAW_ASSERT(resolver.is_font_available(fallback.actual.family_name));
    PIKDRAW_ASSERT(!fallback.warning_message.empty());
    const auto shaped = shape_text("PikDraw", missing);
    PIKDRAW_ASSERT(shaped.requested_font == missing);
    PIKDRAW_ASSERT(shaped.is_fallback);
    PIKDRAW_ASSERT(!shaped.warning_message.empty());
    PIKDRAW_ASSERT(shaped.font == fallback.actual);
    PIKDRAW_ASSERT(rejects([&] { resolver.register_available_font("PikDrawDefinitelyAbsentFontXYZ"); }));
    PIKDRAW_ASSERT(rejects([&] { (void)resolver.is_font_available(std::string("Arial\0X", 7)); }));
}
PIKDRAW_TYPO_TEST(real_glyph_ids_width_and_vietnamese_equivalence) {
    const auto wide = shape_text("WWW", arial());
    const auto narrow = shape_text("III", arial());
    PIKDRAW_ASSERT(wide.total_advance_mm > narrow.total_advance_mm * 2);
    PIKDRAW_ASSERT(wide.glyphs.front().glyph_id != static_cast<uint32_t>('W'));
    const auto nfc = shape_text("Bi\u1ec3n hi\u1ec7u \u00c1nh S\u00e1ng", arial());
    const auto nfd = shape_text("Bie\u0302\u0309n hie\u0323\u0302u A\u0301nh Sa\u0301ng", arial());
    PIKDRAW_ASSERT(nfc.text_utf8 != nfd.text_utf8);
    PIKDRAW_ASSERT_NEAR(nfc.total_advance_mm, nfd.total_advance_mm, 0.001);
    PIKDRAW_ASSERT(nfc.glyphs.size() == nfd.glyphs.size());
    for (size_t i = 0; i < nfc.glyphs.size(); ++i)
        PIKDRAW_ASSERT(nfc.glyphs[i].glyph_id == nfd.glyphs[i].glyph_id);
}
PIKDRAW_TYPO_TEST(strict_input_and_bounded_backend_failures) {
    for (const std::string &invalid : {std::string("\xc0\xaf"), std::string("\xed\xa0\x80"),
                                      std::string("\xf4\x90\x80\x80"), std::string("\xe2\x82")})
        PIKDRAW_ASSERT(rejects([&] { (void)shape_text(invalid, arial()); }));
    auto invalid = arial();
    invalid.size_pt = std::numeric_limits<double>::quiet_NaN();
    PIKDRAW_ASSERT(rejects([&] { (void)shape_text("Test", invalid); }));
    PIKDRAW_ASSERT(rejects([&] { (void)shape_text("\u0645\u0631\u062d\u0628\u0627", arial()); }));
    PIKDRAW_ASSERT(rejects([&] { (void)shape_text("tab\tstop", arial()); }));
}
