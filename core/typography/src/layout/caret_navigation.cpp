#include "pikdraw/typography/layout/caret_navigation.hpp"

namespace pikdraw::typography {

namespace {

bool is_utf8_continuation_byte(uint8_t b) noexcept {
    return (b & 0xC0) == 0x80;
}

} // namespace

size_t next_grapheme_cluster(std::string_view text, size_t current_byte_offset) {
    if (current_byte_offset >= text.size()) return text.size();

    size_t next = current_byte_offset + 1;
    // Skip continuation bytes of current code point
    while (next < text.size() && is_utf8_continuation_byte(static_cast<uint8_t>(text[next]))) {
        next++;
    }

    // Check if next character is a combining mark (0xCC..0xCD in UTF-8), if so consume it too
    while (next < text.size()) {
        const auto c = static_cast<uint8_t>(text[next]);
        if (c == 0xCC || c == 0xCD) {
            next += 2; // Combining mark is 2 bytes in UTF-8
        } else {
            break;
        }
    }
    return std::min(next, text.size());
}

size_t prev_grapheme_cluster(std::string_view text, size_t current_byte_offset) {
    if (current_byte_offset == 0) return 0;

    size_t prev = current_byte_offset - 1;
    while (prev > 0 && is_utf8_continuation_byte(static_cast<uint8_t>(text[prev]))) {
        prev--;
    }
    return prev;
}

CaretHitResult hit_test_caret(
    const TextLayout& layout,
    const geometry::Point2D& local_point_mm
) {
    CaretHitResult result;
    if (layout.lines().empty()) return result;

    // Find closest line vertically
    const auto& lines = layout.lines();
    size_t best_line_idx = 0;
    double min_y_dist = 1e9;

    for (size_t i = 0; i < lines.size(); ++i) {
        const double line_center_y = lines[i].line_bounds.center().y;
        const double dist = std::abs(local_point_mm.y - line_center_y);
        if (dist < min_y_dist) {
            min_y_dist = dist;
            best_line_idx = i;
        }
    }

    const auto& target_line = lines[best_line_idx];
    result.caret_height_mm = target_line.line_bounds.height;

    // Scan glyph positions horizontally
    double current_x = target_line.baseline_origin.x;
    size_t best_byte = 0;
    double best_x_pos = current_x;
    double min_x_dist = std::abs(local_point_mm.x - current_x);

    for (const auto& g : target_line.run.glyphs) {
        current_x += g.x_advance_mm;
        const double dist = std::abs(local_point_mm.x - current_x);
        if (dist < min_x_dist) {
            min_x_dist = dist;
            best_x_pos = current_x;
            best_byte = g.cluster_byte_offset;
        }
    }

    result.byte_index = best_byte;
    result.caret_position = geometry::Point2D{best_x_pos, target_line.line_bounds.y};
    return result;
}

} // namespace pikdraw::typography
