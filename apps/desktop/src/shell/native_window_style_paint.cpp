#include "pikdraw/color/preview/color_transform.hpp"
#include "pikdraw/desktop/shell/native_window.hpp"
#include <iomanip>
#include <sstream>

namespace pikdraw::desktop {
#ifdef _WIN32
void NativeWindow::paint_style_inspector(HDC dc, int left, int top, int bottom) {
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, left, top, left + 280, bottom);
    const bool vi = language_ == UILanguage::Vietnamese;
    const bool dark = theme_ == ThemeMode::CyberDark;
    const COLORREF text = dark ? RGB(220, 230, 242) : RGB(30, 41, 59);
    const COLORREF muted = dark ? RGB(142, 159, 179) : RGB(100, 116, 139);
    const COLORREF accent = dark ? RGB(0, 242, 195) : RGB(0, 112, 88);
    const COLORREF panel = dark ? RGB(19, 29, 43) : RGB(235, 241, 247);
    const auto snapshot = root_.document().current_snapshot();
    const auto &selection = root_.vector_controller().selection();
    const auto style = selection_style(*snapshot, selection.selected_ids(), default_style_);
    const auto &source_color = palette_stroke_ ? style.values.stroke : style.values.fill;
    const bool mixed = palette_stroke_ ? style.mixed_stroke : style.mixed_fill;
    const bool enabled = palette_stroke_ ? style.values.has_stroke : style.values.has_fill;
    const bool mixed_enabled = palette_stroke_ ? style.mixed_stroke_enabled : style.mixed_fill_enabled;
    SetBkMode(dc, TRANSPARENT);
    const auto label = [&](const std::wstring &value, int x, int y, int width, COLORREF colorref) {
        SetTextColor(dc, colorref);
        RECT r{left + x, top + y, left + x + width, top + y + 24};
        DrawTextW(dc, value.c_str(), -1, &r,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    };
    const auto button = [&](RECT r, const std::wstring &value, bool active, bool available = true) {
        OffsetRect(&r, left, top);
        const auto brush = CreateSolidBrush(panel);
        const auto pen =
            CreatePen(PS_SOLID, 1, active ? accent : (dark ? RGB(49, 65, 85) : RGB(187, 200, 216)));
        const auto old_brush = SelectObject(dc, brush);
        const auto old_pen = SelectObject(dc, pen);
        RoundRect(dc, r.left, r.top, r.right, r.bottom, 5, 5);
        SelectObject(dc, old_brush);
        SelectObject(dc, old_pen);
        DeleteObject(brush);
        DeleteObject(pen);
        SetTextColor(dc, available ? (active ? accent : text) : muted);
        DrawTextW(dc, value.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    };
    const auto title = style.count == 0
                           ? (vi ? L"KIỂU CHO ĐỐI TƯỢNG MỚI" : L"NEW OBJECT STYLE")
                           : (vi ? L"ĐANG CHỌN: " : L"SELECTED: ") + std::to_wstring(style.count);
    label(title, 12, 0, 256, accent);
    if (const auto bounds = selection.bounds(*snapshot)) {
        std::wostringstream xy, wh;
        xy << std::fixed << std::setprecision(2) << L"X " << bounds->x << L"   Y " << bounds->y << L" mm";
        wh << std::fixed << std::setprecision(2) << L"W " << bounds->width << L"   H " << bounds->height
           << L" mm";
        label(xy.str(), 12, 27, 256, text);
        label(wh.str(), 12, 49, 256, text);
    } else {
        label(vi ? L"Click hoặc kéo khung để chọn đối tượng" : L"Click or drag a window to select", 12, 32,
              256, muted);
    }
    button({12, 82, 134, 110}, vi ? L"Tô màu · Fill" : L"Fill", !palette_stroke_);
    button({142, 82, 268, 110}, vi ? L"Đường viền · Stroke" : L"Stroke", palette_stroke_, style.all_paths);
    button({12, 120, 268, 164}, L"", false);
    const auto convert = [&](const color::ColorValue &value) {
        return (color_preview_ ? color_preview_->convert(value) : color::to_preview_rgba(value)).to_device();
    };
    const auto device = convert(source_color);
    RECT swatch{left + 20, top + 128, left + 46, top + 156};
    const auto brush = CreateSolidBrush(RGB(device.r, device.g, device.b));
    FillRect(dc, &swatch, brush);
    DeleteObject(brush);
    if (!enabled) {
        const auto pen = CreatePen(PS_SOLID, 2, RGB(235, 65, 80));
        const auto previous = SelectObject(dc, pen);
        MoveToEx(dc, swatch.left, swatch.bottom, nullptr);
        LineTo(dc, swatch.right, swatch.top);
        SelectObject(dc, previous);
        DeleteObject(pen);
    }
    label(mixed ? (vi ? L"Nhiều giá trị" : L"Mixed values") : utf8_to_wide(color_description(source_color)),
          55, 123, 207, text);
    label(vi ? L"Nhấp để nhập kênh màu…" : L"Click to edit source channels…", 55, 143, 207, muted);
    const auto enable_text = mixed_enabled ? (vi ? L"Nhiều trạng thái · bật tất cả" : L"Mixed · enable all")
                             : enabled ? (vi ? L"✓ Đang bật · nhấp để tắt" : L"✓ Enabled · click to disable")
                                       : (vi ? L"Không màu · nhấp để bật" : L"None · click to enable");
    button({12, 174, 268, 202}, enable_text, enabled && !mixed_enabled, style.all_paths);
    label(vi ? L"ĐỘ DÀY VIỀN NGUỒN" : L"SOURCE STROKE WIDTH", 12, 210, 256, muted);
    std::wostringstream width;
    if (style.mixed_width)
        width << (vi ? L"Nhiều giá trị" : L"Mixed values");
    else
        width << std::fixed << std::setprecision(3) << style.values.width_mm << L" mm";
    button({12, 236, 268, 266}, width.str() + L"   ▾", false, style.all_paths);
    label(vi ? L"BẢNG MÀU NGUỒN" : L"SOURCE COLOR PALETTE", 12, 274, 256, accent);
    button({12, 300, 134, 328}, L"RGB", !palette_cmyk_);
    button({142, 300, 268, 328}, L"CMYK", palette_cmyk_);
    const auto &palette = editor_palette(palette_cmyk_);
    for (size_t i = 0; i < palette.size(); ++i) {
        const auto rgba = convert(palette[i]);
        const LONG x = left + 12 + static_cast<int>(i % 9) * 28;
        const LONG y = top + 338 + static_cast<int>(i / 9) * 28;
        RECT rect{x, y, x + 24, y + 24};
        const auto color_brush = CreateSolidBrush(RGB(rgba.r, rgba.g, rgba.b));
        FillRect(dc, &rect, color_brush);
        DeleteObject(color_brush);
        FrameRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
    }
    label(vi ? L"Trái: mục đang chọn · phải: Viền" : L"Left: active target · right: Stroke", 12, 399, 256,
          muted);
    label(color_preview_ && color_preview_->has_cmyk_transform()
              ? L"CMYK preview: ICC → sRGB"
              : (vi ? L"CMYK preview: gần đúng · chưa có ICC" : L"CMYK preview: approximate · no ICC"),
          12, 423, 256, muted);
    button({12, 458, 268, 490}, vi ? L"Profile màu và ICC…" : L"Color profiles and ICC…", false);
    label(vi ? L"Trái → phải: chọn trọn" : L"Left → right: Window", 12, 502, 256, muted);
    label(vi ? L"Phải → trái: chọn cắt" : L"Right → left: Crossing", 12, 524, 256, muted);
    label(vi ? L"Shift: thêm / giữ tỉ lệ · Ctrl: đảo chọn" : L"Shift: add / keep ratio · Ctrl: toggle", 12,
          546, 256, muted);
    RestoreDC(dc, saved);
}
#endif
} // namespace pikdraw::desktop
