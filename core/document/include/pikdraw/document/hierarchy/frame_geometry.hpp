#pragma once
#include "pikdraw/document/model/object.hpp"
#include "pikdraw/typography/layout/glyph_outline.hpp"

namespace pikdraw::document {
// Transient geometry: the frame's original Path/Text object remains authoritative.
inline std::optional<PathObject> frame_geometry(const ObjectVariant& source) {
    if (const auto* path = std::get_if<PathObject>(&source)) {
        if (path->path.empty()) return std::nullopt;
        return *path;
    }
    if (const auto* text = std::get_if<TextObject>(&source)) {
        try {
            const auto layout = text->layout();
            for (const auto& line : layout.lines())
                if (line.run.is_fallback) return std::nullopt;
            PathObject path;
            path.id = text->id;
            path.path = typography::text_to_path_outlines(layout, text->underline, text->strikethrough);
            if (path.path.empty()) return std::nullopt;
            path.transform = text->transform *
                geometry::Transform2D::translation(text->position.x, text->position.y) *
                geometry::Transform2D::scale(1, -1);
            path.fill = text->fill;
            path.has_fill = true;
            path.has_stroke = false;
            path.appearance = text->appearance;
            return path;
        } catch (...) { return std::nullopt; }
    }
    return std::nullopt;
}
} // namespace pikdraw::document
