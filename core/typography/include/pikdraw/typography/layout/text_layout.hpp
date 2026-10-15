#pragma once

#include "pikdraw/geometry/primitives/bounds.hpp"
#include "pikdraw/typography/shaping/shaper.hpp"
#include <vector>
#include <string>

namespace pikdraw::typography {

enum class TextAlignment {
    Left,
    Center,
    Right,
    Justify
};

struct LayoutOptions {
    double max_width_mm{0.0}; // 0 = single unbounded line
    double line_spacing_ratio{1.2};
    TextAlignment alignment{TextAlignment::Left};
};

struct LayoutLine {
    ShapedRun run;
    geometry::Point2D baseline_origin;
    geometry::Rect2D line_bounds;
};

class TextLayout {
public:
    TextLayout() = default;

    void add_line(LayoutLine line);

    [[nodiscard]] const std::vector<LayoutLine>& lines() const noexcept { return lines_; }
    [[nodiscard]] geometry::Rect2D bounds() const noexcept { return bounds_; }
    [[nodiscard]] size_t line_count() const noexcept { return lines_.size(); }

    static TextLayout layout_text(
        std::string_view utf8_text,
        const FontIdentity& font,
        const LayoutOptions& options = LayoutOptions{}
    );

private:
    std::vector<LayoutLine> lines_;
    geometry::Rect2D bounds_{};
};

} // namespace pikdraw::typography
