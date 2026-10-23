#include "pikdraw/typography/layout/text_layout.hpp"
#include "pikdraw/typography/layout/caret_navigation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace pikdraw::typography {

void TextLayout::add_line(LayoutLine line) {
    bounds_ = lines_.empty() ? line.line_bounds : bounds_.united(line.line_bounds);
    lines_.push_back(std::move(line));
}

namespace {
void apply_letter_spacing(ShapedRun& run, double spacing_mm) {
    if (spacing_mm == 0.0 || run.glyphs.size() < 2) return;
    size_t start = 0;
    while (start < run.glyphs.size()) {
        size_t end = start + 1;
        const size_t cluster = run.glyphs[start].cluster_byte_offset;
        while (end < run.glyphs.size() && run.glyphs[end].cluster_byte_offset == cluster) ++end;
        if (end < run.glyphs.size()) {
            run.glyphs[end - 1].x_advance_mm += spacing_mm;
            run.total_advance_mm += spacing_mm;
        }
        start = end;
    }
}

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
        !std::isfinite(options.line_spacing_ratio) || options.line_spacing_ratio <= 0 ||
        !std::isfinite(options.letter_spacing_mm))
        throw std::invalid_argument("Invalid layout width or spacing");
    if (options.alignment != TextAlignment::Left && options.alignment != TextAlignment::Center &&
        options.alignment != TextAlignment::Right && options.alignment != TextAlignment::Justify)
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
            apply_letter_spacing(run, options.letter_spacing_mm);
            size_t length = paragraph_end - start;
            bool is_wrapped = false;
            if (options.max_width_mm > 0 && run.total_advance_mm > options.max_width_mm) {
                length = wrap_end(run, options.max_width_mm);
                // The wrap boundary must also be a source grapheme boundary.
                const size_t previous = prev_grapheme_cluster(run.text_utf8, length);
                const size_t boundary = next_grapheme_cluster(run.text_utf8, previous);
                if (boundary != length)
                    length = previous > 0 ? previous : boundary;
                run = shape_text(text.substr(start, length), font);
                apply_letter_spacing(run, options.letter_spacing_mm);
                is_wrapped = true;
            }
            double x = 0;
            if (options.max_width_mm > run.total_advance_mm) {
                const auto remaining = options.max_width_mm - run.total_advance_mm;
                if (options.alignment == TextAlignment::Center)
                    x = remaining / 2;
                else if (options.alignment == TextAlignment::Right)
                    x = remaining;
                else if (options.alignment == TextAlignment::Justify && is_wrapped && (start + length < paragraph_end)) {
                    std::vector<size_t> expandable_spaces;
                    size_t previous_cluster = std::numeric_limits<size_t>::max();
                    for (const auto& glyph : run.glyphs) {
                        const size_t offset = glyph.cluster_byte_offset;
                        if (offset == previous_cluster || offset >= run.text_utf8.size()) continue;
                        previous_cluster = offset;
                        if (run.text_utf8[offset] == ' ' && offset > 0 && offset + 1 < run.text_utf8.size() &&
                            run.text_utf8[offset - 1] != ' ' && run.text_utf8[offset + 1] != ' ')
                            expandable_spaces.push_back(offset);
                    }
                    std::sort(expandable_spaces.begin(), expandable_spaces.end());
                    const size_t space_count = expandable_spaces.size();
                    if (space_count > 0) {
                        const double extra_per_space = remaining / static_cast<double>(space_count);
                        for (auto& glyph : run.glyphs) {
                            if (std::binary_search(expandable_spaces.begin(), expandable_spaces.end(),
                                                   glyph.cluster_byte_offset)) {
                                glyph.x_advance_mm += extra_per_space;
                            }
                        }
                        run.total_advance_mm = options.max_width_mm;
                    }
                }
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
    if (options.max_width_mm == 0 && options.alignment != TextAlignment::Left) {
        // Unbounded point text aligns its shorter lines within the longest line.
        const double width = result.bounds_.width;
        TextLayout aligned;
        for (auto line : result.lines_) {
            const double remaining = width - line.run.total_advance_mm;
            const double shift = options.alignment == TextAlignment::Center ? remaining / 2 :
                options.alignment == TextAlignment::Right ? remaining : 0.0;
            line.baseline_origin.x += shift;
            line.line_bounds.x += shift;
            aligned.add_line(std::move(line));
        }
        return aligned;
    }
    return result;
}

} // namespace pikdraw::typography
