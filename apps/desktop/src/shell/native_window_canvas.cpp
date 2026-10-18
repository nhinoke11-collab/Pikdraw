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
    camera.set_viewport_size(canvas_width, canvas_height);
    const auto snapshot = workflow().snapshot_for_canvas();
    if (!snapshot)
        return;

    const bool view_is_stable = canvas_cache_valid_
        && cached_canvas_dc_ && cached_canvas_bmp_
        && canvas_width == cached_canvas_w_
        && canvas_height == cached_canvas_h_
        && color_preview_ == cached_canvas_profile_
        && camera.zoom() == cached_zoom_
        && camera.center().x == cached_camera_x_
        && camera.center().y == cached_camera_y_
        && theme_ == cached_theme_;
    if (view_is_stable && cached_canvas_snapshot_
        && snapshot != cached_canvas_snapshot_) {
        damage_tracker_.update(*cached_canvas_snapshot_, *snapshot, camera.zoom());
    } else if (!view_is_stable) {
        damage_tracker_.reset();
    }

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
            cached_canvas_bmp_ = CreateCompatibleBitmap(dc, dip(canvas_width), dip(canvas_height));
            cached_canvas_old_bmp_ = SelectObject(cached_canvas_dc_, cached_canvas_bmp_);
        }

        configure_ui_dc(cached_canvas_dc_);

        // Ensure backend rasterizer clears with active theme's pasteboard color
        root().renderer().backend().set_canvas_background(canvas_background_color());

        RECT local_area{0, 0, canvas_width, canvas_height};
        HBRUSH background = CreateSolidBrush(canvas_background_colorref());
        FillRect(cached_canvas_dc_, &local_area, background);
        DeleteObject(background);

        // Render the captured snapshot directly so incomplete GPU frames cannot
        // be mistaken for the previous reusable buffer contents.
        const int raster_w = dip(canvas_width), raster_h = dip(canvas_height);
        const size_t expected = static_cast<size_t>(raster_w) * raster_h * 4;
        bool frame_rendered = false;
        if (static_cast<uint64_t>(raster_w) * static_cast<uint64_t>(raster_h)
            <= 33554432ULL) {
            if (reusable_bgra_buffer_.size() != expected)
                reusable_bgra_buffer_.resize(expected, 0);
            rendering::PixelBufferTarget render_target{
                nullptr, raster_w, raster_h, raster_w * 4};
            render_target.bgra_pixels = reinterpret_cast<uint32_t*>(reusable_bgra_buffer_.data());
            const auto logical_camera = camera;
            camera.set_zoom(camera.zoom() * dpi_scale_);
            frame_rendered = root().renderer().render_frame(*snapshot, render_target);
            camera = logical_camera;
        }
        if (frame_rendered) {
            BITMAPINFO bitmap{};
            bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bitmap.bmiHeader.biWidth = raster_w;
            bitmap.bmiHeader.biHeight = -raster_h;
            bitmap.bmiHeader.biPlanes = 1;
            bitmap.bmiHeader.biBitCount = 32;
            bitmap.bmiHeader.biCompression = BI_RGB;
            StretchDIBits(cached_canvas_dc_, 0, 0, canvas_width, canvas_height,
                          0, 0, raster_w, raster_h,
                          reusable_bgra_buffer_.data(), &bitmap,
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
        canvas_cache_valid_ = frame_rendered;
        if (frame_rendered)
            damage_tracker_.clear_damage();
        else
            damage_tracker_.mark_full_damage();
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

    paint_inline_text_overlay(dc);

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
    paint_drawing_tool_overlays(dc);
    if (active_tool_ == 13 && gradient_tool_ && gradient_tool_->active())
        paint_gradient_canvas_overlay(dc, canvas_rect(width_, height_));
}

#endif

} // namespace pikdraw::desktop
