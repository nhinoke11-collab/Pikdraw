#include "pikdraw/typography/layout/text_layout.hpp"
#include "pikdraw/typography/layout/caret_navigation.hpp"
#include <cmath>
#include <stdexcept>
namespace pikdraw::typography {
void TextLayout::add_line(LayoutLine line) {
    bounds_ = lines_.empty() ? line.line_bounds : bounds_.united(line.line_bounds);
    lines_.push_back(std::move(line));
}
namespace {
size_t wrap_end(const ShapedRun &run, double max_width) {
    double width = 0;
    size_t end = 0;
    size_t word_break = 0;
    for (size_t i = 0; i < run.glyphs.size(); ++i) {
        width += run.glyphs[i].x_advance_mm;
        if (i + 1 < run.glyphs.size() &&
            run.glyphs[i + 1].cluster_byte_offset == run.glyphs[i].cluster_byte_offset)
            continue;
        const size_t next =
            i + 1 < run.glyphs.size() ? run.glyphs[i + 1].cluster_byte_offset : run.text_utf8.size();
        if (width > max_width && end > 0)
            return word_break > 0 ? word_break : end;
        end = next;
        if (end > 0 && run.text_utf8[end - 1] == ' ')
            word_break = end;
    }
    return end == 0 ? run.text_utf8.size() : end;
}
} // namespace
TextLayout TextLayout::layout_text(std::string_view text, const FontIdentity &font,
                                   const LayoutOptions &options) {
    if (!std::isfinite(options.max_width_mm) || options.max_width_mm < 0 ||
        !std::isfinite(options.line_spacing_ratio) || options.line_spacing_ratio <= 0)
        throw std::invalid_argument("Invalid layout width or spacing");
    if (options.alignment != TextAlignment::Left && options.alignment != TextAlignment::Center &&
        options.alignment != TextAlignment::Right)
        throw std::invalid_argument("Unsupported text alignment");
    TextLayout result;
    double baseline = 0;
    size_t paragraph_start = 0;
    do {
        size_t paragraph_end = text.find_first_of("\r\n", paragraph_start);
        if (paragraph_end == std::string_view::npos)
            paragraph_end = text.size();
        size_t start = paragraph_start;
        do {
            auto run = shape_text(text.substr(start, paragraph_end - start), font);
            size_t length = paragraph_end - start;
            if (options.max_width_mm > 0 && run.total_advance_mm > options.max_width_mm) {
                length = wrap_end(run, options.max_width_mm);
                // The wrap boundary must also be a source grapheme boundary.
                const size_t previous = prev_grapheme_cluster(run.text_utf8, length);
                const size_t boundary = next_grapheme_cluster(run.text_utf8, previous);
                if (boundary != length)
                    length = previous > 0 ? previous : boundary;
                run = shape_text(text.substr(start, length), font);
            }
            double x = 0;
            if (options.max_width_mm > run.total_advance_mm) {
                const auto remaining = options.max_width_mm - run.total_advance_mm;
                if (options.alignment == TextAlignment::Center)
                    x = remaining / 2;
                if (options.alignment == TextAlignment::Right)
                    x = remaining;
            }
            const double ascent = run.ascent_mm;
            const double height = run.ascent_mm + run.descent_mm;
            const double width = run.total_advance_mm;
            result.add_line({std::move(run), {x, baseline}, {x, baseline - ascent, width, height}, start});
            baseline += height * options.line_spacing_ratio;
            start += length;
        } while (start < paragraph_end);
        if (paragraph_end == text.size())
            break;
        paragraph_start = paragraph_end + 1;
        if (text[paragraph_end] == '\r' && paragraph_start < text.size() && text[paragraph_start] == '\n')
            ++paragraph_start;
    } while (paragraph_start <= text.size());
    return result;
}
} // namespace pikdraw::typography
