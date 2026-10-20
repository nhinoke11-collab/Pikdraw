#pragma once
#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pikdraw::desktop {
struct EditorStyle {
    color::ColorValue fill{color::RGBColor{0.0, 0.65, 0.8, 1.0}};
    color::ColorValue stroke{color::RGBColor{0.0, 0.0, 0.0, 1.0}};
    double width_mm{0.0};
    bool has_fill{true};
    bool has_stroke{false};
};
struct SelectionStyle {
    EditorStyle values;
    size_t count{0};
    bool mixed_fill{false}, mixed_stroke{false}, mixed_width{false};
    bool mixed_fill_enabled{false}, mixed_stroke_enabled{false};
    bool all_paths{true};
};
struct SelectionEffect {
    document::EffectParameters parameters{document::GaussianBlurEffect{}};
    uint32_t schema_version{document::kBasicEffectNodeSchemaVersion};
    uint32_t schema_minor{document::kBasicEffectNodeSchemaMinor};
    bool mixed_enabled{false};
    bool enabled{true};
    bool mixed_sigma_x{false};
    bool mixed_sigma_y{false};
    bool mixed_offset_x{false};
    bool mixed_offset_y{false};
    bool mixed_opacity{false};
    bool mixed_color{false};
    bool mixed_shadow_blend_mode{false};
};

struct SelectionAppearance {
    double object_opacity{1.0};
    double fill_opacity{1.0};
    double stroke_opacity{1.0};
    size_t selected_count{0};
    size_t supported_count{0};
    bool mixed_object_opacity{false};
    bool mixed_fill_opacity{false};
    bool mixed_stroke_opacity{false};
    bool all_path_text{true};
    bool all_paths{true};
    bool contains_locked{false};
    document::BlendMode blend_mode{document::BlendMode::Normal};
    bool mixed_blend_mode{false};
    bool effect_stack_compatible{true};
    std::vector<SelectionEffect> effects;

    [[nodiscard]] bool can_edit_object_opacity() const noexcept {
        return selected_count != 0 && all_path_text &&
               supported_count == selected_count && !contains_locked;
    }

    [[nodiscard]] bool can_edit_appearance() const noexcept {
        return can_edit_object_opacity();
    }
    [[nodiscard]] bool can_edit_fill_opacity() const noexcept {
        return can_edit_appearance();
    }
    [[nodiscard]] bool can_edit_stroke_opacity() const noexcept {
        return selected_count != 0 && all_paths &&
               supported_count == selected_count && !contains_locked;
    }
    [[nodiscard]] bool can_edit_effect(size_t index) const noexcept {
        return can_edit_appearance() && effect_stack_compatible && index < effects.size();
    }
};

SelectionStyle selection_style(const document::DocumentSnapshot &snapshot,
                               const std::vector<document::ObjectId> &ids, const EditorStyle &defaults);
SelectionAppearance selection_appearance(const document::DocumentSnapshot &snapshot,
                                           const std::vector<document::ObjectId> &ids);
std::string color_description(const color::ColorValue &value);
const std::array<color::ColorValue, 18> &editor_palette(bool cmyk);
} // namespace pikdraw::desktop
