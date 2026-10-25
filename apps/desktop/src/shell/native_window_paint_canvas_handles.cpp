#include "pikdraw/desktop/shell/native_window.hpp"

#include "native_window_drop_shadow_paint.hpp"
#include "../tools/gradient_tool_controller.hpp"

#include <cmath>
#include <variant>

namespace pikdraw::desktop {

#ifdef _WIN32

void NativeWindow::paint_canvas_interaction_handles(HDC dc, int width, int height) {
    const int saved_dc = SaveDC(dc);
    configure_ui_dc(dc);

    const bool dark = theme_ == ThemeMode::CyberDark;
    const COLORREF accent = dark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    const COLORREF text = dark ? RGB(241, 245, 249) : RGB(15, 23, 42);

    // Drop Shadow exposes a direct-manipulation handle while the effect is editable.
    const auto& selected_ids = root().vector_controller().selection().selected_ids();
    const auto snapshot = root().document().current_snapshot();
    if (active_dock_tab_ == 0 && show_style_inspector_ && selected_ids.size() == 1 &&
        snapshot && workflow().can_edit_selected_effect(property_effect_index_)) {
        const auto appearance = selection_appearance(*snapshot, selected_ids);
        if (appearance.can_edit_effect(property_effect_index_)) {
            const auto& selected_effect = appearance.effects[property_effect_index_];
            const auto* shadow = std::get_if<document::DropShadowEffect>(
                &selected_effect.parameters);
            const auto draft = workflow().effect_draft();
            if (draft && draft->editing_existing &&
                property_effect_index_ < appearance.effects.size()) {
                if (const auto* draft_shadow = std::get_if<document::DropShadowEffect>(
                        &draft->parameters))
                    shadow = draft_shadow;
            }
            const auto bounds = root().vector_controller().selection().bounds(*snapshot);
            if (shadow && bounds) {
                const auto center = bounds->center();
                const geometry::Point2D handle_world{
                    center.x + shadow->dx_mm, center.y + shadow->dy_mm};
                const auto area = canvas_rect(width, height);
                const auto center_screen = root().renderer().camera().world_to_screen(center);
                const auto handle_screen = root().renderer().camera().world_to_screen(handle_world);
                const int cx = area.left + static_cast<int>(std::lround(center_screen.x));
                const int cy = area.top + static_cast<int>(std::lround(center_screen.y));
                const int hx = area.left + static_cast<int>(std::lround(handle_screen.x));
                const int hy = area.top + static_cast<int>(std::lround(handle_screen.y));
                const HPEN pen = CreatePen(PS_SOLID, 2, accent);
                const HGDIOBJ old_pen = SelectObject(dc, pen);
                MoveToEx(dc, cx, cy, nullptr);
                LineTo(dc, hx, hy);
                const HBRUSH brush = CreateSolidBrush(text);
                const HGDIOBJ old_brush = SelectObject(dc, brush);
                Ellipse(dc, hx - 7, hy - 7, hx + 8, hy + 8);
                SelectObject(dc, old_brush);
                SelectObject(dc, old_pen);
                DeleteObject(brush);
                DeleteObject(pen);
            }
        }
    }

    if (active_tool_ == 13 && gradient_tool_ && gradient_tool_->active()) {
        const auto handles = gradient_tool_->tool().get_handles();
        if (!handles.empty()) {
            const auto area = canvas_rect(width, height);
            const HPEN axis_pen = CreatePen(PS_DASH, 1, accent);
            const HGDIOBJ old_axis_pen = SelectObject(dc, axis_pen);
            geometry::Point2D start{0, 0};
            geometry::Point2D end{0, 0};
            bool has_axis = false;
            for (const auto& handle : handles) {
                if (handle.kind == vector::tools::GradientHandleKind::LinearStart ||
                    handle.kind == vector::tools::GradientHandleKind::RadialCenter) {
                    start = handle.position_object;
                    has_axis = true;
                } else if (handle.kind == vector::tools::GradientHandleKind::LinearEnd ||
                           handle.kind == vector::tools::GradientHandleKind::RadialRadius) {
                    end = handle.position_object;
                }
            }
            if (has_axis) {
                const auto a = root().renderer().camera().world_to_screen(start);
                const auto b = root().renderer().camera().world_to_screen(end);
                MoveToEx(dc, area.left + static_cast<int>(std::lround(a.x)),
                         area.top + static_cast<int>(std::lround(a.y)), nullptr);
                LineTo(dc, area.left + static_cast<int>(std::lround(b.x)),
                       area.top + static_cast<int>(std::lround(b.y)));
            }
            SelectObject(dc, old_axis_pen);
            DeleteObject(axis_pen);

            for (const auto& handle : handles) {
                const auto point = root().renderer().camera().world_to_screen(
                    handle.position_object);
                const int x = area.left + static_cast<int>(std::lround(point.x));
                const int y = area.top + static_cast<int>(std::lround(point.y));
                COLORREF fill = text;
                if (handle.kind == vector::tools::GradientHandleKind::Stop)
                    fill = RGB(255, 200, 50);
                else if (handle.kind == vector::tools::GradientHandleKind::LinearStart ||
                         handle.kind == vector::tools::GradientHandleKind::RadialCenter)
                    fill = accent;
                const HBRUSH brush = CreateSolidBrush(fill);
                const HPEN pen = CreatePen(PS_SOLID, 1, RGB(20, 24, 33));
                const HGDIOBJ old_pen = SelectObject(dc, pen);
                const HGDIOBJ old_brush = SelectObject(dc, brush);
                if (handle.kind == vector::tools::GradientHandleKind::Stop) {
                    POINT points[4] = {{x, y - 5}, {x + 5, y}, {x, y + 5}, {x - 5, y}};
                    Polygon(dc, points, 4);
                } else {
                    Ellipse(dc, x - 5, y - 5, x + 6, y + 6);
                }
                SelectObject(dc, old_brush);
                SelectObject(dc, old_pen);
                DeleteObject(pen);
                DeleteObject(brush);
            }
        }
    }

    RestoreDC(dc, saved_dc);
}

#endif

}  // namespace pikdraw::desktop
