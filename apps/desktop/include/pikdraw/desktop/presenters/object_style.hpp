#pragma once
#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include <array>

namespace pikdraw::desktop {
struct EditorStyle {
    color::ColorValue fill{color::RGBColor{0.0, 0.65, 0.8, 1.0}};
    color::ColorValue stroke{color::RGBColor{0.0, 0.0, 0.0, 1.0}};
    double width_mm{.5};
    bool has_fill{true};
    bool has_stroke{true};
};
struct SelectionStyle {
    EditorStyle values;
    size_t count{0};
    bool mixed_fill{false}, mixed_stroke{false}, mixed_width{false};
    bool mixed_fill_enabled{false}, mixed_stroke_enabled{false};
    bool all_paths{true};
};
SelectionStyle selection_style(const document::DocumentSnapshot &snapshot,
                               const std::vector<document::ObjectId> &ids, const EditorStyle &defaults);
std::string color_description(const color::ColorValue &value);
const std::array<color::ColorValue, 18> &editor_palette(bool cmyk);
} // namespace pikdraw::desktop
