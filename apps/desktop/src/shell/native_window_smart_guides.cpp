#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/vector/gestures/selection_resize.hpp"
#include "native_window_length_input.hpp"
#include "../tools/drawing_tool_controller.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <variant>

namespace pikdraw::desktop {

void NativeWindow::update_layout_snap_preview(const document::DocumentSnapshot& snapshot,
                                              geometry::Point2D raw_delta,
                                              int resize_handle,
                                              bool preserve_aspect,
                                              bool shape_point) {
    using namespace vector::precision::snap;
    auto& controller = root().vector_controller();
    auto& gesture = controller.gesture();
    if ((!shape_point && (!gesture.is_active() || !drag_preview_bounds_)) || !drawing_tool_) {
        smart_guide_solution_ = {};
        smart_guide_acquisition_ = {};
        smart_guide_moving_bounds_.reset();
        return;
    }

    const auto apply_preview = [&](geometry::Point2D accepted_delta) {
        if (shape_point) {
            shape_snap_effective_world_ = geometry::Point2D{
                pointer_current_world_.x + accepted_delta.x,
                pointer_current_world_.y + accepted_delta.y};
            const auto preview = vector::ShapeFactory::constrained_drag_bounds(
                pointer_down_world_, *shape_snap_effective_world_, active_tool_ != 9 &&
                    ((shape_drag_modifiers_ & MK_SHIFT) != 0),
                (shape_drag_modifiers_ & MK_CONTROL) != 0);
            drag_preview_bounds_ = preview;
            smart_guide_moving_bounds_ = preview;
            smart_guide_solution_.accepted_delta = accepted_delta;
            return;
        }
        if (resize_handle >= 0 && resize_handle <= 7) {
            constexpr int horizontal[] = {-1, 0, 1, 1, 1, 0, -1, -1};
            constexpr int vertical[] = {1, 1, 1, 0, -1, -1, -1, 0};
            const int hx = horizontal[resize_handle];
            const int hy = vertical[resize_handle];
            if (preserve_aspect && hx != 0 && hy != 0) {
                const bool snapped_x = smart_guide_solution_.acquisition[0].has_value();
                const bool snapped_y = smart_guide_solution_.acquisition[1].has_value();
                if (snapped_x && !snapped_y && drag_preview_bounds_->width > 1e-9) {
                    accepted_delta.y = accepted_delta.x * hy * drag_preview_bounds_->height /
                                       (hx * drag_preview_bounds_->width);
                } else if (snapped_y && !snapped_x && drag_preview_bounds_->height > 1e-9) {
                    accepted_delta.x = accepted_delta.y * hx * drag_preview_bounds_->width /
                                       (hy * drag_preview_bounds_->height);
                }
            }
            smart_guide_solution_.accepted_delta = accepted_delta;
            const auto transform = vector::selection_resize_transform(
                *drag_preview_bounds_, resize_handle, accepted_delta, preserve_aspect);
            if (transform) {
                (void)gesture.update_transform(*transform);
                smart_guide_moving_bounds_ = transform->map_rect(*drag_preview_bounds_);
            }
        } else {
            (void)gesture.update_transform(
                geometry::Transform2D::translation(accepted_delta.x, accepted_delta.y));
            auto moving = *drag_preview_bounds_;
            moving.x += accepted_delta.x;
            moving.y += accepted_delta.y;
            smart_guide_moving_bounds_ = moving;
        }
    };

    const auto config = drawing_tool_->snap_config_snapshot();
    if (!config.master_enabled) {
        smart_guide_solution_ = {};
        smart_guide_solution_.accepted_delta = raw_delta;
        smart_guide_acquisition_ = {};
        apply_preview(raw_delta);
        return;
    }

    const auto page_id = snapshot.active_page_id();
    const bool artboard_mode = artboard_overview_enabled();
    auto selected_roots = shape_point ? std::vector<document::ObjectId>{}
        : snapshot.hierarchy().roots(controller.selection().selected_ids());
    std::sort(selected_roots.begin(), selected_roots.end());
    const auto isolation_owner = current_isolated_group_id().value_or(
        document::kInvalidObjectId);
    const bool target_policy_group = config.groups[static_cast<size_t>(SnapGroup::TargetPolicy)];
    const bool locked_as_reference = target_policy_group &&
        config.options[static_cast<size_t>(SnapOption::LockedAsReference)];
    const bool current_layer_only = target_policy_group &&
        config.options[static_cast<size_t>(SnapOption::CurrentLayerOnly)];
    const bool hidden_as_reference = target_policy_group &&
        config.options[static_cast<size_t>(SnapOption::HiddenLayers)];
    const int current_layer = active_layer_index();
    if (smart_guide_reference_revision_ != snapshot.revision() ||
        smart_guide_reference_page_ != page_id ||
        smart_guide_reference_artboard_mode_ != artboard_mode ||
        smart_guide_reference_selection_roots_ != selected_roots ||
        smart_guide_reference_isolation_ != isolation_owner ||
        smart_guide_reference_active_layer_ != current_layer ||
        smart_guide_reference_locked_ != locked_as_reference ||
        smart_guide_reference_current_layer_only_ != current_layer_only ||
        smart_guide_reference_hidden_ != hidden_as_reference) {
        smart_guide_references_.clear();
        const std::unordered_set<document::ObjectId> moving_roots(selected_roots.begin(), selected_roots.end());
        for (const auto& [id, object] : snapshot.objects().all()) {
            (void)object;
            const auto* entry = snapshot.hierarchy().find(id);
            if (!entry || entry->parent != document::kInvalidObjectId || !entry->bounds ||
                (!entry->visible && !hidden_as_reference) || (entry->locked && !locked_as_reference) ||
                (current_layer_only && entry->layer != active_layer_index()))
                continue;
            if (isolation_owner != document::kInvalidObjectId &&
                !snapshot.hierarchy().contains(isolation_owner, id))
                continue;
            const bool moving = std::any_of(moving_roots.begin(), moving_roots.end(), [&](auto root_id) {
                return snapshot.hierarchy().contains(root_id, id);
            });
            if (moving) continue;
            smart_guide_references_.push_back({id, *entry->bounds});
        }
        smart_guide_reference_revision_ = snapshot.revision();
        smart_guide_reference_page_ = page_id;
        smart_guide_reference_artboard_mode_ = artboard_mode;
        smart_guide_reference_selection_roots_ = selected_roots;
        smart_guide_reference_isolation_ = isolation_owner;
        smart_guide_reference_active_layer_ = current_layer;
        smart_guide_reference_locked_ = locked_as_reference;
        smart_guide_reference_current_layer_only_ = current_layer_only;
        smart_guide_reference_hidden_ = hidden_as_reference;
    }

    const auto page_snap_enabled = config.groups[static_cast<size_t>(SnapGroup::PageGridGuidesConstruction)] &&
        (config.options[static_cast<size_t>(SnapOption::PageEdge)] ||
         config.options[static_cast<size_t>(SnapOption::PageCenter)]);
    LayoutSnapRequest request;
    request.references = smart_guide_references_;
    if (page_snap_enabled) {
        if (artboard_overview_enabled() && !snapshot.active_page().artboards.empty()) {
            for (const auto& artboard : snapshot.active_page().artboards)
                request.references.push_back({artboard.id,
                    {artboard.x_mm, artboard.y_mm, artboard.width_mm, artboard.height_mm},
                    LayoutSnapReferenceKind::Artboard});
        } else {
            const auto& page = snapshot.active_page().layout;
            request.references.push_back({page_id,
                {0.0, 0.0, page.width_mm, page.height_mm}, LayoutSnapReferenceKind::Page});
        }
    }

    if (shape_point) {
        constexpr double point_extent = 1e-6;
        request.moving_bounds = {pointer_current_world_.x - point_extent,
            pointer_current_world_.y - point_extent, point_extent * 2.0, point_extent * 2.0};
        request.raw_delta = {};
        request.spacing_enabled = false;
    } else {
        request.moving_bounds = *drag_preview_bounds_;
        request.raw_delta = raw_delta;
    }
    if (resize_handle >= 0 && resize_handle <= 7) {
        constexpr int horizontal[] = {-1, 0, 1, 1, 1, 0, -1, -1};
        constexpr int vertical[] = {1, 1, 1, 0, -1, -1, -1, 0};
        const int hx = horizontal[resize_handle];
        const int hy = vertical[resize_handle];
        request.moving_feature_mask_x = hx == 0 ? 0 : static_cast<uint8_t>(1u << (hx < 0 ? 0 : 2));
        request.moving_feature_mask_y = hy == 0 ? 0 : static_cast<uint8_t>(1u << (hy < 0 ? 0 : 2));
        request.spacing_enabled = false;
        if (preserve_aspect && hx != 0 && hy != 0) {
            const double sx = std::abs(raw_delta.x / std::max(1e-9, drag_preview_bounds_->width));
            const double sy = std::abs(raw_delta.y / std::max(1e-9, drag_preview_bounds_->height));
            if (sx >= sy) request.moving_feature_mask_y = 0;
            else request.moving_feature_mask_x = 0;
        }
    }
    if (show_guidelines_ && config.groups[static_cast<size_t>(SnapGroup::PageGridGuidesConstruction)] &&
        config.options[static_cast<size_t>(SnapOption::Guide)]) {
        request.guidelines.reserve(guidelines_.size());
        for (size_t index = 0; index < guidelines_.size(); ++index) {
            const auto& guideline = guidelines_[index];
            request.guidelines.push_back({guideline.id,
                guideline.orientation == GuidelineOrientation::Horizontal,
                guideline.position_mm, -1'000'000.0, 1'000'000.0});
        }
    }
    request.previous_acquisition = smart_guide_acquisition_;
    request.acquire_radius_dip = config.acquire_radius_dip;
    request.release_radius_dip = config.release_radius_dip;
    request.alignment_enabled = true;
    request.objects_enabled = config.smart_guides_enabled &&
        config.groups[static_cast<size_t>(SnapGroup::ObjectPoints)];
    request.page_enabled = page_snap_enabled;
    request.guidelines_enabled = show_guidelines_ &&
        config.groups[static_cast<size_t>(SnapGroup::PageGridGuidesConstruction)] &&
        config.options[static_cast<size_t>(SnapOption::Guide)];
    request.spacing_enabled = !shape_point && resize_handle < 0 &&
        config.smart_spacing_enabled &&
        config.groups[static_cast<size_t>(SnapGroup::AutomaticRelations)];
    const double dpi_scale = hwnd_ ? std::max(1.0, static_cast<double>(GetDpiForWindow(hwnd_)) / 96.0) : 1.0;
    const double dip_per_mm = std::max(0.01, root().renderer().camera().zoom()) / dpi_scale;
    request.dip_per_mm_x = dip_per_mm;
    request.dip_per_mm_y = dip_per_mm;

    smart_guide_solution_ = solve_layout_snap(request);
    smart_guide_acquisition_ = smart_guide_solution_.acquisition;
    apply_preview(smart_guide_solution_.accepted_delta);
    if (smart_guide_solution_.snapped && !smart_guide_solution_.guides.empty()) {
        const auto& guide = smart_guide_solution_.guides.front();
        if (guide.key.relation == LayoutSnapRelation::EqualSpacing) {
            const auto gap = format_mm_display_utf8(guide.spacing_mm, display_length_unit_, 5);
            status_msg_ = language_ == UILanguage::Vietnamese
                ? std::string("Khoảng cách bằng nhau ") + (guide.key.vertical_axis ? "Y: " : "X: ") + gap
                : std::string("Equal spacing ") + (guide.key.vertical_axis ? "Y: " : "X: ") + gap;
        } else {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? (guide.key.relation == LayoutSnapRelation::Contact
                    ? "Smart Guide · cạnh đối tượng đang chạm" : "Smart Guide · căn mép/tâm đối tượng")
                : (guide.key.relation == LayoutSnapRelation::Contact
                    ? "Smart Guide · object edges touch" : "Smart Guide · align object edge/center");
        }
    }
}

void NativeWindow::paint_smart_guide_overlay(HDC dc, const RECT& area) {
    using namespace vector::precision::snap;
    if (!smart_guide_solution_.snapped || !drag_preview_bounds_) return;
    const auto& camera = root().renderer().camera();
    const COLORREF guide_color = theme_ == ThemeMode::CyberDark
        ? RGB(105, 193, 255) : RGB(0, 112, 210);
    const auto pen = CreatePen(PS_DOT, 1, guide_color);
    const auto prior_pen = SelectObject(dc, pen);
    const auto prior_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));

    const auto to_screen = [&](geometry::Point2D world) {
        const auto point = camera.world_to_screen(world);
        return POINT{area.left + static_cast<int>(std::lround(point.x)),
                     area.top + static_cast<int>(std::lround(point.y))};
    };
    const auto moving = smart_guide_moving_bounds_.value_or([&] {
        auto bounds = *drag_preview_bounds_;
        bounds.x += smart_guide_solution_.accepted_delta.x;
        bounds.y += smart_guide_solution_.accepted_delta.y;
        return bounds;
    }());
    for (const auto& guide : smart_guide_solution_.guides) {
        if (guide.key.relation == LayoutSnapRelation::EqualSpacing) {
            const double cross = guide.key.vertical_axis ? moving.center().x : moving.center().y;
            const auto start = guide.key.vertical_axis
                ? to_screen({cross, guide.start_mm}) : to_screen({guide.start_mm, cross});
            const auto end = guide.key.vertical_axis
                ? to_screen({cross, guide.end_mm}) : to_screen({guide.end_mm, cross});
            MoveToEx(dc, start.x, start.y, nullptr);
            LineTo(dc, end.x, end.y);
            const int tick = 5;
            if (guide.key.vertical_axis) {
                MoveToEx(dc, start.x - tick, start.y, nullptr); LineTo(dc, start.x + tick, start.y);
                MoveToEx(dc, end.x - tick, end.y, nullptr); LineTo(dc, end.x + tick, end.y);
            } else {
                MoveToEx(dc, start.x, start.y - tick, nullptr); LineTo(dc, start.x, start.y + tick);
                MoveToEx(dc, end.x, end.y - tick, nullptr); LineTo(dc, end.x, end.y + tick);
            }
            continue;
        }
        const auto start = guide.key.vertical_axis
            ? to_screen({guide.start_mm, guide.coordinate_mm})
            : to_screen({guide.coordinate_mm, guide.start_mm});
        const auto end = guide.key.vertical_axis
            ? to_screen({guide.end_mm, guide.coordinate_mm})
            : to_screen({guide.coordinate_mm, guide.end_mm});
        MoveToEx(dc, start.x, start.y, nullptr);
        LineTo(dc, end.x, end.y);
    }
    SelectObject(dc, prior_brush);
    SelectObject(dc, prior_pen);
    DeleteObject(pen);

    const auto spacing = std::find_if(smart_guide_solution_.guides.begin(),
        smart_guide_solution_.guides.end(), [](const auto& guide) {
            return guide.key.relation == LayoutSnapRelation::EqualSpacing;
        });
    if (spacing != smart_guide_solution_.guides.end()) {
        const auto middle = spacing->key.vertical_axis
            ? to_screen({moving.center().x, (spacing->start_mm + spacing->end_mm) * 0.5})
            : to_screen({(spacing->start_mm + spacing->end_mm) * 0.5, moving.center().y});
        const std::wstring label = (spacing->key.vertical_axis ? L"Y-spacing: " : L"X-spacing: ") +
            format_mm_display(spacing->spacing_mm, display_length_unit_, 5);
        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, theme_ == ThemeMode::CyberDark ? RGB(15, 23, 42) : RGB(255, 255, 255));
        SetTextColor(dc, guide_color);
        TextOutW(dc, middle.x + 6, middle.y - 20, label.c_str(), static_cast<int>(label.size()));
        SetBkMode(dc, TRANSPARENT);
    }
}

} // namespace pikdraw::desktop
