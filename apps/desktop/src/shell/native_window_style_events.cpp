#include "native_style_dialog.hpp"
#include <iomanip>
#include <sstream>

namespace pikdraw::desktop {
#ifdef _WIN32
void NativeWindow::open_style_editor(bool stroke) {
    const auto summary =
        selection_style(*root().document().current_snapshot(),
                        root().vector_controller().selection().selected_ids(), default_style_);
    if (stroke && !summary.all_paths)
        return;
    if (const auto edited =
            edit_source_color(hwnd_, stroke ? summary.values.stroke : summary.values.fill,
                              summary.values.width_mm, stroke, language_ == UILanguage::Vietnamese)) {
        vector::StylePatch patch;
        if (stroke) {
            patch.stroke = edited->color;
            patch.width_mm = edited->width_mm;
        } else {
            patch.fill = edited->color;
        }
        (void)apply_style_patch(patch);
    }
}
void NativeWindow::choose_stroke_width(int x, int y) {
    constexpr double widths[] = {.1, .25, .5, 1, 2, 5, 10};
    const auto menu = CreatePopupMenu();
    for (int i = 0; i < 7; ++i) {
        std::wostringstream text;
        text << widths[i] << L" mm";
        AppendMenuW(menu, MF_STRING, 100 + i, text.str().c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 200,
                language_ == UILanguage::Vietnamese ? L"Nhập kích thước…" : L"Custom width…");
    POINT point{x, y};
    ClientToScreen(hwnd_, &point);
    const auto chosen =
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (chosen >= 100 && chosen < 107)
        (void)set_selection_stroke_width(widths[chosen - 100]);
    else if (chosen == 200)
        open_style_editor(true);
}
bool NativeWindow::handle_style_click(int x, int y, bool right_click) {
    if (active_dock_tab_ != 0 || x < width_ - 280 || y < 108 || y >= height_ - 26)
        return false;
    const int dx = x - (width_ - 280), dy = y - 108;
    const auto style = selection_style(*root().document().current_snapshot(),
                                       root().vector_controller().selection().selected_ids(), default_style_);
    if (dy >= 338 && dy < 390 && dx >= 12 && dx < 264) {
        const int column = (dx - 12) / 28, row = (dy - 338) / 28;
        if ((dx - 12) % 28 < 24 && (dy - 338) % 28 < 24 && column < 9 && row < 2) {
            (void)apply_selection_color(right_click || palette_stroke_,
                                        editor_palette(palette_cmyk_)[static_cast<size_t>(row * 9 + column)]);
        }
        return true;
    }
    if (right_click)
        return true;
    if (dy >= 82 && dy < 110) {
        if (dx >= 12 && dx < 134)
            palette_stroke_ = false;
        if (dx >= 142 && dx < 268 && style.all_paths)
            palette_stroke_ = true;
    } else if (dy >= 120 && dy < 164 && dx >= 12 && dx < 268) {
        open_style_editor(palette_stroke_);
    } else if (dy >= 174 && dy < 202 && dx >= 12 && dx < 268 && style.all_paths) {
        if (palette_stroke_)
            (void)set_selection_stroke_enabled(style.mixed_stroke_enabled || !style.values.has_stroke);
        else
            (void)set_selection_fill_enabled(style.mixed_fill_enabled || !style.values.has_fill);
    } else if (dy >= 236 && dy < 266 && dx >= 12 && dx < 268 && style.all_paths) {
        choose_stroke_width(x, y);
    } else if (dy >= 300 && dy < 328) {
        if (dx >= 12 && dx < 134)
            palette_cmyk_ = false;
        if (dx >= 142 && dx < 268)
            palette_cmyk_ = true;
    } else if (dy >= 458 && dy < 490 && dx >= 12 && dx < 268) {
        show_color_profiles();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}
#endif
} // namespace pikdraw::desktop
