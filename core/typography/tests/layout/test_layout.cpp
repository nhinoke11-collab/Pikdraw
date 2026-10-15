#include "test_harness.hpp"
#include "pikdraw/typography/layout/text_layout.hpp"
#include "pikdraw/typography/layout/caret_navigation.hpp"
#include "pikdraw/typography/layout/glyph_outline.hpp"

using namespace pikdraw::typography;

PIKDRAW_TYPO_TEST(multiline_text_layout) {
    const std::string multiline = "Line 1: PikDraw\nLine 2: Vector Editor\nLine 3: 2026";
    FontIdentity font = FontIdentity::make_default();

    LayoutOptions opts;
    opts.alignment = TextAlignment::Left;
    opts.line_spacing_ratio = 1.5;

    TextLayout layout = TextLayout::layout_text(multiline, font, opts);
    PIKDRAW_ASSERT(layout.line_count() == 3);
    PIKDRAW_ASSERT(layout.bounds().width > 0.0);
    PIKDRAW_ASSERT(layout.bounds().height > font.size_mm() * 2.0);
}

PIKDRAW_TYPO_TEST(vietnamese_grapheme_cluster_navigation) {
    // "Biển" in UTF-8
    const std::string text = "Bi\u1EC3n"; // B (1 byte), i (1 byte), ển (3 bytes), n (1 byte)
    size_t c0 = 0;
    size_t c1 = next_grapheme_cluster(text, c0); // after 'B'
    PIKDRAW_ASSERT(c1 == 1);

    size_t c2 = next_grapheme_cluster(text, c1); // after 'i'
    PIKDRAW_ASSERT(c2 == 2);

    size_t c3 = next_grapheme_cluster(text, c2); // after 'ể'
    PIKDRAW_ASSERT(c3 == 5); // 2 + 3 bytes

    size_t c4 = next_grapheme_cluster(text, c3); // after 'n'
    PIKDRAW_ASSERT(c4 == 6);

    // Prev navigation
    PIKDRAW_ASSERT(prev_grapheme_cluster(text, 6) == 5);
    PIKDRAW_ASSERT(prev_grapheme_cluster(text, 5) == 2);
}

PIKDRAW_TYPO_TEST(convert_text_to_path_outlines) {
    const std::string sample = "PikDraw";
    FontIdentity font = FontIdentity::make_default();
    TextLayout layout = TextLayout::layout_text(sample, font);

    pikdraw::geometry::Path2D path = text_to_path_outlines(layout);
    PIKDRAW_ASSERT(!path.empty());
    PIKDRAW_ASSERT(path.bounds().width > 0.0);
    PIKDRAW_ASSERT(path.bounds().height > 0.0);
}
