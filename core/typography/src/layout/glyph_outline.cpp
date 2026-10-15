#include "pikdraw/typography/layout/glyph_outline.hpp"

namespace pikdraw::typography {

geometry::Path2D text_to_path_outlines(const TextLayout& layout) {
    geometry::Path2D path;

    for (const auto& line : layout.lines()) {
        double cur_x = line.baseline_origin.x;
        const double base_y = line.baseline_origin.y;

        for (const auto& glyph : line.run.glyphs) {
            if (glyph.x_advance_mm > 0.0 && glyph.glyph_id > 32) {
                // Vector bounding outline approximation for glyph
                const double gw = glyph.x_advance_mm * 0.9;
                const double gh = line.run.ascent_mm;
                const double gx = cur_x + glyph.offset_x_mm;
                const double gy = base_y - gh + glyph.offset_y_mm;

                geometry::Path2D gp = geometry::Path2D::make_rect(gx, gy, gw, gh);
                for (const auto& el : gp.elements()) {
                    if (el.verb == geometry::PathVerb::MoveTo) path.move_to(el.pt);
                    else if (el.verb == geometry::PathVerb::LineTo) path.line_to(el.pt);
                    else if (el.verb == geometry::PathVerb::Close) path.close();
                }
            }
            cur_x += glyph.x_advance_mm;
        }
    }

    return path;
}

} // namespace pikdraw::typography
