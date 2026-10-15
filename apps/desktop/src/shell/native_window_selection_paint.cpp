#include "native_window_selection_geometry.hpp"
#include <iomanip>
#include <sstream>

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
    const auto snapshot = root_.document().current_snapshot();
    const auto &selection = root_.vector_controller().selection();
    const auto &camera = root_.renderer().camera();
    const auto area = canvas_rect(width_, height_);
    const auto rect_for = [&](const geometry::Rect2D &b) {
        return selection_ui::screen_bounds(camera, b, area);
    };
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
        if (is_dragging_ && drag_preview_bounds_)
            bounds = root_.vector_controller().gesture().current_delta().map_rect(*drag_preview_bounds_);
        const auto rect = rect_for(*bounds);
        outline(dc, rect, RGB(255, 255, 255), PS_SOLID, 3);
        outline(dc, rect, RGB(12, 18, 28));
        for (const auto p : selection_ui::handles(rect)) {
            fill_rect(dc, {p.x - 5, p.y - 5, p.x + 5, p.y + 5}, RGB(255, 255, 255));
            fill_rect(dc, {p.x - 4, p.y - 4, p.x + 4, p.y + 4}, RGB(12, 18, 28));
        }
        const LONG cx = (rect.left + rect.right) / 2, cy = (rect.top + rect.bottom) / 2;
        const auto pen = CreatePen(PS_SOLID, 1, RGB(12, 18, 28));
        const auto previous = SelectObject(dc, pen);
        MoveToEx(dc, cx - 4, cy, nullptr);
        LineTo(dc, cx + 5, cy);
        MoveToEx(dc, cx, cy - 4, nullptr);
        LineTo(dc, cx, cy + 5);
        SelectObject(dc, previous);
        DeleteObject(pen);
        std::wostringstream text;
        text << std::fixed << std::setprecision(2) << bounds->width << L" × " << bounds->height << L" mm";
        RECT label{cx - 95, rect.bottom + 10, cx + 95, rect.bottom + 30};
        fill_rect(dc, label, theme_ == ThemeMode::CyberDark ? RGB(15, 23, 34) : RGB(226, 232, 240));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, theme_ == ThemeMode::CyberDark ? RGB(0, 242, 195) : RGB(0, 112, 88));
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
}
#endif
} // namespace pikdraw::desktop
