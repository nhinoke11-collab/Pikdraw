#pragma once

#include "pikdraw/typography/shaping/font_identity.hpp"
#include <cstdint>
#include <string_view>
#include <vector>

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
    FontIdentity requested_font;
    bool is_fallback{false};
    std::string warning_message;
};

// Real installed font shaping, LTR single-face runs. font records actual face;
// editable source retains its requested identity. Invalid input throws invalid_argument;
// unavailable backend, mixed-font/directional runs throw runtime_error.
[[nodiscard]] ShapedRun shape_text(std::string_view utf8_text, const FontIdentity &font);

} // namespace pikdraw::typography
