#pragma once

#include "pikdraw/geometry/curves/path.hpp"
#include "pikdraw/typography/layout/text_layout.hpp"

namespace pikdraw::typography {

/// Converts a text layout into vector path outlines for manufacturing or vector effects
[[nodiscard]] geometry::Path2D text_to_path_outlines(const TextLayout& layout, bool underline = false, bool strikethrough = false);

} // namespace pikdraw::typography
