#include "pikdraw/desktop/shell/native_window.hpp"
#include "../tools/drawing_tool_controller.hpp"
#include "../tools/pen_tool_controller.hpp"
#include "../tools/node_tool_controller.hpp"
#include "../tools/contour_tool_controller.hpp"
#include "../tools/gradient_tool_controller.hpp"

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
    if (active_tool_ == 13 && gradient_tool_ && gradient_tool_->active())
        paint_gradient_canvas_overlay(dc, canvas_rect(width_, height_));
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

#endif

} // namespace pikdraw::desktop
