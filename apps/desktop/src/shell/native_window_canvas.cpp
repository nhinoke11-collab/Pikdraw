#include "pikdraw/desktop/shell/native_window.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace pikdraw::desktop {

#ifdef _WIN32
namespace {

std::optional<geometry::Rect2D> object_bounds(const document::ObjectVariant& object) {
    return std::visit([](const auto& value) -> std::optional<geometry::Rect2D> {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, document::GroupObject>)
            return std::nullopt;
        else
            return value.bounds();
    }, object);
}

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
    return RECT{72, 122, std::max(72, width - 280), std::max(122, height - 26)};
}

bool NativeWindow::is_canvas_point(int x, int y) const noexcept {
    const RECT area = canvas_rect(width_, height_);
    return x >= area.left && x < area.right && y >= area.top && y < area.bottom;
}

geometry::Point2D NativeWindow::canvas_to_world(int x, int y) noexcept {
    const RECT area = canvas_rect(width_, height_);
    auto& camera = root().renderer().camera();
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

    auto& camera = root().renderer().camera();
    const auto snapshot = root().document().current_snapshot();

    // Check if the canvas bitmap cache needs to be regenerated
    const bool cache_needs_rebuild = !canvas_cache_valid_
        || !cached_canvas_dc_
        || canvas_width != cached_canvas_w_
        || canvas_height != cached_canvas_h_
        || snapshot->revision() != cached_revision_
        || camera.zoom() != cached_zoom_
        || camera.center().x != cached_camera_x_
        || camera.center().y != cached_camera_y_
        || theme_ != cached_theme_;

    if (cache_needs_rebuild) {
        if (!cached_canvas_dc_) {
            cached_canvas_dc_ = CreateCompatibleDC(dc);
        }
        if (cached_canvas_bmp_ && (canvas_width != cached_canvas_w_ || canvas_height != cached_canvas_h_)) {
            SelectObject(cached_canvas_dc_, cached_canvas_old_bmp_);
            DeleteObject(cached_canvas_bmp_);
            cached_canvas_bmp_ = nullptr;
            cached_canvas_old_bmp_ = nullptr;
        }
        if (!cached_canvas_bmp_) {
            cached_canvas_bmp_ = CreateCompatibleBitmap(dc, canvas_width, canvas_height);
            cached_canvas_old_bmp_ = SelectObject(cached_canvas_dc_, cached_canvas_bmp_);
        }

        // Fill background of cached canvas
        RECT local_area{0, 0, canvas_width, canvas_height};
        HBRUSH background = CreateSolidBrush(theme_ == ThemeMode::CyberDark
            ? RGB(18, 20, 26) : RGB(241, 245, 249));
        FillRect(cached_canvas_dc_, &local_area, background);
        DeleteObject(background);

        // Render document snapshot using zero-allocation buffer
        workflow().render_snapshot(canvas_width, canvas_height, reusable_rgba_buffer_);
        const size_t expected = static_cast<size_t>(canvas_width) * static_cast<size_t>(canvas_height) * 4;
        if (reusable_rgba_buffer_.size() == expected) {
            if (reusable_bgra_buffer_.size() != expected) {
                reusable_bgra_buffer_.resize(expected);
            }
            // Fast 32-bit swap from little-endian RGBA to BGRA
            const uint32_t* src32 = reinterpret_cast<const uint32_t*>(reusable_rgba_buffer_.data());
            uint32_t* dst32 = reinterpret_cast<uint32_t*>(reusable_bgra_buffer_.data());
            const size_t count = expected / 4;
            for (size_t index = 0; index < count; ++index) {
                const uint32_t px = src32[index];
                dst32[index] = (px & 0xFF00FF00) | ((px & 0x00FF0000) >> 16) | ((px & 0x000000FF) << 16);
            }
            BITMAPINFO bitmap{};
            bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bitmap.bmiHeader.biWidth = canvas_width;
            bitmap.bmiHeader.biHeight = -canvas_height;
            bitmap.bmiHeader.biPlanes = 1;
            bitmap.bmiHeader.biBitCount = 32;
            bitmap.bmiHeader.biCompression = BI_RGB;
            StretchDIBits(cached_canvas_dc_, 0, 0, canvas_width, canvas_height,
                          0, 0, canvas_width, canvas_height, reusable_bgra_buffer_.data(), &bitmap,
                          DIB_RGB_COLORS, SRCCOPY);
        } else {
            SetBkMode(cached_canvas_dc_, TRANSPARENT);
            SetTextColor(cached_canvas_dc_, RGB(180, 90, 80));
            const wchar_t* error = language_ == UILanguage::Vietnamese
                ? L"Không thể dựng một số đối tượng của tài liệu."
                : L"Some document objects could not be rendered.";
            TextOutW(cached_canvas_dc_, 18, 18, error, static_cast<int>(wcslen(error)));
        }

        // Draw page shadow onto cached canvas
        const auto page_top_left = camera.world_to_screen({0.0, snapshot->page().height_mm});
        const auto page_bottom_right = camera.world_to_screen({snapshot->page().width_mm, 0.0});
        const int page_left = static_cast<int>(std::floor(page_top_left.x));
        const int page_top = static_cast<int>(std::floor(page_top_left.y));
        const int page_right = static_cast<int>(std::ceil(page_bottom_right.x));
        const int page_bottom = static_cast<int>(std::ceil(page_bottom_right.y));
        HBRUSH shadow = CreateSolidBrush(theme_ == ThemeMode::CyberDark
            ? RGB(4, 7, 12) : RGB(148, 163, 184));
        RECT shadow_right{page_right, page_top + 7, page_right + 7, page_bottom + 7};
        RECT shadow_bottom{page_left + 7, page_bottom, page_right + 7, page_bottom + 7};
        FillRect(cached_canvas_dc_, &shadow_right, shadow);
        FillRect(cached_canvas_dc_, &shadow_bottom, shadow);
        DeleteObject(shadow);

        // Update cached parameters
        cached_revision_ = snapshot->revision();
        cached_zoom_ = camera.zoom();
        cached_camera_x_ = camera.center().x;
        cached_camera_y_ = camera.center().y;
        cached_canvas_w_ = canvas_width;
        cached_canvas_h_ = canvas_height;
        cached_theme_ = theme_;
        canvas_cache_valid_ = true;
    }

    // Fast blit from cached memory DC to destination DC!
    BitBlt(dc, area.left, area.top, canvas_width, canvas_height,
           cached_canvas_dc_, 0, 0, SRCCOPY);

    const int saved = SaveDC(dc);
    IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);

    for (const auto id : root().vector_controller().selection().selected_ids()) {
        if (const auto bounds = workflow().get_object_bounds(id))
            draw_outline(dc, camera, *bounds, area.left, area.top,
                         RGB(14, 165, 233), PS_DOT, false);
    }

    if (is_in_isolation_mode()) {
        auto iso_id = *current_isolated_group_id();
        for (const auto& [id, obj_var] : snapshot->objects().all()) {
            if (!is_object_in_isolated_group(id) && !std::holds_alternative<document::GroupObject>(obj_var)) {
                if (auto b = workflow().get_object_bounds(id)) {
                    RECT obj_rc = screen_rect(camera, *b, area.left, area.top);
                    InflateRect(&obj_rc, 4, 4);
                    RECT clipped{};
                    if (IntersectRect(&clipped, &obj_rc, &area)) {
                        HDC mem_wash = CreateCompatibleDC(dc);
                        HBITMAP mem_bmp = CreateCompatibleBitmap(dc, 1, 1);
                        HGDIOBJ old_bmp = SelectObject(mem_wash, mem_bmp);
                        COLORREF wash_col = (theme_ == ThemeMode::CyberDark) ? RGB(18, 20, 26) : RGB(255, 255, 255);
                        SetPixel(mem_wash, 0, 0, wash_col);
                        BLENDFUNCTION bf{};
                        bf.BlendOp = AC_SRC_OVER;
                        bf.SourceConstantAlpha = 180;
                        AlphaBlend(dc, clipped.left, clipped.top, clipped.right - clipped.left, clipped.bottom - clipped.top,
                                   mem_wash, 0, 0, 1, 1, bf);
                        SelectObject(mem_wash, old_bmp);
                        DeleteObject(mem_bmp);
                        DeleteDC(mem_wash);
                    }
                }
            }
        }

        // Draw faint dashed bounding box for the isolated group
        if (auto iso_bounds = workflow().get_object_bounds(iso_id)) {
            draw_outline(dc, camera, *iso_bounds, area.left, area.top,
                         RGB(100, 116, 139), PS_DASH, false);
        }
    }

    if (is_dragging_ && drag_preview_bounds_) {
        auto preview = *drag_preview_bounds_;
        preview.x += drag_preview_delta_.x;
        preview.y += drag_preview_delta_.y;
        draw_outline(dc, camera, preview, area.left, area.top,
                     RGB(0, 242, 195), PS_DASH, false);
    }

    if (is_creating_shape_) {
        const auto preview = bounds_between(pointer_down_world_, pointer_current_world_);
        const bool ellipse = active_tool_ == 2;
        draw_outline(dc, camera, preview, area.left, area.top,
                     RGB(0, 242, 195), PS_DASH, ellipse);
    }

    if (is_knife_cutting_) {
        auto s0 = camera.world_to_screen({knife_start_world_.x, knife_start_world_.y});
        auto s1 = camera.world_to_screen({knife_current_world_.x, knife_current_world_.y});
        int sx0 = area.left + static_cast<int>(s0.x);
        int sy0 = area.top + static_cast<int>(s0.y);
        int sx1 = area.left + static_cast<int>(s1.x);
        int sy1 = area.top + static_cast<int>(s1.y);

        HPEN laser_pen = CreatePen(PS_DASH, 2, RGB(239, 68, 68));
        HGDIOBJ old_pen = SelectObject(dc, laser_pen);
        MoveToEx(dc, sx0, sy0, nullptr);
        LineTo(dc, sx1, sy1);
        SelectObject(dc, old_pen);
        DeleteObject(laser_pen);

        HPEN marker_pen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
        HBRUSH marker_br = CreateSolidBrush(RGB(239, 68, 68));
        HGDIOBJ op = SelectObject(dc, marker_pen);
        HGDIOBJ ob = SelectObject(dc, marker_br);
        Ellipse(dc, sx0 - 4, sy0 - 4, sx0 + 5, sy0 + 5);
        Ellipse(dc, sx1 - 4, sy1 - 4, sx1 + 5, sy1 + 5);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(marker_br);
        DeleteObject(marker_pen);
    }

    // 1. Render all TextObjects with Windows native GDI fonts onto canvas
    for (const auto& [id, obj_var] : snapshot->objects().all()) {
        if (const auto* to = std::get_if<document::TextObject>(&obj_var)) {
            if (!to->visible || to->text.empty()) continue;
            if (is_inline_text_editing_ && inline_editing_text_id_ == id) continue;

            auto pt_screen = camera.world_to_screen(to->position);
            int sx = area.left + static_cast<int>(std::floor(pt_screen.x));
            int sy = area.top + static_cast<int>(std::floor(pt_screen.y));

            int font_px = std::max(8, static_cast<int>(std::lround(to->font.size_pt * camera.zoom() * 1.333)));
            int weight = to->font.weight;
            BOOL italic = to->font.italic ? TRUE : FALSE;
            BOOL underline = to->underline ? TRUE : FALSE;

            std::wstring wfamily(to->font.family_name.begin(), to->font.family_name.end());
            HFONT hfont = CreateFontW(
                font_px, 0, 0, 0, weight, italic, underline, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                wfamily.c_str()
            );
            HGDIOBJ old_f = SelectObject(dc, hfont);

            auto rgb_val = color::to_preview_rgba(to->fill).to_device();
            COLORREF text_color = RGB(rgb_val.r, rgb_val.g, rgb_val.b);
            SetTextColor(dc, text_color);
            SetBkMode(dc, TRANSPARENT);

            int wlen = MultiByteToWideChar(CP_UTF8, 0, to->text.c_str(), -1, nullptr, 0);
            if (wlen > 1) {
                std::vector<wchar_t> wbuf(wlen);
                MultiByteToWideChar(CP_UTF8, 0, to->text.c_str(), -1, wbuf.data(), wlen);

                UINT flags = DT_LEFT | DT_NOPREFIX;
                if (to->alignment == typography::TextAlignment::Center) flags = DT_CENTER | DT_NOPREFIX;
                else if (to->alignment == typography::TextAlignment::Right) flags = DT_RIGHT | DT_NOPREFIX;

                auto b = to->bounds();
                auto b_screen = camera.world_to_screen({to->position.x + b.width, to->position.y + b.height});
                int sw = std::max(40, static_cast<int>(std::ceil(b_screen.x - pt_screen.x)));
                int sh = std::max(20, static_cast<int>(std::ceil(b_screen.y - pt_screen.y)));
                RECT rc_text{sx, sy, sx + sw, sy + sh};
                DrawTextW(dc, wbuf.data(), -1, &rc_text, flags);
            }

            SelectObject(dc, old_f);
            DeleteObject(hfont);
        }
    }

    // 2. Render active on-canvas inline text editing session
    if (is_inline_text_editing_) {
        auto pt_screen = camera.world_to_screen(inline_text_pos_mm_);
        int sx = area.left + static_cast<int>(std::floor(pt_screen.x));
        int sy = area.top + static_cast<int>(std::floor(pt_screen.y));

        int font_px = std::max(10, static_cast<int>(std::lround(font_size_pt_ * camera.zoom() * 1.333)));
        int weight = font_bold_ ? 700 : 400;
        BOOL italic = font_italic_ ? TRUE : FALSE;
        BOOL underline = font_underline_ ? TRUE : FALSE;

        const char* fam_names[] = { "Be Vietnam Pro", "Segoe UI", "Arial", "Times New Roman", "JetBrains Mono" };
        std::string fam_name = fam_names[font_family_index_ % 5];
        std::wstring wfamily(fam_name.begin(), fam_name.end());

        HFONT hfont = CreateFontW(
            font_px, 0, 0, 0, weight, italic, underline, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            wfamily.c_str()
        );
        HGDIOBJ old_f = SelectObject(dc, hfont);

        std::string display_str = inline_text_buffer_.empty()
            ? (language_ == UILanguage::Vietnamese ? "Gõ văn bản tại đây..." : "Type text here...")
            : inline_text_buffer_;

        int wlen = MultiByteToWideChar(CP_UTF8, 0, display_str.c_str(), -1, nullptr, 0);
        std::vector<wchar_t> wbuf(std::max(2, wlen));
        if (wlen > 1) {
            MultiByteToWideChar(CP_UTF8, 0, display_str.c_str(), -1, wbuf.data(), wlen);
        }

        SIZE text_sz{120, font_px + 8};
        if (wlen > 1) {
            GetTextExtentPoint32W(dc, wbuf.data(), static_cast<int>(wcslen(wbuf.data())), &text_sz);
        }

        int box_w = std::max(160, static_cast<int>(text_sz.cx) + 20);
        int box_h = std::max(font_px + 12, static_cast<int>(text_sz.cy) + 8);
        RECT rc_box{sx - 4, sy - 4, sx + box_w, sy + box_h};

        HBRUSH bg_br = CreateSolidBrush(theme_ == ThemeMode::CyberDark ? RGB(22, 30, 44) : RGB(255, 255, 255));
        HPEN bdr_pen = CreatePen(PS_DASH, 1, RGB(0, 242, 195));
        HGDIOBJ op_bg = SelectObject(dc, bdr_pen);
        HGDIOBJ ob_bg = SelectObject(dc, bg_br);
        RoundRect(dc, rc_box.left, rc_box.top, rc_box.right, rc_box.bottom, 4, 4);
        SelectObject(dc, op_bg);
        SelectObject(dc, ob_bg);
        DeleteObject(bdr_pen);
        DeleteObject(bg_br);

        SetBkMode(dc, TRANSPARENT);
        if (inline_text_buffer_.empty()) {
            SetTextColor(dc, RGB(148, 163, 184));
        } else {
            SetTextColor(dc, theme_ == ThemeMode::CyberDark ? RGB(241, 245, 249) : RGB(15, 23, 42));
        }

        RECT rc_inner{sx, sy, sx + box_w - 8, sy + box_h};
        UINT flags = DT_LEFT | DT_NOPREFIX;
        if (font_alignment_ == typography::TextAlignment::Center) flags = DT_CENTER | DT_NOPREFIX;
        else if (font_alignment_ == typography::TextAlignment::Right) flags = DT_RIGHT | DT_NOPREFIX;
        DrawTextW(dc, wbuf.data(), -1, &rc_inner, flags);

        // Blinking caret vertical bar
        HPEN caret_pen = CreatePen(PS_SOLID, 2, RGB(0, 242, 195));
        HGDIOBJ old_cp = SelectObject(dc, caret_pen);
        int caret_x = sx + (inline_text_buffer_.empty() ? 2 : static_cast<int>(text_sz.cx) + 2);
        MoveToEx(dc, caret_x, sy + 2, nullptr);
        LineTo(dc, caret_x, sy + font_px + 2);
        SelectObject(dc, old_cp);
        DeleteObject(caret_pen);

        SelectObject(dc, old_f);
        DeleteObject(hfont);

        HFONT tip_f = CreateFontW(11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HGDIOBJ old_tf = SelectObject(dc, tip_f);
        SetTextColor(dc, RGB(100, 116, 139));
        const wchar_t* tip = (language_ == UILanguage::Vietnamese)
            ? L"↵ Enter: Hoàn tất   ·   Shift+↵: Xuống dòng   ·   Esc: Hủy"
            : L"↵ Enter: Commit   ·   Shift+↵: New line   ·   Esc: Cancel";
        RECT rc_tip{sx - 4, rc_box.bottom + 2, sx + 320, rc_box.bottom + 18};
        DrawTextW(dc, tip, -1, &rc_tip, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, old_tf);
        DeleteObject(tip_f);
    }

    RestoreDC(dc, saved);

    // Paint Breadcrumb Bar at the top of canvas when in isolation mode
    if (is_in_isolation_mode()) {
        int bar_h = 32;
        RECT rc_bar = { area.left, area.top, area.right, area.top + bar_h };
        HBRUSH br_bar = CreateSolidBrush(theme_ == ThemeMode::CyberDark ? RGB(15, 23, 42) : RGB(241, 245, 249));
        HPEN pen_bdr = CreatePen(PS_SOLID, 1, RGB(14, 165, 233));
        HGDIOBJ old_b = SelectObject(dc, br_bar);
        HGDIOBJ old_p = SelectObject(dc, pen_bdr);
        Rectangle(dc, rc_bar.left, rc_bar.top, rc_bar.right, rc_bar.bottom);
        SelectObject(dc, old_b);
        SelectObject(dc, old_p);
        DeleteObject(pen_bdr);
        DeleteObject(br_bar);

        HFONT bar_font = CreateFontW(12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT bar_font_reg = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HGDIOBJ old_f = SelectObject(dc, bar_font_reg);
        SetBkMode(dc, TRANSPARENT);

        // [ 🏠 Main Document ] button
        RECT rc_home = { area.left + 8, area.top + 4, area.left + 140, area.top + bar_h - 4 };
        HBRUSH br_pill = CreateSolidBrush(theme_ == ThemeMode::CyberDark ? RGB(30, 41, 59) : RGB(226, 232, 240));
        FillRect(dc, &rc_home, br_pill);
        DeleteObject(br_pill);
        SetTextColor(dc, RGB(14, 165, 233));
        const wchar_t* home_txt = language_ == UILanguage::Vietnamese ? L"🏠 Tài liệu chính" : L"🏠 Main Document";
        DrawTextW(dc, home_txt, -1, &rc_home, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Separator ❯
        RECT rc_sep = { area.left + 144, area.top + 4, area.left + 160, area.top + bar_h - 4 };
        SetTextColor(dc, RGB(148, 163, 184));
        DrawTextW(dc, L"❯", -1, &rc_sep, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // [ 📁 Group (N objects) ]
        auto cur_id = *current_isolated_group_id();
        std::wstring grp_info = language_ == UILanguage::Vietnamese ? L"📁 Nhóm #" : L"📁 Group #";
        grp_info += std::to_wstring(cur_id);
        const auto grp_obj = snapshot->objects().get(cur_id);
        if (grp_obj && std::holds_alternative<document::GroupObject>(*grp_obj)) {
            const auto& g = std::get<document::GroupObject>(*grp_obj);
            grp_info += L" (" + std::to_wstring(g.children_ids.size()) +
                (language_ == UILanguage::Vietnamese ? L" đối tượng)" : L" objects)");
        }
        RECT rc_grp = { area.left + 165, area.top + 4, area.left + 380, area.top + bar_h - 4 };
        SelectObject(dc, bar_font);
        SetTextColor(dc, theme_ == ThemeMode::CyberDark ? RGB(0, 242, 195) : RGB(2, 132, 199));
        DrawTextW(dc, grp_info.c_str(), -1, &rc_grp, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // [ ✕ Exit Group (Esc) ] button at far right
        RECT rc_exit = { area.right - 150, area.top + 4, area.right - 8, area.top + bar_h - 4 };
        HBRUSH br_exit = CreateSolidBrush(RGB(239, 68, 68));
        HPEN pen_exit = CreatePen(PS_SOLID, 1, RGB(220, 38, 38));
        SelectObject(dc, br_exit);
        SelectObject(dc, pen_exit);
        RoundRect(dc, rc_exit.left, rc_exit.top, rc_exit.right, rc_exit.bottom, 4, 4);
        DeleteObject(pen_exit);
        DeleteObject(br_exit);
        SetTextColor(dc, RGB(255, 255, 255));
        const wchar_t* exit_txt = language_ == UILanguage::Vietnamese ? L"✕ Thoát nhóm (Esc)" : L"✕ Exit Group (Esc)";
        DrawTextW(dc, exit_txt, -1, &rc_exit, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(dc, old_f);
        DeleteObject(bar_font);
        DeleteObject(bar_font_reg);
    }

    paint_canvas_guidelines(dc, width, height);
    paint_canvas_rulers(dc, width, height);
}

void NativeWindow::paint_canvas_guidelines(HDC dc, int width, int height) {
    const RECT area = canvas_rect(width, height);
    if (area.right <= area.left || area.bottom <= area.top) return;

    auto& camera = root().renderer().camera();
    const double zoom = camera.zoom();
    if (zoom <= 0.0) return;

    const bool is_dark = (theme_ == ThemeMode::CyberDark);
    const COLORREF col_guide = is_dark ? RGB(0, 242, 255) : RGB(2, 132, 199);
    const COLORREF col_drag_guide = is_dark ? RGB(56, 189, 248) : RGB(14, 165, 233);
    const COLORREF col_tag_bg = is_dark ? RGB(15, 23, 42) : RGB(241, 245, 249);

    int saved = SaveDC(dc);
    IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);

    // 1. Draw existing guidelines if show_guidelines_ is true
    if (show_guidelines_) {
        HPEN guide_pen = CreatePen(PS_DOT, 1, col_guide);
        HGDIOBJ old_pen = SelectObject(dc, guide_pen);
        HFONT guide_font = font_secondary_ ? font_secondary_ : font_be_vietnam_;
        HGDIOBJ old_font = SelectObject(dc, guide_font);
        SetBkMode(dc, TRANSPARENT);

        for (const auto& g : guidelines_) {
            if (g.orientation == GuidelineOrientation::Horizontal) {
                const auto screen_pt = camera.world_to_screen({0.0, g.position_mm});
                const int sy = area.top + static_cast<int>(std::lround(screen_pt.y));
                if (sy >= area.top && sy <= area.bottom) {
                    MoveToEx(dc, area.left, sy, nullptr);
                    LineTo(dc, area.right, sy);

                    // Measurement tag near left ruler
                    std::wostringstream wss;
                    wss << L"Y " << std::fixed << std::setprecision(1) << g.position_mm << L"mm";
                    std::wstring tag = wss.str();

                    RECT rc_tag = { area.left + 4, sy - 14, area.left + 78, sy - 1 };
                    HBRUSH tag_br = CreateSolidBrush(col_tag_bg);
                    FillRect(dc, &rc_tag, tag_br);
                    DeleteObject(tag_br);
                    SetTextColor(dc, col_guide);
                    DrawTextW(dc, tag.c_str(), -1, &rc_tag, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
            } else {
                const auto screen_pt = camera.world_to_screen({g.position_mm, 0.0});
                const int sx = area.left + static_cast<int>(std::lround(screen_pt.x));
                if (sx >= area.left && sx <= area.right) {
                    MoveToEx(dc, sx, area.top, nullptr);
                    LineTo(dc, sx, area.bottom);

                    // Measurement tag near top ruler
                    std::wostringstream wss;
                    wss << L"X " << std::fixed << std::setprecision(1) << g.position_mm << L"mm";
                    std::wstring tag = wss.str();

                    RECT rc_tag = { sx + 2, area.top + 2, sx + 76, area.top + 15 };
                    HBRUSH tag_br = CreateSolidBrush(col_tag_bg);
                    FillRect(dc, &rc_tag, tag_br);
                    DeleteObject(tag_br);
                    SetTextColor(dc, col_guide);
                    DrawTextW(dc, tag.c_str(), -1, &rc_tag, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
            }
        }

        SelectObject(dc, old_font);
        SelectObject(dc, old_pen);
        DeleteObject(guide_pen);
    }

    // 2. Draw live dragging guideline if currently dragging from ruler
    if (is_dragging_guideline_) {
        HPEN drag_pen = CreatePen(PS_DASH, 1, col_drag_guide);
        HGDIOBJ old_pen = SelectObject(dc, drag_pen);
        HFONT badge_font = font_be_vietnam_bold_ ? font_be_vietnam_bold_ : font_be_vietnam_;
        HGDIOBJ old_font = SelectObject(dc, badge_font);

        if (drag_guideline_orientation_ == GuidelineOrientation::Horizontal) {
            const auto screen_pt = camera.world_to_screen({0.0, drag_guideline_pos_mm_});
            const int sy = area.top + static_cast<int>(std::lround(screen_pt.y));
            if (sy >= area.top && sy <= area.bottom) {
                MoveToEx(dc, area.left, sy, nullptr);
                LineTo(dc, area.right, sy);

                // Live floating coordinate badge
                std::wostringstream wss;
                wss << (language_ == UILanguage::Vietnamese ? L"Gióng ngang Y: " : L"Horiz Guide Y: ")
                    << std::fixed << std::setprecision(1) << drag_guideline_pos_mm_ << L" mm";
                std::wstring badge = wss.str();

                RECT rc_calc = {0, 0, 0, 0};
                DrawTextW(dc, badge.c_str(), -1, &rc_calc, DT_CALCRECT | DT_NOPREFIX);
                int bw = rc_calc.right - rc_calc.left + 16;
                int bh = rc_calc.bottom - rc_calc.top + 8;
                int bx = area.left + 80;
                int by = sy - bh - 4;
                if (by < area.top + 4) by = sy + 6;

                RECT rc_badge = { bx, by, bx + bw, by + bh };
                HBRUSH br_bg = CreateSolidBrush(RGB(15, 23, 42));
                HPEN pen_bdr = CreatePen(PS_SOLID, 1, RGB(0, 242, 195));
                HGDIOBJ old_bp = SelectObject(dc, pen_bdr);
                HGDIOBJ old_bb = SelectObject(dc, br_bg);
                RoundRect(dc, rc_badge.left, rc_badge.top, rc_badge.right, rc_badge.bottom, 4, 4);
                SelectObject(dc, old_bp);
                SelectObject(dc, old_bb);
                DeleteObject(pen_bdr);
                DeleteObject(br_bg);

                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, RGB(0, 242, 195));
                RECT rc_txt = { bx + 8, by + 4, bx + bw - 8, by + bh - 4 };
                DrawTextW(dc, badge.c_str(), -1, &rc_txt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
        } else {
            const auto screen_pt = camera.world_to_screen({drag_guideline_pos_mm_, 0.0});
            const int sx = area.left + static_cast<int>(std::lround(screen_pt.x));
            if (sx >= area.left && sx <= area.right) {
                MoveToEx(dc, sx, area.top, nullptr);
                LineTo(dc, sx, area.bottom);

                // Live floating coordinate badge
                std::wostringstream wss;
                wss << (language_ == UILanguage::Vietnamese ? L"Gióng dọc X: " : L"Vert Guide X: ")
                    << std::fixed << std::setprecision(1) << drag_guideline_pos_mm_ << L" mm";
                std::wstring badge = wss.str();

                RECT rc_calc = {0, 0, 0, 0};
                DrawTextW(dc, badge.c_str(), -1, &rc_calc, DT_CALCRECT | DT_NOPREFIX);
                int bw = rc_calc.right - rc_calc.left + 16;
                int bh = rc_calc.bottom - rc_calc.top + 8;
                int bx = sx + 8;
                int by = area.top + 30;
                if (bx + bw > area.right - 8) bx = sx - bw - 8;

                RECT rc_badge = { bx, by, bx + bw, by + bh };
                HBRUSH br_bg = CreateSolidBrush(RGB(15, 23, 42));
                HPEN pen_bdr = CreatePen(PS_SOLID, 1, RGB(0, 242, 195));
                HGDIOBJ old_bp = SelectObject(dc, pen_bdr);
                HGDIOBJ old_bb = SelectObject(dc, br_bg);
                RoundRect(dc, rc_badge.left, rc_badge.top, rc_badge.right, rc_badge.bottom, 4, 4);
                SelectObject(dc, old_bp);
                SelectObject(dc, old_bb);
                DeleteObject(pen_bdr);
                DeleteObject(br_bg);

                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, RGB(0, 242, 195));
                RECT rc_txt = { bx + 8, by + 4, bx + bw - 8, by + bh - 4 };
                DrawTextW(dc, badge.c_str(), -1, &rc_txt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
        }

        SelectObject(dc, old_font);
        SelectObject(dc, old_pen);
        DeleteObject(drag_pen);
    }

    RestoreDC(dc, saved);
}

void NativeWindow::paint_canvas_rulers(HDC dc, int width, int height) {
    const RECT area = canvas_rect(width, height);
    const auto& camera = root().renderer().camera();
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
    const auto& page = root().document().current_snapshot()->page();

    const COLORREF base = theme_ == ThemeMode::CyberDark ? RGB(15, 23, 34) : RGB(226, 232, 240);
    const COLORREF tick = theme_ == ThemeMode::CyberDark ? RGB(82, 98, 118) : RGB(100, 116, 139);
    const COLORREF text = theme_ == ThemeMode::CyberDark ? RGB(171, 185, 202) : RGB(71, 85, 105);
    HBRUSH base_brush = CreateSolidBrush(base);
    RECT top_ruler{area.left, 102, area.right, area.top};
    RECT left_ruler{52, area.top, area.left, area.bottom};
    RECT corner{52, 102, area.left, area.top};
    FillRect(dc, &top_ruler, base_brush);
    FillRect(dc, &left_ruler, base_brush);
    FillRect(dc, &corner, base_brush);
    DeleteObject(base_brush);

    HPEN line = CreatePen(PS_SOLID, 1, tick);
    HGDIOBJ old_pen = SelectObject(dc, line);
    ensure_ui_fonts();
    HFONT ruler_font = font_ruler_;
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
            TextOutW(dc, x + 2, 103, label.c_str(), static_cast<int>(label.size()));
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

    // Guideline Tick Markers on Rulers
    if (show_guidelines_) {
        HPEN guide_marker_pen = CreatePen(PS_SOLID, 2, RGB(0, 242, 195));
        HGDIOBJ old_gp = SelectObject(dc, guide_marker_pen);
        for (const auto& g : guidelines_) {
            if (g.orientation == GuidelineOrientation::Vertical) {
                const auto screen = camera.world_to_screen({g.position_mm, 0.0});
                const int x = area.left + static_cast<int>(std::lround(screen.x));
                if (x >= area.left && x <= area.right) {
                    MoveToEx(dc, x, area.top - 18, nullptr);
                    LineTo(dc, x, area.top);
                }
            } else {
                const auto screen = camera.world_to_screen({0.0, g.position_mm});
                const int y = area.top + static_cast<int>(std::lround(screen.y));
                if (y >= area.top && y <= area.bottom) {
                    MoveToEx(dc, area.left - 18, y, nullptr);
                    LineTo(dc, area.left, y);
                }
            }
        }
        SelectObject(dc, old_gp);
        DeleteObject(guide_marker_pen);
    }

    SelectObject(dc, old_font);
    SelectObject(dc, old_pen);
    DeleteObject(line);
}

void NativeWindow::handle_canvas_mouse_down(int x, int y) {
    if (!is_canvas_point(x, y))
        return;
    SetCapture(hwnd_);
    if (active_tool_ == 3) {
        is_panning_ = true;
        last_pointer_x_ = x;
        last_pointer_y_ = y;
        return;
    }
    pointer_active_ = true;
    pointer_down_world_ = canvas_to_world(x, y);
    pointer_current_world_ = pointer_down_world_;
    drag_preview_delta_ = {};
    drag_preview_bounds_.reset();
    const auto snapshot = root().document().current_snapshot();
    pointer_start_revision_ = snapshot->revision();

    if (active_tool_ == 0) {
        auto& controller = root().vector_controller();
#ifdef _WIN32
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
#else
        const bool shift = false;
#endif
        auto hit_id = vector::SelectionModel::hit_test(pointer_down_world_, *snapshot);
        if (is_in_isolation_mode()) {
            if (hit_id && !is_object_in_isolated_group(*hit_id)) {
                hit_id = std::nullopt; // Outside objects cannot be selected in isolation mode
            }
        } else {
            if (hit_id) {
                // If clicked object is inside a group, select top parent group
                document::ObjectId cur = *hit_id;
                while (auto parent = workflow().find_parent_group(cur)) {
                    cur = *parent;
                }
                hit_id = cur;
            }
        }

        if (hit_id) {
            if (shift) {
                controller.selection().select(*hit_id, vector::SelectionMode::Toggle);
            } else if (!controller.selection().is_selected(*hit_id)) {
                controller.selection().select(*hit_id, vector::SelectionMode::Replace);
            }

            // Expand gesture target IDs to include all children of any selected group
            std::vector<document::ObjectId> gesture_ids;
            for (const auto id : controller.selection().selected_ids()) {
                gesture_ids.push_back(id);
                if (workflow().is_group(id)) {
                    std::function<void(document::ObjectId)> add_desc = [&](document::ObjectId gid) {
                        auto obj = snapshot->objects().get(gid);
                        if (!obj) return;
                        if (auto* g = std::get_if<document::GroupObject>(&*obj)) {
                            for (auto cid : g->children_ids) {
                                gesture_ids.push_back(cid);
                                add_desc(cid);
                            }
                        }
                    };
                    add_desc(id);
                }
            }
            controller.gesture().begin(pointer_down_world_, gesture_ids);
        } else {
            if (!shift) {
                controller.selection().clear();
            }
        }
        is_dragging_ = controller.gesture().is_active();
        if (is_dragging_) {
            for (const auto id : controller.selection().selected_ids()) {
                if (const auto bounds = workflow().get_object_bounds(id))
                    drag_preview_bounds_ = drag_preview_bounds_
                        ? drag_preview_bounds_->united(*bounds) : *bounds;
            }
        }
    } else if (active_tool_ == 5) {
        is_knife_cutting_ = true;
        knife_start_world_ = pointer_down_world_;
        knife_current_world_ = pointer_down_world_;
    } else if (active_tool_ == 4) {
        // TEXT TOOL: click to start typing directly on canvas!
        if (is_inline_text_editing_) {
            commit_inline_text_editing();
        }
        auto hit_id = vector::SelectionModel::hit_test(pointer_down_world_, *snapshot);
        if (hit_id) {
            auto opt = snapshot->objects().get(*hit_id);
            if (opt && std::holds_alternative<document::TextObject>(*opt)) {
                start_inline_text_editing(pointer_down_world_, *hit_id);
            } else {
                start_inline_text_editing(pointer_down_world_, document::kInvalidObjectId);
            }
        } else {
            start_inline_text_editing(pointer_down_world_, document::kInvalidObjectId);
        }
    } else {
        if (is_inline_text_editing_) {
            commit_inline_text_editing();
        }
        is_creating_shape_ = true;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativeWindow::handle_canvas_mouse_move(int x, int y) {
    if (is_canvas_point(x, y)) {
        const auto point = canvas_to_world(x, y);
        cursor_mm_x_ = point.x;
        cursor_mm_y_ = point.y;
    }
    if (is_panning_) {
        root().renderer().camera().pan_pixels(
            static_cast<double>(x - last_pointer_x_),
            static_cast<double>(y - last_pointer_y_));
        last_pointer_x_ = x;
        last_pointer_y_ = y;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (!pointer_active_) {
        RECT rc_coord{16, height_ - 26, 215, height_};
        InvalidateRect(hwnd_, &rc_coord, FALSE);
        return;
    }

    pointer_current_world_ = canvas_to_world(x, y);
    const auto& page_snap = root().document().current_snapshot()->page();
    const double half_canvas = geometry::kMaxCanvasDimensionMm * 0.5;
    const double min_canvas_x = page_snap.width_mm * 0.5 - half_canvas;
    const double max_canvas_x = page_snap.width_mm * 0.5 + half_canvas;
    const double min_canvas_y = page_snap.height_mm * 0.5 - half_canvas;
    const double max_canvas_y = page_snap.height_mm * 0.5 + half_canvas;
    pointer_current_world_.x = std::clamp(pointer_current_world_.x, min_canvas_x, max_canvas_x);
    pointer_current_world_.y = std::clamp(pointer_current_world_.y, min_canvas_y, max_canvas_y);

    if (is_knife_cutting_) {
        knife_current_world_ = pointer_current_world_;
    } else if (is_dragging_) {
        (void)root().vector_controller().on_pointer_move(pointer_current_world_);
        drag_preview_delta_ = {pointer_current_world_.x - pointer_down_world_.x,
                                pointer_current_world_.y - pointer_down_world_.y};
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativeWindow::handle_canvas_mouse_up(int x, int y) {
    if (!pointer_active_)
        return;
    pointer_current_world_ = canvas_to_world(x, y);
    const auto& page_snap = root().document().current_snapshot()->page();
    const double half_canvas = geometry::kMaxCanvasDimensionMm * 0.5;
    const double min_canvas_x = page_snap.width_mm * 0.5 - half_canvas;
    const double max_canvas_x = page_snap.width_mm * 0.5 + half_canvas;
    const double min_canvas_y = page_snap.height_mm * 0.5 - half_canvas;
    const double max_canvas_y = page_snap.height_mm * 0.5 + half_canvas;
    pointer_current_world_.x = std::clamp(pointer_current_world_.x, min_canvas_x, max_canvas_x);
    pointer_current_world_.y = std::clamp(pointer_current_world_.y, min_canvas_y, max_canvas_y);

    bool changed = false;
    if (is_knife_cutting_) {
        knife_current_world_ = pointer_current_world_;
        if (workflow().split_objects_by_line(knife_start_world_, knife_current_world_)) {
            changed = true;
            status_msg_ = (language_ == UILanguage::Vietnamese)
                ? "Đã dùng dao cắt phân tách hình khối thành công · Ctrl+Z để hoàn tác"
                : "Knife split completed · Ctrl+Z to undo";
        }
    } else if (is_creating_shape_) {
        auto bounds = bounds_between(pointer_down_world_, pointer_current_world_);
        bounds.width = std::min(bounds.width, geometry::kMaxObjectDimensionMm);
        bounds.height = std::min(bounds.height, geometry::kMaxObjectDimensionMm);
        if (bounds.width > 1.0 && bounds.height > 1.0) {
            document::ObjectId id = document::kInvalidObjectId;
            if (active_tool_ == 1) {
                id = workflow().add_rectangle(bounds.x, bounds.y, bounds.width, bounds.height,
                                             "#00f2c3", "#0ea5e9", 2.0);
            } else if (active_tool_ == 2) {
                id = workflow().add_ellipse(bounds.x + bounds.width * 0.5,
                                           bounds.y + bounds.height * 0.5,
                                           bounds.width * 0.5, bounds.height * 0.5,
                                           "#00f2c3", "#0ea5e9", 2.0);
            } else if (active_tool_ == 4) {
                id = workflow().add_vietnamese_text(bounds.x, bounds.y,
                                                   "PikDraw 2026", "Be Vietnam Pro", 24.0, "#00f2c3");
            }
            if (id != document::kInvalidObjectId) {
                if (is_in_isolation_mode()) {
                    auto iso_id = *current_isolated_group_id();
                    auto snap = root().document().current_snapshot();
                    auto obj = snap->objects().get(iso_id);
                    if (obj && std::holds_alternative<document::GroupObject>(*obj)) {
                        document::GroupObject updated_grp = std::get<document::GroupObject>(*obj);
                        updated_grp.children_ids.push_back(id);
                        auto map = snap->objects().set(iso_id, std::move(updated_grp));
                        root().document().apply_mutation(snap->page(), std::move(map));
                    }
                }
                root().vector_controller().selection().select(id);
                changed = true;
            }
        }
    } else if (is_dragging_) {
        const auto result = root().vector_controller().on_pointer_up(
            pointer_current_world_, root().document());
        changed = result.success &&
            root().document().current_snapshot()->revision() != pointer_start_revision_;
    }

    pointer_active_ = false;
    is_dragging_ = false;
    is_creating_shape_ = false;
    is_knife_cutting_ = false;
    drag_preview_bounds_.reset();
    drag_preview_delta_ = {};
    if (GetCapture() == hwnd_)
        ReleaseCapture();
    workflow().sync_status_bar();
    if (!changed) {
        status_msg_ = (language_ == UILanguage::Vietnamese ? "Sẵn sàng" : "Ready");
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

#endif

} // namespace pikdraw::desktop
