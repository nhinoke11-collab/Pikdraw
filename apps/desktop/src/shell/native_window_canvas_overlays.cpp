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

void NativeWindow::paint_gradient_canvas_overlay(HDC dc, const RECT& area) {
    if (!gradient_tool_ || !gradient_tool_->active())
        return;

    const auto* paint = gradient_tool_->active_draft_paint();
    if (!paint) return;

    const auto& camera = root().renderer().camera();
    const auto screen = [&](geometry::Point2D pt) {
        const auto local = camera.world_to_screen(pt);
        return POINT{area.left + static_cast<LONG>(std::lround(local.x)),
                     area.top + static_cast<LONG>(std::lround(local.y))};
    };

    const int saved = SaveDC(dc);
    SetBkMode(dc, TRANSPARENT);

    const auto handles = gradient_tool_->tool().get_handles();
    if (handles.empty()) {
        RestoreDC(dc, saved);
        return;
    }

    // Connect axis
    if (paint->kind == document::GradientKind::Linear) {
        HPEN axis_pen = CreatePen(PS_SOLID, 2, RGB(0, 242, 195));
        HGDIOBJ old_pen = SelectObject(dc, axis_pen);
        const auto p1 = screen(handles[0].position_object);
        const auto p2 = screen(handles[1].position_object);
        MoveToEx(dc, p1.x, p1.y, nullptr);
        LineTo(dc, p2.x, p2.y);
        SelectObject(dc, old_pen);
        DeleteObject(axis_pen);
    } else {
        HPEN axis_pen = CreatePen(PS_DASH, 1, RGB(0, 196, 255));
        HGDIOBJ old_pen = SelectObject(dc, axis_pen);
        const auto center = screen(handles[0].position_object);
        if (handles.size() > 1) {
            const auto r_pt = screen(handles[1].position_object);
            MoveToEx(dc, center.x, center.y, nullptr);
            LineTo(dc, r_pt.x, r_pt.y);
            const int rx = std::abs(r_pt.x - center.x);
            const int ry = handles.size() > 2 ? std::abs(screen(handles[2].position_object).y - center.y) : rx;
            Arc(dc, center.x - rx, center.y - ry, center.x + rx, center.y + ry, 0, 0, 0, 0);
        }
        SelectObject(dc, old_pen);
        DeleteObject(axis_pen);
    }

    // Draw handles
    HBRUSH handle_brush = CreateSolidBrush(RGB(255, 255, 255));
    HPEN handle_pen = CreatePen(PS_SOLID, 2, RGB(0, 242, 195));
    SelectObject(dc, handle_brush);
    SelectObject(dc, handle_pen);

    for (const auto& h : handles) {
        const auto pt = screen(h.position_object);
        if (h.kind == vector::tools::GradientHandleKind::LinearStart ||
            h.kind == vector::tools::GradientHandleKind::LinearEnd ||
            h.kind == vector::tools::GradientHandleKind::RadialCenter ||
            h.kind == vector::tools::GradientHandleKind::RadialRadius) {
            Rectangle(dc, pt.x - 4, pt.y - 4, pt.x + 5, pt.y + 5);
        } else {
            Ellipse(dc, pt.x - 3, pt.y - 3, pt.x + 4, pt.y + 4);
        }
    }

    DeleteObject(handle_pen);
    DeleteObject(handle_brush);
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
