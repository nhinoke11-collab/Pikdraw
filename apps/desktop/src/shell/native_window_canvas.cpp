#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_canvas_frame.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace pikdraw::desktop {

#ifdef _WIN32
namespace {

RECT screen_rect(const rendering::Camera2D& camera, const geometry::Rect2D& bounds,
                 int origin_x, int origin_y) {
    const auto top_left = camera.world_to_screen({bounds.x, bounds.y + bounds.height});
    const auto bottom_right = camera.world_to_screen({bounds.x + bounds.width, bounds.y});
    return RECT{origin_x + static_cast<int>(top_left.x),
                origin_y + static_cast<int>(top_left.y),
                origin_x + static_cast<int>(bottom_right.x),
                origin_y + static_cast<int>(bottom_right.y)};
}

void draw_outline(HDC dc, const rendering::Camera2D& camera,
                  const geometry::Rect2D& bounds, int origin_x, int origin_y,
                  COLORREF color, int style, bool ellipse) {
    const RECT rect = screen_rect(camera, bounds, origin_x, origin_y);
    HPEN pen = CreatePen(style, 1, color);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    if (ellipse)
        Ellipse(dc, rect.left, rect.top, rect.right, rect.bottom);
    else
        Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

geometry::Rect2D bounds_between(const geometry::Point2D& first,
                                const geometry::Point2D& second) {
    return {std::min(first.x, second.x), std::min(first.y, second.y),
            std::abs(second.x - first.x), std::abs(second.y - first.y)};
}

} // namespace

RECT NativeWindow::canvas_rect(int width, int height) const noexcept {
    return RECT{72, 92, std::max(72, width - 280), std::max(92, height - 26)};
}

bool NativeWindow::is_canvas_point(int x, int y) const noexcept {
    const RECT area = canvas_rect(width_, height_);
    return x >= area.left && x < area.right && y >= area.top && y < area.bottom;
}

geometry::Point2D NativeWindow::canvas_to_world(int x, int y) noexcept {
    const RECT area = canvas_rect(width_, height_);
    auto& camera = root_.renderer().camera();
    camera.set_viewport_size(area.right - area.left, area.bottom - area.top);
    const auto point = camera.screen_to_world({static_cast<float>(x - area.left),
                                               static_cast<float>(y - area.top)});
    return {point.x, point.y};
}

void NativeWindow::paint_document_canvas(HDC dc, int width, int height) {
    const RECT area = canvas_rect(width, height);
    const int canvas_width = area.right - area.left;
    const int canvas_height = area.bottom - area.top;
    if (canvas_width <= 0 || canvas_height <= 0)
        return;

    const int saved = SaveDC(dc);
    IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);
    HBRUSH background = CreateSolidBrush(theme_ == ThemeMode::CyberDark
        ? RGB(18, 20, 26) : RGB(241, 245, 249));
    FillRect(dc, &area, background);
    DeleteObject(background);

    auto &camera = root_.renderer().camera();
    camera.set_viewport_size(canvas_width, canvas_height);
    const auto snapshot = root_.document().current_snapshot();
    if (!canvas_frame_)
        canvas_frame_ = std::make_unique<CanvasFrameCache>();
    auto &frame = *canvas_frame_;
    const size_t expected = static_cast<size_t>(canvas_width) *
                            static_cast<size_t>(canvas_height) * 4;
    if (!frame.matches(snapshot, camera, canvas_width, canvas_height, color_preview_, theme_ == ThemeMode::CyberDark)) {
        const auto rgba = workflow_.render_snapshot(canvas_width, canvas_height);
        frame.bgra.resize(rgba.size());
        for (size_t index = 0; index < rgba.size(); index += 4) {
            frame.bgra[index] = rgba[index + 2];
            frame.bgra[index + 1] = rgba[index + 1];
            frame.bgra[index + 2] = rgba[index];
            frame.bgra[index + 3] = 255;
        }
        frame.snapshot = snapshot;
        frame.profile = color_preview_;
        frame.center = camera.center();
        frame.zoom = camera.zoom();
        frame.width = canvas_width;
        frame.height = canvas_height;
        frame.dark = theme_ == ThemeMode::CyberDark;
    }
    if (frame.bgra.size() == expected) {
        BITMAPINFO bitmap{};
        bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap.bmiHeader.biWidth = canvas_width;
        bitmap.bmiHeader.biHeight = -canvas_height;
        bitmap.bmiHeader.biPlanes = 1;
        bitmap.bmiHeader.biBitCount = 32;
        bitmap.bmiHeader.biCompression = BI_RGB;
        StretchDIBits(dc, area.left, area.top, canvas_width, canvas_height,
                      0, 0, canvas_width, canvas_height, frame.bgra.data(), &bitmap,
                      DIB_RGB_COLORS, SRCCOPY);
    } else {
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(180, 90, 80));
        const wchar_t* error = language_ == UILanguage::Vietnamese
            ? L"Không thể dựng một số đối tượng của tài liệu."
            : L"Some document objects could not be rendered.";
        TextOutW(dc, area.left + 18, area.top + 18, error,
                 static_cast<int>(wcslen(error)));
    }

    // Draw only the page's external shadow after rendering, leaving page pixels untouched.
    const auto page_top_left = camera.world_to_screen({0.0, snapshot->page().height_mm});
    const auto page_bottom_right = camera.world_to_screen(
        {snapshot->page().width_mm, 0.0});
    const int page_left = area.left + static_cast<int>(std::floor(page_top_left.x));
    const int page_top = area.top + static_cast<int>(std::floor(page_top_left.y));
    const int page_right = area.left + static_cast<int>(std::ceil(page_bottom_right.x));
    const int page_bottom = area.top + static_cast<int>(std::ceil(page_bottom_right.y));
    HBRUSH shadow = CreateSolidBrush(theme_ == ThemeMode::CyberDark
        ? RGB(4, 7, 12) : RGB(148, 163, 184));
    RECT shadow_right{page_right, page_top + 7, page_right + 7, page_bottom + 7};
    RECT shadow_bottom{page_left + 7, page_bottom, page_right + 7, page_bottom + 7};
    FillRect(dc, &shadow_right, shadow);
    FillRect(dc, &shadow_bottom, shadow);
    DeleteObject(shadow);

    paint_selection_overlay(dc);

    if (is_creating_shape_) {
        const auto preview = bounds_between(pointer_down_world_, pointer_current_world_);
        const bool ellipse = active_tool_ == 2;
        draw_outline(dc, camera, preview, area.left, area.top,
                     RGB(0, 242, 195), PS_DASH, ellipse);
    }
    const auto &diagnostics = root_.renderer().last_metric().diagnostics;
    const auto errors = std::count_if(diagnostics.begin(), diagnostics.end(),
        [](const auto &item) { return item.error; });
    const bool icc_error = color_preview_ && color_preview_->conversion_failure_count() > 0;
    if (errors > 0 || icc_error) {
        const auto warning = icc_error ? (language_ == UILanguage::Vietnamese
            ? L"ICC preview có lỗi; một số màu đang được hiển thị gần đúng."
            : L"ICC preview failed for some colors; approximate preview is active.")
            : language_ == UILanguage::Vietnamese
            ? L"Một số đối tượng chưa dựng được. Dữ liệu nguồn được giữ nguyên."
            : L"Some objects could not be displayed. Source data is preserved.";
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(205, 125, 45));
        TextOutW(dc, area.left + 12, area.bottom - 24, warning, static_cast<int>(wcslen(warning)));
    }
    RestoreDC(dc, saved);
    paint_canvas_rulers(dc, width, height);
}

void NativeWindow::paint_canvas_rulers(HDC dc, int width, int height) {
    const RECT area = canvas_rect(width, height);
    const auto& camera = root_.renderer().camera();
    const double zoom = camera.zoom();
    if (zoom <= 0.0)
        return;

    const auto top_left = camera.screen_to_world({0.0f, 0.0f});
    const auto bottom_right = camera.screen_to_world(
        {static_cast<float>(area.right - area.left),
         static_cast<float>(area.bottom - area.top)});
    const double visible_width = std::abs(bottom_right.x - top_left.x);
    const double visible_height = std::abs(bottom_right.y - top_left.y);
    const auto choose_step = [](double visible) {
        const double raw = visible / 12.0;
        const double power = std::pow(10.0, std::floor(std::log10(std::max(raw, 1e-9))));
        const double scaled = raw / power;
        const double unit = scaled <= 1.0 ? 1.0 : scaled <= 2.0 ? 2.0
                          : scaled <= 5.0 ? 5.0 : 10.0;
        const double major = unit * power;
        return std::pair{std::max(major / 5.0, 1e-9), major};
    };
    const auto [minor_x, major_x] = choose_step(visible_width);
    const auto [minor_y, major_y] = choose_step(visible_height);
    const auto& page = root_.document().current_snapshot()->page();

    const COLORREF base = theme_ == ThemeMode::CyberDark ? RGB(15, 23, 34) : RGB(226, 232, 240);
    const COLORREF tick = theme_ == ThemeMode::CyberDark ? RGB(82, 98, 118) : RGB(100, 116, 139);
    const COLORREF text = theme_ == ThemeMode::CyberDark ? RGB(171, 185, 202) : RGB(71, 85, 105);
    HBRUSH base_brush = CreateSolidBrush(base);
    RECT top_ruler{area.left, 72, area.right, area.top};
    RECT left_ruler{52, area.top, area.left, area.bottom};
    RECT corner{52, 72, area.left, area.top};
    FillRect(dc, &top_ruler, base_brush);
    FillRect(dc, &left_ruler, base_brush);
    FillRect(dc, &corner, base_brush);
    DeleteObject(base_brush);

    HPEN line = CreatePen(PS_SOLID, 1, tick);
    HGDIOBJ old_pen = SelectObject(dc, line);
    HFONT ruler_font = CreateFontW(10, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ old_font = SelectObject(dc, ruler_font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, text);
    const double left_world = camera.screen_to_world({0.0f, 0.0f}).x;
    const double right_world = camera.screen_to_world(
        {static_cast<float>(area.right - area.left), 0.0f}).x;
    const double first_x = std::floor(left_world / minor_x) * minor_x;
    for (double value = first_x; value <= right_world + minor_x; value += minor_x) {
        const auto screen = camera.world_to_screen({value, page.height_mm * 0.5});
        const int x = area.left + static_cast<int>(std::lround(screen.x));
        const double major_index = value / major_x;
        const bool is_major = std::abs(major_index - std::round(major_index)) < 1e-6;
        MoveToEx(dc, x, area.top - (is_major ? 10 : 5), nullptr);
        LineTo(dc, x, area.top - 1);
        if (is_major && x >= area.left && x < area.right - 26) {
            const auto label = std::to_wstring(static_cast<long long>(std::llround(value)));
            TextOutW(dc, x + 2, 73, label.c_str(), static_cast<int>(label.size()));
        }
    }
    const double bottom_world = camera.screen_to_world(
        {0.0f, static_cast<float>(area.bottom - area.top)}).y;
    const double top_world = camera.screen_to_world({0.0f, 0.0f}).y;
    const double first_y = std::floor(bottom_world / minor_y) * minor_y;
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, 52, area.top, area.left, area.bottom);
    HFONT vertical_font = CreateFontW(10, 0, 900, 900, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SelectObject(dc, vertical_font);
    for (double value = first_y; value <= top_world + minor_y; value += minor_y) {
        const auto screen = camera.world_to_screen({page.width_mm * 0.5, value});
        const int y = area.top + static_cast<int>(std::lround(screen.y));
        const double major_index = value / major_y;
        const bool is_major = std::abs(major_index - std::round(major_index)) < 1e-6;
        MoveToEx(dc, area.left - (is_major ? 10 : 5), y, nullptr);
        LineTo(dc, area.left - 1, y);
        if (is_major && y >= area.top + 14 && y < area.bottom - 8) {
            const auto label = std::to_wstring(static_cast<long long>(std::llround(value)));
            TextOutW(dc, 54, y + 3, label.c_str(), static_cast<int>(label.size()));
        }
    }
    SelectObject(dc, ruler_font);
    DeleteObject(vertical_font);
    RestoreDC(dc, saved);
    MoveToEx(dc, area.left, area.top, nullptr);
    LineTo(dc, area.right, area.top);
    MoveToEx(dc, area.left, area.top, nullptr);
    LineTo(dc, area.left, area.bottom);

    SelectObject(dc, old_font);
    SelectObject(dc, old_pen);
    DeleteObject(ruler_font);
    DeleteObject(line);
}

#endif

} // namespace pikdraw::desktop
