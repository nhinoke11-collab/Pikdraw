#pragma once

#include "pikdraw/geometry/curves/path.hpp"
#include "pikdraw/geometry/primitives/point.hpp"
#include "pikdraw/geometry/primitives/transform.hpp"
#include "pikdraw/typography/shaping/font_identity.hpp"
#include "pikdraw/typography/layout/text_layout.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace pikdraw::typography {

struct TextOnPathOptions {
    double start_offset_mm{0.0};
    double baseline_offset_mm{0.0};
    bool reversed{false};
    TextAlignment alignment{TextAlignment::Left};
};

struct PlacedGlyphOnPath {
    geometry::Point2D position{0.0, 0.0};
    double rotation_radians{0.0};
    double advance_mm{0.0};
    bool is_visible{true};
    geometry::Path2D transformed_outline;
};

struct TextOnPathLayoutResult {
    std::vector<PlacedGlyphOnPath> glyphs;
    geometry::Path2D combined_outlines;
    geometry::Rect2D bounds;
    double path_length_mm{0.0};
    double text_length_mm{0.0};
    bool fits_on_path{true};
};

/// Computes placement of text along an arbitrary 2D vector path.
[[nodiscard]] TextOnPathLayoutResult layout_text_on_path(
    const geometry::Path2D& path,
    std::string_view utf8_text,
    const FontIdentity& font,
    const TextOnPathOptions& options = TextOnPathOptions{}
);

} // namespace pikdraw::typography
