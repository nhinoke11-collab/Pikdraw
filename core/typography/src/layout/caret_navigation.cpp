#include "pikdraw/typography/layout/caret_navigation.hpp"
#include "../shaping/system_font.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace pikdraw::typography {
namespace {
struct Scalar {
    uint32_t value;
    size_t byte;
    size_t end;
};
std::vector<Scalar> scalars(std::string_view text) {
#ifdef _WIN32
    (void)system_font::wide(text);
#else
    throw std::runtime_error("Unicode backend unavailable");
#endif
    std::vector<Scalar> result;
    for (size_t i = 0; i < text.size();) {
        const size_t begin = i;
        const auto lead = static_cast<uint8_t>(text[i++]);
        const int count = lead < 0x80 ? 0 : lead < 0xe0 ? 1 : lead < 0xf0 ? 2 : 3;
        uint32_t value = lead & (count == 0 ? 0x7fu : count == 1 ? 0x1fu : count == 2 ? 0x0fu : 0x07u);
        for (int j = 0; j < count; ++j)
            value = (value << 6) | (static_cast<uint8_t>(text[i++]) & 0x3fu);
        result.push_back({value, begin, i});
    }
    return result;
}
bool extend(uint32_t cp) {
    if ((cp >= 0xfe00 && cp <= 0xfe0f) || (cp >= 0xe0100 && cp <= 0xe01ef) ||
        (cp >= 0x1f3fb && cp <= 0x1f3ff))
        return true;
#ifdef _WIN32
    if (cp <= 0xffff) {
        WORD type = 0;
        const wchar_t unit = static_cast<wchar_t>(cp);
        if (GetStringTypeW(CT_CTYPE3, &unit, 1, &type))
            return (type & (C3_NONSPACING | C3_VOWELMARK)) != 0;
    }
#endif
    return false;
}
std::vector<size_t> boundaries(std::string_view text) {
    const auto chars = scalars(text);
    std::vector<size_t> result{0};
    size_t regional_count = 0;
    for (size_t i = 0; i < chars.size(); ++i) {
        const auto cp = chars[i].value;
        const bool regional = cp >= 0x1f1e6 && cp <= 0x1f1ff;
        bool join =
            i > 0 && (extend(cp) || cp == 0x200d || chars[i - 1].value == 0x200d ||
                      (chars[i - 1].value == '\r' && cp == '\n') || (regional && regional_count % 2 == 1));
        if (!join && chars[i].byte != 0)
            result.push_back(chars[i].byte);
        regional_count = regional ? regional_count + 1 : 0;
    }
    if (!text.empty())
        result.push_back(text.size());
    return result;
}
} // namespace
size_t next_grapheme_cluster(std::string_view text, size_t current) {
    const auto limits = boundaries(text);
    const auto next = std::upper_bound(limits.begin(), limits.end(), current);
    return next == limits.end() ? text.size() : *next;
}
size_t prev_grapheme_cluster(std::string_view text, size_t current) {
    const auto limits = boundaries(text);
    const auto previous = std::lower_bound(limits.begin(), limits.end(), std::min(current, text.size()));
    return previous == limits.begin() ? 0 : *std::prev(previous);
}
CaretHitResult hit_test_caret(const TextLayout &layout, const geometry::Point2D &point) {
    if (!std::isfinite(point.x) || !std::isfinite(point.y))
        throw std::invalid_argument("Invalid caret coordinate");
    CaretHitResult result;
    if (layout.lines().empty())
        return result;
    const LayoutLine *target = &layout.lines().front();
    double closest = std::numeric_limits<double>::infinity();
    for (const auto &line : layout.lines()) {
        const double distance = std::abs(point.y - line.line_bounds.center().y);
        if (distance < closest) {
            target = &line;
            closest = distance;
        }
    }
    double x = target->baseline_origin.x;
    double best = std::abs(point.x - x);
    result = {target->source_byte_offset, {x, target->line_bounds.y}, target->line_bounds.height};
    for (size_t i = 0; i < target->run.glyphs.size(); ++i) {
        const auto &glyph = target->run.glyphs[i];
        x += glyph.x_advance_mm;
        if (i + 1 < target->run.glyphs.size() &&
            target->run.glyphs[i + 1].cluster_byte_offset == glyph.cluster_byte_offset)
            continue;
        const size_t end = i + 1 < target->run.glyphs.size() ? target->run.glyphs[i + 1].cluster_byte_offset
                                                             : target->run.text_utf8.size();
        if (std::abs(point.x - x) < best) {
            best = std::abs(point.x - x);
            result.byte_index = target->source_byte_offset + end;
            result.caret_position.x = x;
        }
    }
    return result;
}
} // namespace pikdraw::typography
