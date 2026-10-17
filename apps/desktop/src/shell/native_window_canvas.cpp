#include "pikdraw/desktop/shell/native_window.hpp"
#include "../tools/drawing_tool_controller.hpp"
#include "../tools/pen_tool_controller.hpp"
#include "../tools/node_tool_controller.hpp"
#include "../tools/contour_tool_controller.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numbers>
#include <sstream>
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
    const auto snapshot = workflow().snapshot_for_canvas();

    // Check if the canvas bitmap cache needs to be regenerated
    const bool cache_needs_rebuild = !canvas_cache_valid_
        || !cached_canvas_dc_
        || canvas_width != cached_canvas_w_
        || canvas_height != cached_canvas_h_
        || snapshot != cached_canvas_snapshot_
        || color_preview_ != cached_canvas_profile_
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

        // Ensure backend rasterizer clears with active theme's pasteboard color
        root().renderer().backend().set_canvas_background(canvas_background_color());

        // Fill background of cached canvas
        RECT local_area{0, 0, canvas_width, canvas_height};
        HBRUSH background = CreateSolidBrush(canvas_background_colorref());
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
            ? RGB(24, 28, 36) : RGB(148, 163, 184));
        RECT shadow_right{page_right, page_top + 6, page_right + 6, page_bottom + 6};
        RECT shadow_bottom{page_left + 6, page_bottom, page_right + 6, page_bottom + 6};
        FillRect(cached_canvas_dc_, &shadow_right, shadow);
        FillRect(cached_canvas_dc_, &shadow_bottom, shadow);
        DeleteObject(shadow);

        // Draw crisp 1px page border so white paper sheet is clearly delimited
        HPEN page_border = CreatePen(PS_SOLID, 1, theme_ == ThemeMode::CyberDark
            ? RGB(50, 56, 68) : RGB(203, 213, 225));
        HGDIOBJ old_pen = SelectObject(cached_canvas_dc_, page_border);
        HGDIOBJ old_brush = SelectObject(cached_canvas_dc_, GetStockObject(NULL_BRUSH));
        Rectangle(cached_canvas_dc_, page_left, page_top, page_right + 1, page_bottom + 1);
        SelectObject(cached_canvas_dc_, old_brush);
        SelectObject(cached_canvas_dc_, old_pen);
        DeleteObject(page_border);

        // Update cached parameters
        cached_revision_ = snapshot->revision();
        cached_canvas_snapshot_ = snapshot;
        cached_canvas_profile_ = color_preview_;
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
                        COLORREF wash_col = (theme_ == ThemeMode::CyberDark) ? RGB(68, 72, 80) : RGB(255, 255, 255);
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
        const auto preview = shape_drag_bounds(shape_drag_modifiers_);
        if (active_tool_ == 9) {
            const auto center = preview.center();
            const double radius = std::min(preview.width, preview.height) * 0.5;
            const double rotation = shape_drag_polygon_rotation(shape_drag_modifiers_) *
                                    std::numbers::pi / 180.0;
            std::vector<POINT> vertices;
            vertices.reserve(static_cast<size_t>(polygon_sides_));
            for (int side = 0; side < polygon_sides_; ++side) {
                const double angle = rotation + 2.0 * std::numbers::pi * side / polygon_sides_;
                const auto screen = camera.world_to_screen({
                    center.x + radius * std::cos(angle), center.y + radius * std::sin(angle)});
                vertices.push_back({area.left + static_cast<LONG>(std::lround(screen.x)),
                                     area.top + static_cast<LONG>(std::lround(screen.y))});
            }
            HPEN pen = CreatePen(PS_DASH, 1, RGB(0, 242, 195));
            const HGDIOBJ old_pen = SelectObject(dc, pen);
            const HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            ::Polygon(dc, vertices.data(), static_cast<int>(vertices.size()));
            SelectObject(dc, old_brush);
            SelectObject(dc, old_pen);
            DeleteObject(pen);
        } else {
            draw_outline(dc, camera, preview, area.left, area.top,
                         RGB(0, 242, 195), PS_DASH, active_tool_ == 2);
        }
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

    paint_drawing_overlay(dc);
    const auto& diagnostics = root().renderer().last_metric().diagnostics;
    const auto errors = std::count_if(diagnostics.begin(), diagnostics.end(),
        [](const auto& item) { return item.error; });
    const auto draft = workflow().effect_draft();
    const auto& selected_ids = root().vector_controller().selection().selected_ids();
    const bool draft_preview_error = draft && draft->preview_enabled &&
        std::any_of(diagnostics.begin(), diagnostics.end(), [&](const auto& item) {
            return item.error &&
                (item.object_id == document::kInvalidObjectId ||
                 std::find(selected_ids.begin(), selected_ids.end(), item.object_id) != selected_ids.end());
        });
    const bool authored_effect_error = snapshot && std::any_of(
        diagnostics.begin(), diagnostics.end(), [&](const auto& item) {
            if (!item.error || item.object_id == document::kInvalidObjectId) return false;
            const auto object = snapshot->objects().get(item.object_id);
            if (!object) return false;
            return std::visit([](const auto &value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, document::PathObject> ||
                              std::is_same_v<T, document::TextObject>)
                    return !value.appearance.effects.empty();
                return false;
            }, *object);
        });
    const bool effect_render_error = draft_preview_error || authored_effect_error;
    const bool icc_error = color_preview_ && color_preview_->conversion_failure_count() > 0;
    if (errors > 0 || icc_error) {
        const wchar_t* warning = effect_render_error ? (language_ == UILanguage::Vietnamese
            ? (draft_preview_error
                ? L"Xem trước hiệu ứng vượt giới hạn dựng hiện tại. Giảm Sigma/lệch hoặc zoom rồi thử lại; bản nguồn chưa đổi."
                : L"Không thể dựng hiệu ứng trong giới hạn hiện tại. Giảm Sigma/lệch hoặc zoom; dữ liệu nguồn vẫn được giữ.")
            : (draft_preview_error
                ? L"Effect preview exceeds current render limits. Reduce Sigma/offset or zoom and retry; source is unchanged."
                : L"An effect exceeds current render limits. Reduce Sigma/offset or zoom; source data is preserved."))
            : icc_error ? (language_ == UILanguage::Vietnamese
            ? L"ICC preview có lỗi; một số màu đang được hiển thị gần đúng."
            : L"ICC preview failed for some colors; approximate preview is active.")
            : language_ == UILanguage::Vietnamese
            ? L"Một số đối tượng chưa dựng được. Dữ liệu nguồn được giữ nguyên."
            : L"Some objects could not be displayed. Source data is preserved.";
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(205, 125, 45));
        TextOutW(dc, area.left + 12, area.bottom - 24, warning,
                 static_cast<int>(wcslen(warning)));
        if (effect_render_error)
            status_msg_ = language_ == UILanguage::Vietnamese
                ? (draft_preview_error
                    ? "Preview vượt giới hạn render · giảm Sigma/lệch hoặc zoom · tài liệu chưa đổi"
                    : "Hiệu ứng vượt giới hạn render · giảm Sigma/lệch hoặc zoom · nguồn được giữ")
                : (draft_preview_error
                    ? "Preview exceeds render limits · reduce Sigma/offset or zoom · document unchanged"
                    : "Effect exceeds render limits · reduce Sigma/offset or zoom · source preserved");
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
    paint_selection_overlay(dc);
}

void NativeWindow::paint_drawing_overlay(HDC dc) {
    if (active_tool_ == 8 && pen_tool_ && pen_tool_->active())
        paint_pen_overlay(dc);
    if (active_tool_ == 11 && node_tool_ && node_tool_->active())
        paint_node_canvas_overlay(dc, width_, height_);
    if (active_tool_ == 12 && contour_tool_ && contour_tool_->active())
        paint_contour_canvas_overlay(dc, canvas_rect(width_, height_));
    if (!drawing_tool_) return;
    const auto state = drawing_tool_->visual_state();
    const RECT area = canvas_rect(width_, height_);
    const auto& camera = root().renderer().camera();
    const auto screen = [&](geometry::Point2D point) {
        const auto local = camera.world_to_screen(point);
        return POINT{area.left + static_cast<LONG>(std::lround(local.x)),
                     area.top + static_cast<LONG>(std::lround(local.y))};
    };
    if (state.active && active_tool_ != 8) {
        HPEN source_pen = CreatePen(PS_SOLID, 2, RGB(0, 242, 195));
        HGDIOBJ old_pen = SelectObject(dc, source_pen);
        for (size_t index = 1; index < state.points.size(); ++index) {
            const auto first = screen(state.points[index - 1]);
            const auto second = screen(state.points[index]);
            MoveToEx(dc, first.x, first.y, nullptr);
            LineTo(dc, second.x, second.y);
        }
        if (state.candidate && !state.points.empty()) {
            HPEN preview_pen = CreatePen(PS_DASH, 1, RGB(0, 196, 255));
            SelectObject(dc, preview_pen);
            const auto start = screen(state.points.back());
            const auto end = screen(*state.candidate);
            MoveToEx(dc, start.x, start.y, nullptr);
            LineTo(dc, end.x, end.y);
            SelectObject(dc, source_pen);
            DeleteObject(preview_pen);
        }
        SelectObject(dc, old_pen);
        DeleteObject(source_pen);
        HBRUSH anchor_brush = CreateSolidBrush(RGB(0, 242, 195));
        HGDIOBJ old_brush = SelectObject(dc, anchor_brush);
        for (const auto point : state.points) {
            const auto marker = screen(point);
            Ellipse(dc, marker.x - 3, marker.y - 3, marker.x + 4, marker.y + 4);
        }
        SelectObject(dc, old_brush);
        DeleteObject(anchor_brush);
        if (state.candidate) {
            const auto marker = screen(*state.candidate);
            HPEN marker_pen = CreatePen(PS_SOLID, 2,
                state.snap_kind ? RGB(255, 190, 70) : RGB(0, 196, 255));
            HGDIOBJ previous_pen = SelectObject(dc, marker_pen);
            HGDIOBJ previous_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Ellipse(dc, marker.x - 6, marker.y - 6, marker.x + 7, marker.y + 7);
            MoveToEx(dc, marker.x - 9, marker.y, nullptr);
            LineTo(dc, marker.x + 10, marker.y);
            MoveToEx(dc, marker.x, marker.y - 9, nullptr);
            LineTo(dc, marker.x, marker.y + 10);
            SelectObject(dc, previous_brush);
            SelectObject(dc, previous_pen);
            DeleteObject(marker_pen);
        }

        if (state.candidate && !state.points.empty()) {
            const auto base_pt = state.points.back();
            const auto cand_pt = *state.candidate;
            const double delta_x = cand_pt.x - base_pt.x;
            const double delta_y = cand_pt.y - base_pt.y;
            const double cand_dist = std::hypot(delta_x, delta_y);
            if (cand_dist > 0.1) {
                constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
                constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
                double raw_deg = std::atan2(delta_y, delta_x) * kRadToDeg;
                if (raw_deg < 0.0) raw_deg += 360.0;
                double snap_deg = std::round(raw_deg / 15.0) * 15.0;
                if (snap_deg >= 360.0) snap_deg -= 360.0;
                double diff = std::abs(raw_deg - snap_deg);
                if (diff > 180.0) diff = 360.0 - diff;
                const bool is_snapped = (diff <= 0.5) || (active_num_field_ == NumericFieldId::PenAngle) || !pen_numeric_angle_.empty();

                if (is_snapped) {
                    const double ray_deg = (active_num_field_ == NumericFieldId::PenAngle || !pen_numeric_angle_.empty()) ? raw_deg : snap_deg;
                    const double rad = ray_deg * kDegToRad;
                    const geometry::Point2D ray_dir{std::cos(rad), std::sin(rad)};
                    const auto ray_s_world = base_pt - geometry::Point2D{ray_dir.x * 200.0, ray_dir.y * 200.0};
                    const auto ray_e_world = base_pt + geometry::Point2D{ray_dir.x * 3000.0, ray_dir.y * 3000.0};
                    const POINT rs = screen(ray_s_world);
                    const POINT re = screen(ray_e_world);
                    HPEN track_pen = CreatePen(PS_DOT, 1, theme_ == ThemeMode::CyberDark ? RGB(56, 189, 248) : RGB(14, 165, 233));
                    HGDIOBJ old_p = SelectObject(dc, track_pen);
                    MoveToEx(dc, rs.x, rs.y, nullptr);
                    LineTo(dc, re.x, re.y);
                    SelectObject(dc, old_p);
                    DeleteObject(track_pen);
                }

                const POINT end = screen(cand_pt);
                const int hud_w = 176;
                const int hud_h = 24;
                int bx = end.x + 16;
                int by = end.y - 28;
                if (bx + hud_w > area.right - 8) bx = end.x - hud_w - 16;
                if (bx < area.left + 8) bx = area.left + 8;
                if (by < area.top + 8) by = end.y + 16;
                if (by + hud_h > area.bottom - 8) by = area.bottom - hud_h - 8;

                RECT badge_rc{bx, by, bx + hud_w, by + hud_h};
                HBRUSH badge_bg = CreateSolidBrush(theme_ == ThemeMode::CyberDark ? RGB(15, 23, 42) : RGB(248, 250, 252));
                HPEN badge_pen = CreatePen(PS_SOLID, 1, theme_ == ThemeMode::CyberDark ? RGB(56, 189, 248) : RGB(14, 165, 233));
                HGDIOBJ old_bg = SelectObject(dc, badge_bg);
                HGDIOBJ old_pn = SelectObject(dc, badge_pen);
                RoundRect(dc, badge_rc.left, badge_rc.top, badge_rc.right, badge_rc.bottom, 4, 4);
                SelectObject(dc, old_bg);
                SelectObject(dc, old_pn);
                DeleteObject(badge_bg);
                DeleteObject(badge_pen);

                const bool focus_len = (active_num_field_ == NumericFieldId::PenLength);
                const bool focus_ang = (active_num_field_ == NumericFieldId::PenAngle);

                RECT rc_len{bx + 3, by + 2, bx + 85, by + hud_h - 2};
                RECT rc_ang{bx + 89, by + 2, bx + hud_w - 3, by + hud_h - 2};

                if (focus_len || focus_ang) {
                    RECT focused_rc = focus_len ? rc_len : rc_ang;
                    HPEN focus_pen = CreatePen(PS_SOLID, 1, RGB(0, 242, 195));
                    HGDIOBJ p = SelectObject(dc, focus_pen);
                    HGDIOBJ b = SelectObject(dc, GetStockObject(NULL_BRUSH));
                    RoundRect(dc, focused_rc.left, focused_rc.top, focused_rc.right, focused_rc.bottom, 2, 2);
                    SelectObject(dc, b);
                    SelectObject(dc, p);
                    DeleteObject(focus_pen);
                }

                std::wostringstream wlen, wang;
                if (focus_len) {
                    wlen << L"L: " << utf8_to_wide(num_input_buffer_) << L"|";
                } else if (!pen_numeric_length_.empty()) {
                    wlen << L"L: " << utf8_to_wide(pen_numeric_length_);
                } else {
                    wlen << L"L: " << std::fixed << std::setprecision(1) << cand_dist;
                }

                if (focus_ang) {
                    wang << L"∠ " << utf8_to_wide(num_input_buffer_) << L"°|";
                } else if (!pen_numeric_angle_.empty()) {
                    wang << L"∠ " << utf8_to_wide(pen_numeric_angle_) << L"°";
                } else {
                    wang << L"∠ " << std::fixed << std::setprecision(0) << (is_snapped ? snap_deg : raw_deg) << L"°";
                }

                HFONT hud_font = font_secondary_ ? font_secondary_ : font_be_vietnam_;
                HGDIOBJ old_f = SelectObject(dc, hud_font);
                SetBkMode(dc, TRANSPARENT);

                SetTextColor(dc, focus_len ? RGB(0, 242, 195) : (theme_ == ThemeMode::CyberDark ? RGB(226, 232, 240) : RGB(15, 23, 42)));
                const auto slen = wlen.str();
                DrawTextW(dc, slen.c_str(), -1, &rc_len, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                SetTextColor(dc, focus_ang ? RGB(0, 242, 195) : (theme_ == ThemeMode::CyberDark ? RGB(226, 232, 240) : RGB(15, 23, 42)));
                const auto sang = wang.str();
                DrawTextW(dc, sang.c_str(), -1, &rc_ang, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                SelectObject(dc, old_f);
            }
        }

        std::wostringstream hud;
        const auto kind = state.kind == vector::tools::line::PathDrawingKind::Line
            ? L"LINE" : L"POLYLINE";
        hud << kind << L"  ·  " << state.points.size() << L" nodes";
        if (state.candidate) {
            hud << L"  ·  X " << std::fixed << std::setprecision(2)
                << state.candidate->x << L" mm  Y " << state.candidate->y << L" mm";
            if (!state.points.empty()) {
                const auto delta = *state.candidate - state.points.back();
                hud << L"  ·  L " << std::hypot(delta.x, delta.y) << L" mm"
                    << L"  ∠ " << std::atan2(delta.y, delta.x) *
                        180.0 / 3.14159265358979323846 << L"°";
            }
        }
        if (!state.numeric_entry.empty())
            hud << L"  ·  = " << NativeWindow::utf8_to_wide(state.numeric_entry);
        const auto config = state.snap_config;
        hud << L"  ·  Snap " << (config.master_enabled ? L"ON" : L"OFF")
            << L"  ·  " << (config.direction.mode == vector::precision::snap::DirectionMode::Free
                ? L"Free" : config.direction.mode == vector::precision::snap::DirectionMode::Ortho
                ? L"Ortho" : L"Polar");
        if (state.snap_index_failed) hud << L"  ·  Snap unavailable; raw input active";
        else if (!state.snap_index_ready) hud << L"  ·  Index building";
        RECT text{area.left + 264, area.top + 10, area.right - 12, area.top + 36};
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(0, 242, 195));
        const auto line = hud.str();
        DrawTextW(dc, line.c_str(), -1, &text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        const wchar_t* hint = language_ == UILanguage::Vietnamese
            ? L"Nhấp: đặt điểm   Enter: kết thúc Polyline   Backspace: lùi điểm   Esc: hoàn tất/hủy"
            : L"Click: place point   Enter: finish Polyline   Backspace: undo point   Esc: finish/cancel";
        RECT hint_rect{area.left + 264, area.top + 34, area.right - 12, area.top + 54};
        SetTextColor(dc, RGB(150, 170, 190));
        DrawTextW(dc, hint, -1, &hint_rect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    if (drawing_tool_->snap_panel_open_value()) {
        const int left = area.left + 12, top = area.top + 12;
        RECT panel{left, top, left + 250, top + 264};
        HBRUSH panel_brush = CreateSolidBrush(theme_ == ThemeMode::CyberDark
            ? RGB(13, 22, 34) : RGB(250, 252, 255));
        HPEN panel_pen = CreatePen(PS_SOLID, 1, RGB(0, 196, 170));
        HGDIOBJ previous_brush = SelectObject(dc, panel_brush);
        HGDIOBJ previous_pen = SelectObject(dc, panel_pen);
        RoundRect(dc, panel.left, panel.top, panel.right, panel.bottom, 8, 8);
        SelectObject(dc, previous_brush);
        SelectObject(dc, previous_pen);
        DeleteObject(panel_brush);
        DeleteObject(panel_pen);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, theme_ == ThemeMode::CyberDark ? RGB(0, 242, 195) : RGB(0, 120, 95));
        RECT title{left + 12, top + 5, left + 238, top + 27};
        DrawTextW(dc, language_ == UILanguage::Vietnamese ? L"BẮT ĐIỂM & HƯỚNG"
                                                          : L"SNAP & DIRECTION",
                  -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        const auto config = state.snap_config;
        const COLORREF text_color = theme_ == ThemeMode::CyberDark
            ? RGB(220, 230, 240) : RGB(30, 41, 59);
        SetTextColor(dc, text_color);
        const auto option = [&](int y, const wchar_t* label, bool enabled) {
            RECT row{left + 10, top + y, left + 240, top + y + 22};
            HBRUSH row_brush = CreateSolidBrush(enabled ? RGB(18, 55, 60)
                : (theme_ == ThemeMode::CyberDark ? RGB(20, 31, 45) : RGB(235, 240, 246)));
            FillRect(dc, &row, row_brush);
            DeleteObject(row_brush);
            SetTextColor(dc, enabled ? RGB(0, 242, 195) : text_color);
            DrawTextW(dc, label, -1, &row, DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                      DT_NOPREFIX | DT_LEFT);
        };
        option(32, language_ == UILanguage::Vietnamese ? L"Bắt điểm tổng" : L"Master snapping",
               config.master_enabled);
        option(58, L"Endpoint", config.options[static_cast<size_t>(vector::precision::snap::SnapOption::Endpoint)]);
        option(82, L"Source node", config.options[static_cast<size_t>(vector::precision::snap::SnapOption::SourceNode)]);
        option(106, L"Line midpoint", config.options[static_cast<size_t>(vector::precision::snap::SnapOption::LineMidpoint)]);
        option(130, L"Grid", config.options[static_cast<size_t>(vector::precision::snap::SnapOption::Grid)]);
        const wchar_t* modes[] = {L"Free", L"Ortho", L"Polar"};
        for (int index = 0; index < 3; ++index) {
            RECT mode{left + 10 + index * 78, top + 156, left + 84 + index * 78, top + 178};
            const auto selected = static_cast<int>(config.direction.mode) == index;
            HBRUSH mode_brush = CreateSolidBrush(selected ? RGB(0, 125, 112)
                : (theme_ == ThemeMode::CyberDark ? RGB(20, 31, 45) : RGB(235, 240, 246)));
            FillRect(dc, &mode, mode_brush);
            DeleteObject(mode_brush);
            SetTextColor(dc, selected ? RGB(255, 255, 255) : text_color);
            DrawTextW(dc, modes[index], -1, &mode, DT_CENTER | DT_VCENTER |
                      DT_SINGLELINE | DT_NOPREFIX);
        }
        std::wostringstream polar;
        polar << L"Polar step: " << config.direction.polar.step_degrees << L"°  · click to cycle";
        RECT polar_row{left + 10, top + 182, left + 240, top + 204};
        SetTextColor(dc, text_color);
        DrawTextW(dc, polar.str().c_str(), -1, &polar_row,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        std::wostringstream profile_label;
        profile_label << L"Profile: " << NativeWindow::utf8_to_wide(state.profile)
            << L" · Shift lock "
            << (state.profile == "Corel" ? 15.0 : state.profile == "Custom"
                ? config.direction.polar.step_degrees : 45.0) << L"° · click";
        RECT profile{left + 10, top + 207, left + 240, top + 229};
        SetTextColor(dc, text_color);
        DrawTextW(dc, profile_label.str().c_str(), -1, &profile,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        RECT unsupported{left + 10, top + 232, left + 240, top + 256};
        SetTextColor(dc, RGB(150, 160, 175));
        DrawTextW(dc, language_ == UILanguage::Vietnamese
            ? L"Giao, tiếp tuyến, Bezier: chưa khả dụng"
            : L"Intersection, tangent, Bezier: unavailable",
            -1, &unsupported, DT_LEFT | DT_VCENTER | DT_SINGLELINE |
            DT_END_ELLIPSIS | DT_NOPREFIX);
    }
}

void NativeWindow::paint_pen_overlay(HDC dc) {
    if (!pen_tool_ || !pen_tool_->active()) return;
    const RECT area = canvas_rect(width_, height_);
    const auto& camera = root().renderer().camera();
    const auto screen = [&](geometry::Point2D source_point) {
        const auto world = pen_tool_->world_point_from_source(source_point);
        const auto local = camera.world_to_screen(world);
        return POINT{area.left + static_cast<LONG>(std::lround(local.x)),
                     area.top + static_cast<LONG>(std::lround(local.y))};
    };
    const auto screen_world = [&](geometry::Point2D world_point) {
        const auto local = camera.world_to_screen(world_point);
        return POINT{area.left + static_cast<LONG>(std::lround(local.x)),
                     area.top + static_cast<LONG>(std::lround(local.y))};
    };
    const auto& draft = pen_tool_->draft();
    const COLORREF ink = theme_ == ThemeMode::CyberDark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    const COLORREF preview_color = theme_ == ThemeMode::CyberDark ? RGB(0, 196, 255) : RGB(0, 112, 190);
    const int saved = SaveDC(dc);
    SetBkMode(dc, TRANSPARENT);

    HPEN path_pen = CreatePen(PS_SOLID, 2, ink);
    HGDIOBJ previous_pen = SelectObject(dc, path_pen);
    HGDIOBJ previous_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    for (size_t i = 0; i < draft.segments.size() && i + 1 < draft.nodes.size(); ++i) {
        const auto& start_node = draft.nodes[i];
        const auto& end_node = draft.nodes[i + 1];
        const POINT start = screen(start_node.anchor);
        const POINT end = screen(end_node.anchor);
        if (draft.segments[i].kind == document::PathSegmentKind::CubicBezier) {
            const auto out = start_node.has_outgoing_handle
                ? start_node.outgoing_handle : start_node.anchor;
            const auto in = end_node.has_incoming_handle
                ? end_node.incoming_handle : end_node.anchor;
            POINT curve[4]{start, screen(out), screen(in), end};
            PolyBezier(dc, curve, 4);
        } else {
            MoveToEx(dc, start.x, start.y, nullptr);
            LineTo(dc, end.x, end.y);
        }
    }

    std::optional<geometry::Point2D> preview_anchor;
    std::optional<geometry::Point2D> preview_incoming;
    std::optional<geometry::Point2D> preview_outgoing;
    bool preview_world_coordinates = false;
    if (const auto& pointer = pen_tool_->pointer_preview()) {
        preview_anchor = pointer->anchor;
        preview_incoming = pointer->incoming_handle;
        preview_outgoing = pointer->outgoing_handle;
        preview_world_coordinates = true;
    } else if (pen_tool_->candidate()) {
        preview_anchor = pen_tool_->candidate()->anchor;
        preview_incoming = pen_tool_->candidate()->incoming_handle;
        preview_outgoing = pen_tool_->candidate()->outgoing_handle;
    }

    if (!draft.nodes.empty() && preview_anchor) {
        const auto& start_node = draft.nodes.back();
        const POINT start = screen(start_node.anchor);
        const POINT end = preview_world_coordinates ? screen_world(*preview_anchor)
                                                   : screen(*preview_anchor);
        const auto start_control = start_node.has_outgoing_handle
            ? start_node.outgoing_handle : start_node.anchor;
        const auto preview_control = preview_incoming.value_or(*preview_anchor);
        const bool curved = start_node.has_outgoing_handle || preview_incoming.has_value();
        HPEN candidate_pen = CreatePen(PS_DASH, 1, preview_color);
        SelectObject(dc, candidate_pen);
        if (curved) {
            POINT curve[4]{start, screen(start_control),
                preview_world_coordinates ? screen_world(preview_control) : screen(preview_control), end};
            PolyBezier(dc, curve, 4);
        } else {
            MoveToEx(dc, start.x, start.y, nullptr);
            LineTo(dc, end.x, end.y);
        }
        SelectObject(dc, path_pen);
        DeleteObject(candidate_pen);

        const auto base_pt = pen_tool_->world_point_from_source(start_node.anchor);
        const auto cand_pt = preview_world_coordinates ? *preview_anchor : pen_tool_->world_point_from_source(*preview_anchor);
        const double delta_x = cand_pt.x - base_pt.x;
        const double delta_y = cand_pt.y - base_pt.y;
        const double cand_dist = std::hypot(delta_x, delta_y);
        if (!curved && cand_dist > 0.1) {
            constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
            constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
            double raw_deg = std::atan2(delta_y, delta_x) * kRadToDeg;
            if (raw_deg < 0.0) raw_deg += 360.0;
            double snap_deg = std::round(raw_deg / 15.0) * 15.0;
            if (snap_deg >= 360.0) snap_deg -= 360.0;
            double diff = std::abs(raw_deg - snap_deg);
            if (diff > 180.0) diff = 360.0 - diff;
            const bool is_snapped = (diff <= 0.5) || (active_num_field_ == NumericFieldId::PenAngle) || !pen_numeric_angle_.empty();

            if (is_snapped) {
                const double ray_deg = (active_num_field_ == NumericFieldId::PenAngle || !pen_numeric_angle_.empty()) ? raw_deg : snap_deg;
                const double rad = ray_deg * kDegToRad;
                const geometry::Point2D ray_dir{std::cos(rad), std::sin(rad)};
                const auto ray_s_world = base_pt - geometry::Point2D{ray_dir.x * 200.0, ray_dir.y * 200.0};
                const auto ray_e_world = base_pt + geometry::Point2D{ray_dir.x * 3000.0, ray_dir.y * 3000.0};
                const POINT rs = screen_world(ray_s_world);
                const POINT re = screen_world(ray_e_world);
                HPEN track_pen = CreatePen(PS_DOT, 1, theme_ == ThemeMode::CyberDark ? RGB(56, 189, 248) : RGB(14, 165, 233));
                HGDIOBJ old_p = SelectObject(dc, track_pen);
                MoveToEx(dc, rs.x, rs.y, nullptr);
                LineTo(dc, re.x, re.y);
                SelectObject(dc, old_p);
                DeleteObject(track_pen);
            }

            const int hud_w = 176;
            const int hud_h = 24;
            int bx = end.x + 16;
            int by = end.y - 28;
            if (bx + hud_w > area.right - 8) bx = end.x - hud_w - 16;
            if (bx < area.left + 8) bx = area.left + 8;
            if (by < area.top + 8) by = end.y + 16;
            if (by + hud_h > area.bottom - 8) by = area.bottom - hud_h - 8;

            RECT badge_rc{bx, by, bx + hud_w, by + hud_h};
            HBRUSH badge_bg = CreateSolidBrush(theme_ == ThemeMode::CyberDark ? RGB(15, 23, 42) : RGB(248, 250, 252));
            HPEN badge_pen = CreatePen(PS_SOLID, 1, theme_ == ThemeMode::CyberDark ? RGB(56, 189, 248) : RGB(14, 165, 233));
            HGDIOBJ old_bg = SelectObject(dc, badge_bg);
            HGDIOBJ old_pn = SelectObject(dc, badge_pen);
            RoundRect(dc, badge_rc.left, badge_rc.top, badge_rc.right, badge_rc.bottom, 4, 4);
            SelectObject(dc, old_bg);
            SelectObject(dc, old_pn);
            DeleteObject(badge_bg);
            DeleteObject(badge_pen);

            const bool focus_len = (active_num_field_ == NumericFieldId::PenLength);
            const bool focus_ang = (active_num_field_ == NumericFieldId::PenAngle);

            RECT rc_len{bx + 3, by + 2, bx + 85, by + hud_h - 2};
            RECT rc_ang{bx + 89, by + 2, bx + hud_w - 3, by + hud_h - 2};

            if (focus_len || focus_ang) {
                RECT focused_rc = focus_len ? rc_len : rc_ang;
                HPEN focus_pen = CreatePen(PS_SOLID, 1, RGB(0, 242, 195));
                HGDIOBJ p = SelectObject(dc, focus_pen);
                HGDIOBJ b = SelectObject(dc, GetStockObject(NULL_BRUSH));
                RoundRect(dc, focused_rc.left, focused_rc.top, focused_rc.right, focused_rc.bottom, 2, 2);
                SelectObject(dc, b);
                SelectObject(dc, p);
                DeleteObject(focus_pen);
            }

            std::wostringstream wlen, wang;
            if (focus_len) {
                wlen << L"L: " << utf8_to_wide(num_input_buffer_) << L"|";
            } else if (!pen_numeric_length_.empty()) {
                wlen << L"L: " << utf8_to_wide(pen_numeric_length_);
            } else {
                wlen << L"L: " << std::fixed << std::setprecision(1) << cand_dist;
            }

            if (focus_ang) {
                wang << L"∠ " << utf8_to_wide(num_input_buffer_) << L"°|";
            } else if (!pen_numeric_angle_.empty()) {
                wang << L"∠ " << utf8_to_wide(pen_numeric_angle_) << L"°";
            } else {
                wang << L"∠ " << std::fixed << std::setprecision(0) << (is_snapped ? snap_deg : raw_deg) << L"°";
            }

            HFONT hud_font = font_secondary_ ? font_secondary_ : font_be_vietnam_;
            HGDIOBJ old_f = SelectObject(dc, hud_font);
            SetBkMode(dc, TRANSPARENT);

            SetTextColor(dc, focus_len ? RGB(0, 242, 195) : (theme_ == ThemeMode::CyberDark ? RGB(226, 232, 240) : RGB(15, 23, 42)));
            const auto slen = wlen.str();
            DrawTextW(dc, slen.c_str(), -1, &rc_len, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            SetTextColor(dc, focus_ang ? RGB(0, 242, 195) : (theme_ == ThemeMode::CyberDark ? RGB(226, 232, 240) : RGB(15, 23, 42)));
            const auto sang = wang.str();
            DrawTextW(dc, sang.c_str(), -1, &rc_ang, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            SelectObject(dc, old_f);
        }
    }

    HBRUSH anchor_brush = CreateSolidBrush(ink);
    SelectObject(dc, anchor_brush);
    for (const auto& node : draft.nodes) {
        const POINT point = screen(node.anchor);
        Rectangle(dc, point.x - 3, point.y - 3, point.x + 4, point.y + 4);
    }
    if (preview_anchor) {
        const POINT point = preview_world_coordinates ? screen_world(*preview_anchor)
                                                     : screen(*preview_anchor);
        HBRUSH preview_brush = CreateSolidBrush(preview_color);
        SelectObject(dc, preview_brush);
        Ellipse(dc, point.x - 5, point.y - 5, point.x + 6, point.y + 6);
        if (preview_incoming || preview_outgoing) {
            const auto draw_handle = [&](std::optional<geometry::Point2D> handle) {
                if (!handle) return;
                const POINT hp = preview_world_coordinates ? screen_world(*handle) : screen(*handle);
                MoveToEx(dc, point.x, point.y, nullptr);
                LineTo(dc, hp.x, hp.y);
                Ellipse(dc, hp.x - 3, hp.y - 3, hp.x + 4, hp.y + 4);
            };
            HPEN handle_pen = CreatePen(PS_SOLID, 1, RGB(245, 158, 11));
            SelectObject(dc, handle_pen);
            draw_handle(preview_incoming);
            draw_handle(preview_outgoing);
            SelectObject(dc, path_pen);
            DeleteObject(handle_pen);
        }
        SelectObject(dc, anchor_brush);
        DeleteObject(preview_brush);
    }

    SelectObject(dc, previous_brush);
    SelectObject(dc, previous_pen);
    DeleteObject(anchor_brush);
    DeleteObject(path_pen);

    std::wostringstream hud;
    hud << L"PEN  ·  " << draft.nodes.size() << L" nodes";
    if (pen_tool_->resuming()) hud << L"  ·  RESUME";
    if (drawing_tool_ && drawing_tool_->active()) {
        const auto snap_state = drawing_tool_->visual_state();
        hud << L"  ·  Snap " << (snap_state.snap_config.master_enabled ? L"ON" : L"OFF");
        if (snap_state.snap_index_failed) hud << L" (unavailable; raw input)";
        else if (!snap_state.snap_index_ready) hud << L" (index building)";
        else if (snap_state.snap_kind) hud << L" (target acquired)";
    }
    if (!pen_numeric_buffer_.empty())
        hud << L"  ·  " << NativeWindow::utf8_to_wide(pen_numeric_buffer_);
    RECT hud_rect{area.left + 12, area.top + 10, area.right - 12, area.top + 34};
    SetTextColor(dc, ink);
    const auto hud_text = hud.str();
    DrawTextW(dc, hud_text.c_str(), -1, &hud_rect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    const wchar_t* hint = language_ == UILanguage::Vietnamese
        ? L"Nhấp đặt điểm · kéo tạo cong · Ctrl/Shift khóa theo hồ sơ · Alt tách · Enter kết thúc · Shift+Esc hủy"
        : L"Click point · drag for curve · Ctrl/Shift constrain by profile · Alt separates · Enter finish · Shift+Esc cancel";
    RECT hint_rect{area.left + 12, area.top + 34, area.right - 12, area.top + 54};
    SetTextColor(dc, RGB(150, 170, 190));
    DrawTextW(dc, hint, -1, &hint_rect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    RestoreDC(dc, saved);
}

void NativeWindow::paint_node_canvas_overlay(HDC dc, int width, int height) {
    if (!node_tool_ || !node_tool_->active()) return;
    const auto target_id = node_tool_->target_object();
    if (target_id == document::kInvalidObjectId) return;
    const auto snapshot = root().document().current_snapshot();
    if (!snapshot) return;
    const auto obj_opt = snapshot->get_object(target_id);
    if (!obj_opt || !std::holds_alternative<document::PathObject>(*obj_opt)) return;
    const auto& po = std::get<document::PathObject>(*obj_opt);
    if (!po.authoring_source) return;

    const RECT area = canvas_rect(width, height);
    const auto& camera = root().renderer().camera();
    const auto screen = [&](geometry::Point2D pt) {
        const auto local = camera.world_to_screen(pt);
        return POINT{area.left + static_cast<LONG>(std::lround(local.x)),
                     area.top + static_cast<LONG>(std::lround(local.y))};
    };

    const int saved = SaveDC(dc);
    SetBkMode(dc, TRANSPARENT);

    // 1. Draw handles for selected nodes
    const COLORREF handle_color = RGB(0, 210, 210);
    HPEN handle_pen = CreatePen(PS_DOT, 1, handle_color);
    HBRUSH handle_brush = CreateSolidBrush(handle_color);
    SelectObject(dc, handle_pen);
    SelectObject(dc, handle_brush);

    for (const auto& sub : po.authoring_source->subpaths) {
        for (const auto& node : sub.nodes) {
            if (node_tool_->is_node_selected(sub.id, node.id)) {
                const POINT a = screen(node.anchor);
                if (node.has_incoming_handle) {
                    const POINT h = screen(node.incoming_handle);
                    MoveToEx(dc, a.x, a.y, nullptr);
                    LineTo(dc, h.x, h.y);
                    Ellipse(dc, h.x - 4, h.y - 4, h.x + 5, h.y + 5);
                }
                if (node.has_outgoing_handle) {
                    const POINT h = screen(node.outgoing_handle);
                    MoveToEx(dc, a.x, a.y, nullptr);
                    LineTo(dc, h.x, h.y);
                    Ellipse(dc, h.x - 4, h.y - 4, h.x + 5, h.y + 5);
                }
            }
        }
    }
    DeleteObject(handle_pen);
    DeleteObject(handle_brush);

    // 2. Draw nodes
    HPEN node_pen = CreatePen(PS_SOLID, 1, RGB(15, 23, 42));
    HBRUSH sel_brush = CreateSolidBrush(RGB(0, 242, 195));
    HBRUSH unsel_brush = CreateSolidBrush(RGB(255, 255, 255));

    for (const auto& sub : po.authoring_source->subpaths) {
        for (const auto& node : sub.nodes) {
            const POINT pt = screen(node.anchor);
            const bool is_sel = node_tool_->is_node_selected(sub.id, node.id);
            SelectObject(dc, node_pen);
            SelectObject(dc, is_sel ? sel_brush : unsel_brush);
            Rectangle(dc, pt.x - 4, pt.y - 4, pt.x + 5, pt.y + 5);
        }
    }
    DeleteObject(node_pen);
    DeleteObject(sel_brush);
    DeleteObject(unsel_brush);

    // 3. Draw marquee rectangle if active
    if (node_tool_->is_marquee()) {
        const auto rect = node_tool_->marquee_rect();
        const POINT p1 = screen({rect.x, rect.y + rect.height});
        const POINT p2 = screen({rect.x + rect.width, rect.y});
        HPEN marquee_pen = CreatePen(PS_DOT, 1, RGB(0, 196, 255));
        SelectObject(dc, marquee_pen);
        SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, std::min(p1.x, p2.x), std::min(p1.y, p2.y),
                      std::max(p1.x, p2.x), std::max(p1.y, p2.y));
        DeleteObject(marquee_pen);
    }

    RestoreDC(dc, saved);
}

void NativeWindow::paint_contour_canvas_overlay(HDC dc, const RECT& area) {
    if (!contour_tool_ || !contour_tool_->active() || !contour_tool_->has_preview())
        return;

    const auto& res = contour_tool_->preview_result();
    if (res.steps.empty()) return;

    const auto& camera = root().renderer().camera();
    const auto screen = [&](geometry::Point2D pt) {
        const auto local = camera.world_to_screen(pt);
        return POINT{area.left + static_cast<LONG>(std::lround(local.x)),
                     area.top + static_cast<LONG>(std::lround(local.y))};
    };

    const int saved = SaveDC(dc);
    SetBkMode(dc, TRANSPARENT);

    // Cyan dashed pen for contour preview loops
    HPEN preview_pen = CreatePen(PS_DASH, 1, RGB(0, 242, 195));
    HGDIOBJ old_pen = SelectObject(dc, preview_pen);

    for (const auto& step : res.steps) {
        for (const auto& loop : step.loops) {
            POINT start_pt{0, 0};
            for (const auto& el : loop.elements()) {
                if (el.verb == geometry::PathVerb::MoveTo) {
                    start_pt = screen(el.pt);
                    MoveToEx(dc, start_pt.x, start_pt.y, nullptr);
                } else if (el.verb == geometry::PathVerb::LineTo) {
                    const POINT p = screen(el.pt);
                    LineTo(dc, p.x, p.y);
                } else if (el.verb == geometry::PathVerb::Close) {
                    LineTo(dc, start_pt.x, start_pt.y);
                }
            }
        }
    }

    SelectObject(dc, old_pen);
    DeleteObject(preview_pen);
    RestoreDC(dc, saved);
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

#endif

} // namespace pikdraw::desktop
