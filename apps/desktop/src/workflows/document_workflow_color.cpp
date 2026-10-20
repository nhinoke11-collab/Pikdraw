#include "pikdraw/desktop/workflows/document_workflow.hpp"
#include "pikdraw/document/commands/color_commands.hpp"
#include "pikdraw/document/model/object.hpp"
#include "pikdraw/vector/vector_controller.hpp"
#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace pikdraw::desktop {

namespace {
bool colors_match(const color::ColorValue& a, const color::ColorValue& b, double tolerance = 0.005) {
    if (a.color_space() != b.color_space()) return false;
    return std::visit([&](const auto& val_a) -> bool {
        using T = std::decay_t<decltype(val_a)>;
        if constexpr (std::is_same_v<T, color::RGBColor>) {
            const auto& val_b = std::get<color::RGBColor>(b.payload());
            return std::abs(val_a.r - val_b.r) <= tolerance &&
                   std::abs(val_a.g - val_b.g) <= tolerance &&
                   std::abs(val_a.b - val_b.b) <= tolerance &&
                   std::abs(val_a.a - val_b.a) <= tolerance;
        } else if constexpr (std::is_same_v<T, color::CMYKColor>) {
            const auto& val_b = std::get<color::CMYKColor>(b.payload());
            return std::abs(val_a.c - val_b.c) <= tolerance &&
                   std::abs(val_a.m - val_b.m) <= tolerance &&
                   std::abs(val_a.y - val_b.y) <= tolerance &&
                   std::abs(val_a.k - val_b.k) <= tolerance &&
                   std::abs(val_a.a - val_b.a) <= tolerance;
        } else if constexpr (std::is_same_v<T, color::SpotColor>) {
            const auto& val_b = std::get<color::SpotColor>(b.payload());
            return val_a.spot_name == val_b.spot_name &&
                   std::abs(val_a.tint - val_b.tint) <= tolerance;
        } else if constexpr (std::is_same_v<T, color::GrayscaleColor>) {
            const auto& val_b = std::get<color::GrayscaleColor>(b.payload());
            return std::abs(val_a.gray - val_b.gray) <= tolerance &&
                   std::abs(val_a.a - val_b.a) <= tolerance;
        }
        return false;
    }, a.payload());
}

struct ObjectColors {
    std::optional<color::ColorValue> fill;
    std::optional<color::ColorValue> stroke;
    bool has_stroke{false};
    std::vector<color::ColorSpace> spaces;
};

ObjectColors extract_colors(const document::ObjectVariant& var) {
    ObjectColors info;
    std::visit([&](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, document::PathObject>) {
            info.fill = item.fill;
            info.spaces.push_back(item.fill.color_space());
            if (item.has_stroke && item.stroke_width_mm > 0.0) {
                info.stroke = item.stroke;
                info.has_stroke = true;
                info.spaces.push_back(item.stroke.color_space());
            }
            if (item.fill_gradient) {
                for (const auto& s : item.fill_gradient->stops)
                    info.spaces.push_back(s.color.color_space());
            }
        } else if constexpr (std::is_same_v<T, document::TextObject>) {
            info.fill = item.fill;
            info.spaces.push_back(item.fill.color_space());
        }
    }, var);
    return info;
}
} // namespace

bool DocumentWorkflow::convert_document_color_mode(color::DocumentColorMode mode,
    std::shared_ptr<const color::IccPreview> icc) {
    const auto result = root_.document().execute_command(
        std::make_unique<document::ConvertDocumentColorModeCommand>(
            mode, color::default_document_profile(mode), std::move(icc)));
    if (result.success && !result.is_no_change) {
        cancel_effect_draft();
        clear_text_preview();
    }
    sync_status_bar();
    return result.success;
}

bool DocumentWorkflow::set_document_raster_dpi(int32_t dpi) {
    const auto result = root_.document().execute_command(
        std::make_unique<document::SetDocumentRasterDpiCommand>(dpi));
    if (result.success && !result.is_no_change) cancel_effect_draft();
    sync_status_bar();
    return result.success;
}

bool DocumentWorkflow::set_page_color_mode(document::PageId page_id, color::DocumentColorMode mode,
    std::shared_ptr<const color::IccPreview> icc) {
    const auto result = root_.document().execute_command(
        std::make_unique<document::SetPageColorModeCommand>(
            page_id, mode, color::default_document_profile(mode), std::move(icc)));
    if (result.success && !result.is_no_change) {
        cancel_effect_draft();
        clear_text_preview();
    }
    sync_status_bar();
    return result.success;
}

bool DocumentWorkflow::set_page_raster_dpi(document::PageId page_id, int32_t dpi) {
    const auto result = root_.document().execute_command(
        std::make_unique<document::SetPageRasterDpiCommand>(page_id, dpi));
    if (result.success && !result.is_no_change) cancel_effect_draft();
    sync_status_bar();
    return result.success;
}

bool DocumentWorkflow::convert_selected_objects_color_mode(color::DocumentColorMode mode,
    std::shared_ptr<const color::IccPreview> icc) {
    const auto selected = root_.vector_controller().selection().selected_ids();
    if (selected.empty()) return false;
    const auto result = root_.document().execute_command(
        std::make_unique<document::ConvertObjectsColorModeCommand>(
            selected, mode, color::default_document_profile(mode), std::move(icc)));
    if (result.success && !result.is_no_change) {
        cancel_effect_draft();
        clear_text_preview();
    }
    sync_status_bar();
    return result.success;
}

bool DocumentWorkflow::convert_active_page_color_mode(color::DocumentColorMode mode,
    std::shared_ptr<const color::IccPreview> icc) {
    return set_page_color_mode(root_.document().active_page_id(), mode, std::move(icc));
}

std::vector<document::ObjectId> DocumentWorkflow::find_objects_matching_color(
    const document::ObjectId* reference_id,
    const color::ColorValue* reference_color,
    SelectSameCriterion criterion) const {
    std::vector<document::ObjectId> matched;
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot) return matched;

    ObjectColors ref_info;
    if (reference_id) {
        if (const auto* obj = snapshot->objects().lookup_ref(*reference_id)) {
            ref_info = extract_colors(*obj);
        }
    } else if (reference_color) {
        ref_info.fill = *reference_color;
        ref_info.spaces.push_back(reference_color->color_space());
    }

    const auto& current_page = snapshot->page();
    for (const auto& layer : current_page.layers) {
        for (const auto id : layer.object_ids) {
            const auto* obj = snapshot->objects().lookup_ref(id);
            if (!obj) continue;
            const auto target_info = extract_colors(*obj);

            bool matches = false;
            switch (criterion) {
            case SelectSameCriterion::FillColor:
                if (ref_info.fill && target_info.fill) {
                    matches = colors_match(*target_info.fill, *ref_info.fill);
                }
                break;
            case SelectSameCriterion::StrokeColor:
                if (ref_info.has_stroke && target_info.has_stroke && ref_info.stroke && target_info.stroke) {
                    matches = colors_match(*target_info.stroke, *ref_info.stroke);
                }
                break;
            case SelectSameCriterion::FillAndStroke:
                if (ref_info.fill && target_info.fill && colors_match(*target_info.fill, *ref_info.fill)) {
                    if (ref_info.has_stroke == target_info.has_stroke) {
                        if (!ref_info.has_stroke) matches = true;
                        else if (ref_info.stroke && target_info.stroke) {
                            matches = colors_match(*target_info.stroke, *ref_info.stroke);
                        }
                    }
                }
                break;
            case SelectSameCriterion::ColorSpace:
                if (ref_info.fill && target_info.fill) {
                    matches = (target_info.fill->color_space() == ref_info.fill->color_space());
                } else if (!ref_info.spaces.empty() && !target_info.spaces.empty()) {
                    matches = (target_info.spaces.front() == ref_info.spaces.front());
                }
                break;
            case SelectSameCriterion::CmykOnly:
                matches = (target_info.fill && target_info.fill->color_space() == color::ColorSpace::Cmyk) ||
                          (!target_info.fill && target_info.stroke && target_info.stroke->color_space() == color::ColorSpace::Cmyk);
                break;
            case SelectSameCriterion::RgbOnly:
                matches = (target_info.fill && target_info.fill->color_space() == color::ColorSpace::Rgb) ||
                          (!target_info.fill && target_info.stroke && target_info.stroke->color_space() == color::ColorSpace::Rgb);
                break;
            case SelectSameCriterion::SpotOnly:
                matches = (target_info.fill && target_info.fill->color_space() == color::ColorSpace::Spot) ||
                          (!target_info.fill && target_info.stroke && target_info.stroke->color_space() == color::ColorSpace::Spot);
                break;
            }

            if (matches) matched.push_back(id);
        }
    }
    return matched;
}

bool DocumentWorkflow::select_same(SelectSameCriterion criterion) {
    const auto selected = root_.vector_controller().selection().selected_ids();
    const document::ObjectId* ref_id = nullptr;
    if (criterion != SelectSameCriterion::CmykOnly &&
        criterion != SelectSameCriterion::RgbOnly &&
        criterion != SelectSameCriterion::SpotOnly) {
        if (selected.empty()) return false;
        ref_id = &selected.front();
    }
    const auto matched = find_objects_matching_color(ref_id, nullptr, criterion);
    if (matched.empty()) return false;
    const auto snapshot = root_.document().current_snapshot();
    root_.vector_controller().set_selection(matched, *snapshot, vector::SelectionMode::Replace);
    sync_status_bar();
    return true;
}

bool DocumentWorkflow::select_by_color(const color::ColorValue& color, SelectSameCriterion criterion) {
    const auto matched = find_objects_matching_color(nullptr, &color, criterion);
    if (matched.empty()) return false;
    const auto snapshot = root_.document().current_snapshot();
    root_.vector_controller().set_selection(matched, *snapshot, vector::SelectionMode::Replace);
    sync_status_bar();
    return true;
}

} // namespace pikdraw::desktop
