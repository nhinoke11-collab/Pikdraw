#pragma once

#include "pikdraw/typography/layout/text_layout.hpp"
#include <string>
#include <string_view>

namespace pikdraw::typography {

struct TextFlowSplitResult {
    std::string fitted_text;
    std::string overflow_text;
    bool has_overflow{false};
    size_t split_byte_offset{0};
};

/// Splits text across a frame height boundary.
/// If frame_height_mm <= 0, no overflow splitting occurs (fitted_text == text, has_overflow == false).
[[nodiscard]] TextFlowSplitResult split_text_flow(
    std::string_view utf8_text,
    const FontIdentity& font,
    const LayoutOptions& options,
    double frame_height_mm
);

} // namespace pikdraw::typography
