#include "native_window_selection_geometry.hpp"
#include <iomanip>
#include <sstream>
#include <array>

namespace pikdraw::desktop {
#ifdef _WIN32
namespace {
void outline(HDC dc, RECT rect, COLORREF color, int style = PS_SOLID, int width = 1) {
    const auto pen = CreatePen(style, width, color);
    const auto old_pen = SelectObject(dc, pen);
    const auto old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
}
void fill_rect(HDC dc, RECT rect, COLORREF color) {
    const auto brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}
void marquee_wash(HDC dc, RECT rect, COLORREF color) {
    if (rect.right <= rect.left || rect.bottom <= rect.top)
        return;
    const auto source = CreateCompatibleDC(dc);
    const auto bitmap = CreateCompatibleBitmap(dc, 1, 1);
    const auto previous = SelectObject(source, bitmap);
    fill_rect(source, {0, 0, 1, 1}, color);
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 35, 0};
    AlphaBlend(dc, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, source, 0, 0, 1, 1,
               blend);
    SelectObject(source, previous);
    DeleteObject(bitmap);
    DeleteDC(source);
}
} // namespace

void NativeWindow::paint_selection_overlay(HDC dc) {
    const auto snapshot = root().document().current_snapshot();
    const auto &selection = root().vector_controller().selection();
    const auto &camera = root().renderer().camera();
    const auto area = canvas_rect(width_, height_);
    const int saved_dc = SaveDC(dc);
    IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);
    const auto rect_for = [&](const geometry::Rect2D &b) {
        return selection_ui::screen_bounds(camera, b, area);
    };
    if (is_inline_text_editing_ && text_editor_) {
        const auto& editor = *text_editor_;
        if (editor.composition.empty()) {
            const auto bounds = editor.parent_transform.map_rect(editor.draft.bounds());
            const auto rect = rect_for(bounds);
            const COLORREF frame_col = theme_ == ThemeMode::CyberDark
                ? RGB(0, 242, 195) : RGB(14, 165, 233);
            outline(dc, rect, frame_col, PS_DOT);
            const auto handle_pts = selection_ui::handles(rect);
            const COLORREF border = theme_ == ThemeMode::CyberDark
                ? RGB(148, 163, 184) : RGB(100, 116, 139);
            for (const auto point : handle_pts) {
                fill_rect(dc, {point.x - 4, point.y - 4, point.x + 4, point.y + 4}, border);
                fill_rect(dc, {point.x - 3, point.y - 3, point.x + 3, point.y + 3}, RGB(255, 255, 255));
            }
        }
        RestoreDC(dc, saved_dc);
        return;
    }
    if (hover_object_ && !pointer_active_ && !selection.is_selected(*hover_object_)) {
        const auto *object = snapshot->objects().lookup_ref(*hover_object_);
        if (object)
            std::visit(
                [&](const auto &value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (!std::is_same_v<T, document::GroupObject>)
                        outline(dc, rect_for(value.bounds()), RGB(14, 165, 233), PS_DOT);
                },
                *object);
    }
    if (auto bounds = selection.bounds(*snapshot)) {
        const auto current_delta = root().vector_controller().gesture().current_delta();
        if (is_dragging_ && drag_preview_bounds_)
            bounds = current_delta.map_rect(*drag_preview_bounds_);
        const auto rect = rect_for(*bounds);
        // Borderless selection: only sizing handles and pivot are drawn, matching CorelDRAW / Illustrator standards
        if (is_dragging_ && drag_preview_bounds_) {
            const auto &source = *drag_preview_bounds_;
            const std::array<geometry::Point2D, 4> corners{{
                {source.x, source.y}, {source.right(), source.y},
                {source.right(), source.bottom()}, {source.x, source.bottom()}}};
            std::array<POINT, 5> preview{};
            for (size_t i = 0; i < corners.size(); ++i) {
                const auto world = current_delta.map_point(corners[i]);
                const auto screen = camera.world_to_screen(world);
                preview[i] = {area.left + static_cast<LONG>(std::lround(screen.x)),
                              area.top + static_cast<LONG>(std::lround(screen.y))};
            }
            preview.back() = preview.front();
            HPEN preview_pen = CreatePen(PS_DASH, 1, RGB(0, 135, 106));
            const auto old_pen = SelectObject(dc, preview_pen);
            const auto old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Polyline(dc, preview.data(), static_cast<int>(preview.size()));
            SelectObject(dc, old_brush);
            SelectObject(dc, old_pen);
            DeleteObject(preview_pen);
        }
        const COLORREF handle_border = theme_ == ThemeMode::CyberDark ? RGB(148, 163, 184) : RGB(100, 116, 139);
        const auto handle_pts = selection_ui::handles(rect);
        if (is_rotate_mode_) {
            // In rotate mode: corner handles indicate rotation affordances, side handles indicate skew
            for (size_t i = 0; i < handle_pts.size(); ++i) {
                const auto p = handle_pts[i];
                if (i % 2 == 0) { // Corners: rotation circles
                    const auto brush = CreateSolidBrush(RGB(255, 255, 255));
                    const auto pen = CreatePen(PS_SOLID, 1, RGB(14, 165, 233));
                    const auto old_b = SelectObject(dc, brush);
                    const auto old_p = SelectObject(dc, pen);
                    Ellipse(dc, p.x - 5, p.y - 5, p.x + 5, p.y + 5);
                    SetPixel(dc, p.x, p.y, RGB(14, 165, 233));
                    SelectObject(dc, old_p);
                    SelectObject(dc, old_b);
                    DeleteObject(pen);
                    DeleteObject(brush);
                } else { // Sides: thin neutral handles
                    fill_rect(dc, {p.x - 4, p.y - 4, p.x + 4, p.y + 4}, handle_border);
                    fill_rect(dc, {p.x - 3, p.y - 3, p.x + 3, p.y + 3}, RGB(255, 255, 255));
                }
            }
        } else {
            // Standard scale mode: small 8x8 white square handles with thin 1px neutral light gray border
            for (const auto p : handle_pts) {
                fill_rect(dc, {p.x - 4, p.y - 4, p.x + 4, p.y + 4}, handle_border);
                fill_rect(dc, {p.x - 3, p.y - 3, p.x + 3, p.y + 3}, RGB(255, 255, 255));
            }
        }
        const auto rotate = selection_ui::rotation_handle(rect);
        const auto rotate_brush = CreateSolidBrush(RGB(255, 255, 255));
        const auto rotate_pen = CreatePen(PS_SOLID, 1, RGB(14, 165, 233));
        const auto old_rotate_brush = SelectObject(dc, rotate_brush);
        const auto old_rotate_pen = SelectObject(dc, rotate_pen);
        Ellipse(dc, rotate.x - 7, rotate.y - 7, rotate.x + 7, rotate.y + 7);
        // A short arrow mark distinguishes rotation affordance
        MoveToEx(dc, rotate.x - 3, rotate.y + 1, nullptr);
        LineTo(dc, rotate.x - 2, rotate.y - 3);
        LineTo(dc, rotate.x + 2, rotate.y - 4);
        LineTo(dc, rotate.x + 4, rotate.y - 1);
        LineTo(dc, rotate.x + 2, rotate.y - 2);
        SelectObject(dc, old_rotate_pen);
        SelectObject(dc, old_rotate_brush);
        DeleteObject(rotate_pen);
        DeleteObject(rotate_brush);

        // Rotation Pivot Point (tâm xoay) - displayed in both modes, draggable
        const auto pivot_world = custom_pivot_world_.value_or(bounds->center());
        const auto pivot_screen = selection_ui::pivot_point(camera, pivot_world, area);
        const LONG px = pivot_screen.x, py = pivot_screen.y;
        const auto pivot_pen = CreatePen(PS_SOLID, 1, RGB(14, 165, 233));
        const auto pivot_brush = CreateSolidBrush(RGB(255, 255, 255));
        const auto old_p_brush = SelectObject(dc, pivot_brush);
        const auto old_p_pen = SelectObject(dc, pivot_pen);
        Ellipse(dc, px - 6, py - 6, px + 7, py + 7);
        const auto dot_brush = CreateSolidBrush(RGB(14, 165, 233));
        SelectObject(dc, dot_brush);
        Ellipse(dc, px - 2, py - 2, px + 3, py + 3);
        SelectObject(dc, pivot_brush);
        DeleteObject(dot_brush);
        MoveToEx(dc, px - 9, py, nullptr);
        LineTo(dc, px - 5, py);
        MoveToEx(dc, px + 6, py, nullptr);
        LineTo(dc, px + 10, py);
        MoveToEx(dc, px, py - 9, nullptr);
        LineTo(dc, px, py - 5);
        MoveToEx(dc, px, py + 6, nullptr);
        LineTo(dc, px, py + 10);
        SelectObject(dc, old_p_pen);
        SelectObject(dc, old_p_brush);
        DeleteObject(pivot_pen);
        DeleteObject(pivot_brush);

        const LONG cx = (rect.left + rect.right) / 2;
        std::wostringstream text;
        if (quick_dimension_active_) {
            text << (language_ == UILanguage::Vietnamese ? L"Kích thước: " : L"Dimensions: ");
            if (!quick_dimension_buffer_.empty()) {
                for (char c : quick_dimension_buffer_) text << static_cast<wchar_t>(c);
                text << L" mm (↵ Enter)";
            } else {
                text << std::fixed << std::setprecision(1) << bounds->width << L", " << bounds->height << (language_ == UILanguage::Vietnamese ? L" (Gõ ngang,dài ↵)" : L" (Type W,L ↵)");
            }
        } else {
            text << std::fixed << std::setprecision(2) << bounds->width << L" × " << bounds->height << L" mm";
            if (is_rotate_mode_) {
                text << (language_ == UILanguage::Vietnamese ? L" · Chế độ xoay" : L" · Rotate Mode");
            }
        }
        const int label_half_w = quick_dimension_active_ ? 140 : 105;
        RECT label{cx - label_half_w, rect.bottom + 10, cx + label_half_w, rect.bottom + 30};
        fill_rect(dc, label, quick_dimension_active_ ? RGB(20, 32, 48) : (theme_ == ThemeMode::CyberDark ? RGB(15, 23, 34) : RGB(226, 232, 240)));
        if (quick_dimension_active_) {
            outline(dc, label, RGB(0, 242, 195), PS_SOLID);
        }
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, quick_dimension_active_ ? RGB(0, 242, 195) : (theme_ == ThemeMode::CyberDark ? RGB(0, 242, 195) : RGB(0, 112, 88)));
        DrawTextW(dc, text.str().c_str(), -1, &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    if (is_marquee_) {
        auto rect = rect_for(selection_ui::between(pointer_down_world_, pointer_current_world_));
        const bool window = pointer_current_world_.x >= pointer_down_world_.x;
        const COLORREF color = window ? RGB(30, 120, 235) : RGB(15, 170, 95);
        RECT wash;
        if (IntersectRect(&wash, &rect, &area))
            marquee_wash(dc, wash, color);
        outline(dc, rect, color, window ? PS_SOLID : PS_DASH);
        const bool vi = language_ == UILanguage::Vietnamese;
        const wchar_t *text = window ? (vi ? L"Chọn trọn · trái → phải" : L"Window · left → right")
                                     : (vi ? L"Chọn cắt · phải → trái" : L"Crossing · right → left");
        RECT label{std::max(rect.left, area.left) + 4, std::max(rect.top, area.top) + 4,
                   std::max(rect.left, area.left) + 220, std::max(rect.top, area.top) + 24};
        SetTextColor(dc, color);
        SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, text, -1, &label, DT_SINGLELINE | DT_NOPREFIX);
    }
    if (saved_dc)
        RestoreDC(dc, saved_dc);
}
#endif
} // namespace pikdraw::desktop
