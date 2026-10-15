#pragma once

#include "pikdraw/typography/layout/text_layout.hpp"
#include <string_view>
#include <vector>

namespace pikdraw::typography {

struct CaretHitResult {
    size_t byte_index{0};
    geometry::Point2D caret_position;
    double caret_height_mm{0.0};
};

/// Grapheme cluster boundary navigation
[[nodiscard]] size_t next_grapheme_cluster(std::string_view text, size_t current_byte_offset);
[[nodiscard]] size_t prev_grapheme_cluster(std::string_view text, size_t current_byte_offset);

/// Finds caret index nearest to a click coordinate in model space
[[nodiscard]] CaretHitResult hit_test_caret(
    const TextLayout& layout,
    const geometry::Point2D& local_point_mm
);

} // namespace pikdraw::typography
