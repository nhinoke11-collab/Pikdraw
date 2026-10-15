#pragma once

#include "pikdraw/typography/shaping/font_identity.hpp"
#include <string_view>
#include <vector>
#include <cstdint>

namespace pikdraw::typography {

struct GlyphPosition {
    uint32_t glyph_id{0};
    size_t cluster_byte_offset{0};
    double x_advance_mm{0.0};
    double y_advance_mm{0.0};
    double offset_x_mm{0.0};
    double offset_y_mm{0.0};
};

struct ShapedRun {
    FontIdentity font;
    std::string text_utf8;
    std::vector<GlyphPosition> glyphs;
    double total_advance_mm{0.0};
    double ascent_mm{0.0};
    double descent_mm{0.0};
};

[[nodiscard]] ShapedRun shape_text(std::string_view utf8_text, const FontIdentity& font);

} // namespace pikdraw::typography
