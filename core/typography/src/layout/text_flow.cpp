#include "pikdraw/typography/layout/text_flow.hpp"
#include <algorithm>

namespace pikdraw::typography {

TextFlowSplitResult split_text_flow(
    std::string_view utf8_text,
    const FontIdentity& font,
    const LayoutOptions& options,
    double frame_height_mm
) {
    TextFlowSplitResult result;
    if (utf8_text.empty() || frame_height_mm <= 0.0) {
        result.fitted_text = std::string(utf8_text);
        result.overflow_text.clear();
        result.has_overflow = false;
        result.split_byte_offset = utf8_text.size();
        return result;
    }

    auto layout = TextLayout::layout_text(utf8_text, font, options);
    const auto& lines = layout.lines();
    if (lines.empty()) {
        result.fitted_text = std::string(utf8_text);
        result.split_byte_offset = utf8_text.size();
        return result;
    }

    size_t first_overflow_line_idx = lines.size();
    for (size_t i = 0; i < lines.size(); ++i) {
        const auto& line = lines[i];
        double bottom = line.line_bounds.y + line.line_bounds.height;
        if (bottom > frame_height_mm) {
            first_overflow_line_idx = i;
            break;
        }
    }

    if (first_overflow_line_idx >= lines.size()) {
        result.fitted_text = std::string(utf8_text);
        result.overflow_text.clear();
        result.has_overflow = false;
        result.split_byte_offset = utf8_text.size();
        return result;
    }

    size_t split_offset = lines[first_overflow_line_idx].source_byte_offset;
    if (split_offset > utf8_text.size()) {
        split_offset = utf8_text.size();
    }

    result.split_byte_offset = split_offset;
    result.fitted_text = std::string(utf8_text.substr(0, split_offset));
    result.overflow_text = std::string(utf8_text.substr(split_offset));
    result.has_overflow = !result.overflow_text.empty();
    return result;
}

} // namespace pikdraw::typography
