#include "pikdraw/typography/shaping/shaper.hpp"

namespace pikdraw::typography {

namespace {

// Helper to decode next UTF-8 codepoint and length in bytes
uint32_t decode_utf8(std::string_view s, size_t offset, size_t& out_bytes) {
    if (offset >= s.size()) {
        out_bytes = 0;
        return 0;
    }

    const auto c0 = static_cast<uint8_t>(s[offset]);
    if (c0 < 0x80) {
        out_bytes = 1;
        return c0;
    } else if ((c0 & 0xE0) == 0xC0) {
        out_bytes = (offset + 1 < s.size()) ? 2 : 1;
        if (out_bytes == 2) {
            const auto c1 = static_cast<uint8_t>(s[offset + 1]);
            return ((c0 & 0x1F) << 6) | (c1 & 0x3F);
        }
        return c0;
    } else if ((c0 & 0xF0) == 0xE0) {
        out_bytes = (offset + 2 < s.size()) ? 3 : 1;
        if (out_bytes == 3) {
            const auto c1 = static_cast<uint8_t>(s[offset + 1]);
            const auto c2 = static_cast<uint8_t>(s[offset + 2]);
            return ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
        }
        return c0;
    } else if ((c0 & 0xF8) == 0xF0) {
        out_bytes = (offset + 3 < s.size()) ? 4 : 1;
        if (out_bytes == 4) {
            const auto c1 = static_cast<uint8_t>(s[offset + 1]);
            const auto c2 = static_cast<uint8_t>(s[offset + 2]);
            const auto c3 = static_cast<uint8_t>(s[offset + 3]);
            return ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
        }
        return c0;
    }
    out_bytes = 1;
    return c0;
}

bool is_combining_mark(uint32_t cp) noexcept {
    // Unicode Combining Diacritical Marks: U+0300 - U+036F
    return cp >= 0x0300 && cp <= 0x036F;
}

} // namespace

ShapedRun shape_text(std::string_view utf8_text, const FontIdentity& font) {
    ShapedRun run;
    run.font = font;
    run.text_utf8 = std::string(utf8_text);

    const double em_mm = font.size_mm();
    run.ascent_mm = em_mm * 0.8;
    run.descent_mm = em_mm * 0.2;

    const bool is_mono = font.family_name.find("Mono") != std::string::npos;
    const double base_advance = is_mono ? (em_mm * 0.6) : (em_mm * 0.52);

    size_t offset = 0;
    double current_x = 0.0;

    while (offset < utf8_text.size()) {
        size_t cp_bytes = 0;
        const uint32_t cp = decode_utf8(utf8_text, offset, cp_bytes);

        GlyphPosition pos;
        pos.glyph_id = cp;
        pos.cluster_byte_offset = offset;

        if (is_combining_mark(cp)) {
            // Combining accents in NFD do not advance horizontally
            pos.x_advance_mm = 0.0;
            pos.offset_x_mm = -base_advance * 0.5; // Centered over base letter
        } else if (cp == ' ' || cp == '\t') {
            pos.x_advance_mm = is_mono ? base_advance : (em_mm * 0.3);
        } else if (cp == 'i' || cp == 'l' || cp == 'j' || cp == 't') {
            pos.x_advance_mm = is_mono ? base_advance : (em_mm * 0.32);
        } else if (cp == 'm' || cp == 'w' || cp == 'M' || cp == 'W') {
            pos.x_advance_mm = is_mono ? base_advance : (em_mm * 0.85);
        } else {
            pos.x_advance_mm = base_advance;
        }

        current_x += pos.x_advance_mm;
        run.glyphs.push_back(pos);
        offset += cp_bytes;
    }

    run.total_advance_mm = current_x;
    return run;
}

} // namespace pikdraw::typography
