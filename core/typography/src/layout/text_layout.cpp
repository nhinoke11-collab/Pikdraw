#include "pikdraw/typography/layout/text_layout.hpp"
#include <sstream>

namespace pikdraw::typography {

void TextLayout::add_line(LayoutLine line) {
    if (lines_.empty()) {
        bounds_ = line.line_bounds;
    } else {
        bounds_ = bounds_.united(line.line_bounds);
    }
    lines_.push_back(std::move(line));
}

TextLayout TextLayout::layout_text(
    std::string_view utf8_text,
    const FontIdentity& font,
    const LayoutOptions& options
) {
    TextLayout layout;
    if (utf8_text.empty()) {
        return layout;
    }

    const double line_height = font.size_mm() * options.line_spacing_ratio;
    double current_y = 0.0;

    std::stringstream ss{std::string(utf8_text)};
    std::string line_str;

    while (std::getline(ss, line_str, '\n')) {
        ShapedRun run = shape_text(line_str, font);

        double x_offset = 0.0;
        if (options.max_width_mm > run.total_advance_mm) {
            if (options.alignment == TextAlignment::Center) {
                x_offset = (options.max_width_mm - run.total_advance_mm) * 0.5;
            } else if (options.alignment == TextAlignment::Right) {
                x_offset = options.max_width_mm - run.total_advance_mm;
            }
        }

        LayoutLine line;
        line.run = std::move(run);
        line.baseline_origin = geometry::Point2D{x_offset, current_y + font.size_mm() * 0.8};
        line.line_bounds = geometry::Rect2D{
            x_offset,
            current_y,
            line.run.total_advance_mm,
            font.size_mm()
        };

        layout.add_line(std::move(line));
        current_y += line_height;
    }

    return layout;
}

} // namespace pikdraw::typography
