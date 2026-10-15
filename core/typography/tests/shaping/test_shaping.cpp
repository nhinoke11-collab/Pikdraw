#include "test_harness.hpp"
#include "pikdraw/typography/shaping/font_identity.hpp"
#include "pikdraw/typography/shaping/font_resolver.hpp"
#include "pikdraw/typography/shaping/shaper.hpp"

using namespace pikdraw::typography;

PIKDRAW_TYPO_TEST(font_identity_conversion) {
    FontIdentity font{"Plus Jakarta Sans", "PlusJakartaSans-Bold", 700, false, 72.0};
    // 72 points must equal exactly 25.4 mm (1 inch)
    PIKDRAW_ASSERT_NEAR(font.size_mm(), 25.4, 1e-9);

    FontIdentity mono = FontIdentity::make_mono(36.0);
    PIKDRAW_ASSERT_NEAR(mono.size_mm(), 12.7, 1e-9);
}

PIKDRAW_TYPO_TEST(font_resolver_fallback) {
    auto& resolver = FontResolver::instance();
    FontIdentity available{"Plus Jakarta Sans", "PlusJakartaSans-Regular", 400, false, 12.0};
    ResolvedFont res_avail = resolver.resolve(available);
    PIKDRAW_ASSERT(!res_avail.is_fallback);

    FontIdentity missing{"NonExistentFontFamilyXYZ", "NonExistentFont", 400, false, 12.0};
    ResolvedFont res_miss = resolver.resolve(missing);
    PIKDRAW_ASSERT(res_miss.is_fallback);
    // Original requested identity must be preserved according to Contract C4
    PIKDRAW_ASSERT(res_miss.requested.family_name == "NonExistentFontFamilyXYZ");
    PIKDRAW_ASSERT(res_miss.actual.family_name == "Plus Jakarta Sans");
    PIKDRAW_ASSERT(!res_miss.warning_message.empty());
}

PIKDRAW_TYPO_TEST(vietnamese_nfc_nfd_shaping) {
    // Canonical test string from Contract C4 / M1 specifications:
    // "PikDraw — Biển hiệu Ánh Sáng"
    const std::string text_nfc = "PikDraw \u2014 Bi\u1EC3n hi\u1EC7u \u00C1nh S\u00E1ng";
    FontIdentity font = FontIdentity::make_default();

    ShapedRun run_nfc = shape_text(text_nfc, font);
    PIKDRAW_ASSERT(!run_nfc.glyphs.empty());
    PIKDRAW_ASSERT(run_nfc.total_advance_mm > 0.0);

    // Decomposed NFD with combining hook above U+0309 on 'e'
    // "Bie\u0309n"
    const std::string text_nfd = "Bi\u0065\u0309n";
    ShapedRun run_nfd = shape_text(text_nfd, font);

    // In NFD, combining mark U+0309 must have 0 horizontal advance
    bool found_combining = false;
    for (const auto& g : run_nfd.glyphs) {
        if (g.glyph_id == 0x0309) {
            found_combining = true;
            PIKDRAW_ASSERT_NEAR(g.x_advance_mm, 0.0, 1e-9);
        }
    }
    PIKDRAW_ASSERT(found_combining);
}
