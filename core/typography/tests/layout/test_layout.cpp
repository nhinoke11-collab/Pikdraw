#include "pikdraw/typography/layout/caret_navigation.hpp"
#include "pikdraw/typography/layout/glyph_outline.hpp"
#include "pikdraw/typography/layout/text_layout.hpp"
#include "test_harness.hpp"
#include <limits>
using namespace pikdraw::typography;
namespace {
FontIdentity arial() {
    return {"Arial", "ArialMT", 400, false, 12};
}
} // namespace
PIKDRAW_TYPO_TEST(multiline_source_offsets_trailing_empty_and_baseline) {
    const auto layout = TextLayout::layout_text("one\r\ntwo\n", arial());
    PIKDRAW_ASSERT(layout.line_count() == 3);
    PIKDRAW_ASSERT(layout.lines()[0].source_byte_offset == 0);
    PIKDRAW_ASSERT(layout.lines()[1].source_byte_offset == 5);
    PIKDRAW_ASSERT(layout.lines()[2].source_byte_offset == 9);
    PIKDRAW_ASSERT_NEAR(layout.lines()[0].baseline_origin.y, 0, 1e-9);
    PIKDRAW_ASSERT(layout.lines()[1].baseline_origin.y > 0);
    const auto hit = hit_test_caret(layout, {1000, layout.lines()[1].baseline_origin.y});
    PIKDRAW_ASSERT(hit.byte_index == 8);
}
PIKDRAW_TYPO_TEST(vietnamese_navigation_roundtrip_and_cluster_caret) {
    const std::string text = "Bie\u0302\u0309n";
    PIKDRAW_ASSERT(next_grapheme_cluster(text, 2) == 7);
    PIKDRAW_ASSERT(prev_grapheme_cluster(text, 7) == 2);
    PIKDRAW_ASSERT(prev_grapheme_cluster(text, 1000) == 7);
    PIKDRAW_ASSERT(next_grapheme_cluster("\r\nX", 0) == 2);
    PIKDRAW_ASSERT(next_grapheme_cluster("Bi\u1ec3n", 1) == 2);
    PIKDRAW_ASSERT(next_grapheme_cluster("Bi\u1ec3n", 2) == 5);
    const auto layout = TextLayout::layout_text(text, arial());
    const auto hit = hit_test_caret(layout, {1000, 0});
    PIKDRAW_ASSERT(hit.byte_index == text.size());
    for (double x = 0; x < layout.bounds().width; x += 0.1) {
        const auto index = hit_test_caret(layout, {x, 0}).byte_index;
        PIKDRAW_ASSERT(index == 0 || index == 1 || index == 2 || index == 7 || index == 8);
    }
}
PIKDRAW_TYPO_TEST(real_O_outline_has_two_curved_closed_contours) {
    const auto path = text_to_path_outlines(TextLayout::layout_text("O", arial()));
    size_t starts = 0, closes = 0, curves = 0;
    for (const auto &element : path.elements()) {
        if (element.verb == pikdraw::geometry::PathVerb::MoveTo)
            ++starts;
        if (element.verb == pikdraw::geometry::PathVerb::Close)
            ++closes;
        if (element.verb == pikdraw::geometry::PathVerb::CubicTo)
            ++curves;
    }
    PIKDRAW_ASSERT(starts == 2);
    PIKDRAW_ASSERT(closes == 2);
    PIKDRAW_ASSERT(curves >= 8);
    PIKDRAW_ASSERT(path.bounds().width > 0);
    PIKDRAW_ASSERT(path.bounds().y < 0);
    PIKDRAW_ASSERT(text_to_path_outlines(TextLayout::layout_text(" ", arial())).empty());
}
PIKDRAW_TYPO_TEST(wrap_keeps_all_source_bytes_and_rejects_fake_justify) {
    LayoutOptions options;
    options.max_width_mm = shape_text("hello ", arial()).total_advance_mm + 0.1;
    const std::string source = "hello world again";
    const auto layout = TextLayout::layout_text(source, arial(), options);
    PIKDRAW_ASSERT(layout.line_count() >= 3);
    std::string reconstructed;
    for (const auto &line : layout.lines()) {
        PIKDRAW_ASSERT(line.source_byte_offset == reconstructed.size());
        reconstructed += line.run.text_utf8;
    }
    PIKDRAW_ASSERT(reconstructed == source);
    options.alignment = TextAlignment::Justify;
    bool rejected = false;
    try {
        (void)TextLayout::layout_text(source, arial(), options);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    PIKDRAW_ASSERT(rejected);
}
PIKDRAW_TYPO_TEST(outlines_reject_invalid_run_placement_without_partial_result) {
    auto line = TextLayout::layout_text("O", arial()).lines().front();
    line.run.glyphs.front().x_advance_mm = std::numeric_limits<double>::max();
    TextLayout invalid;
    invalid.add_line(line);
    bool rejected = false;
    try {
        (void)text_to_path_outlines(invalid);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    PIKDRAW_ASSERT(rejected);
}
