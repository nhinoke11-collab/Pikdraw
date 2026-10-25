#include "pikdraw/desktop/shell/native_window.hpp"
#include "../tools/drawing_tool_controller.hpp"
#include "../tools/pen_tool_controller.hpp"
#include "../tools/node_tool_controller.hpp"
#include "../tools/contour_tool_controller.hpp"
#include "../tools/gradient_tool_controller.hpp"
#include "../tools/dimension_tool_controller.hpp"
#include "../tools/annotation/annotation_tool_controller.hpp"
#include "native_gdiplus_api.hpp"
#include "native_window_length_input.hpp"

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

struct AntialiasedOverlayRenderer {
    GdiplusApi* api{nullptr};
    GpGraphics* g{nullptr};
    HDC dc{nullptr};

    explicit AntialiasedOverlayRenderer(HDC hdc) : dc(hdc) {
        api = GdiplusApi::get();
        if (api) {
            if (api->CreateFromHDC(dc, &g) != 0 || !g) {
                g = nullptr;
            } else {
                api->SetSmoothingMode(g, kSmoothingModeAntiAlias);
                api->SetPixelOffsetMode(g, kPixelOffsetModeHalf);
            }
        }
    }

    ~AntialiasedOverlayRenderer() {
        if (api && g) {
            api->DeleteGraphics(g);
        }
    }

    void draw_bezier(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3,
                     COLORREF color, float width = 1.2f, BYTE alpha = 255) {
        if (api && g && api->DrawBezier) {
            GpPen* pen = nullptr;
            if (api->CreatePen1(MakeGdiplusARGB(color, alpha), width, kUnitPixel, &pen) == 0 && pen) {
                api->DrawBezier(g, pen, x0, y0, x1, y1, x2, y2, x3, y3);
                api->DeletePen(pen);
            }
        } else {
            HPEN hpen = CreatePen(PS_SOLID, static_cast<int>(std::max(1.0f, std::round(width))), color);
            HGDIOBJ old_pen = SelectObject(dc, hpen);
            POINT pts[4]{
                POINT{static_cast<LONG>(std::lround(x0)), static_cast<LONG>(std::lround(y0))},
                POINT{static_cast<LONG>(std::lround(x1)), static_cast<LONG>(std::lround(y1))},
                POINT{static_cast<LONG>(std::lround(x2)), static_cast<LONG>(std::lround(y2))},
                POINT{static_cast<LONG>(std::lround(x3)), static_cast<LONG>(std::lround(y3))}
            };
            PolyBezier(dc, pts, 4);
            SelectObject(dc, old_pen);
            DeleteObject(hpen);
        }
    }

    void draw_line(float x0, float y0, float x1, float y1,
                   COLORREF color, float width = 1.0f, BYTE alpha = 255, int dash_style = kDashStyleSolid) {
        if (api && g) {
            GpPen* pen = nullptr;
            if (api->CreatePen1(MakeGdiplusARGB(color, alpha), width, kUnitPixel, &pen) == 0 && pen) {
                if (dash_style != kDashStyleSolid && api->SetPenDashStyle) {
                    api->SetPenDashStyle(pen, dash_style);
                }
                api->DrawLine(g, pen, x0, y0, x1, y1);
                api->DeletePen(pen);
            }
        } else {
            int gdi_style = (dash_style == kDashStyleDash) ? PS_DASH :
                            (dash_style == kDashStyleDot) ? PS_DOT : PS_SOLID;
            HPEN hpen = CreatePen(gdi_style, static_cast<int>(std::max(1.0f, std::round(width))), color);
            HGDIOBJ old_pen = SelectObject(dc, hpen);
            MoveToEx(dc, static_cast<int>(std::lround(x0)), static_cast<int>(std::lround(y0)), nullptr);
            LineTo(dc, static_cast<int>(std::lround(x1)), static_cast<int>(std::lround(y1)));
            SelectObject(dc, old_pen);
            DeleteObject(hpen);
        }
    }

    void draw_circle(float cx, float cy, float radius,
                     COLORREF stroke_color, COLORREF fill_color,
                     float stroke_width = 1.0f, BYTE stroke_alpha = 255, BYTE fill_alpha = 255) {
        if (api && g) {
            if (fill_alpha > 0) {
                GpSolidFill* brush = nullptr;
                if (api->CreateSolidFill(MakeGdiplusARGB(fill_color, fill_alpha), &brush) == 0 && brush) {
                    api->FillEllipse(g, brush, cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
                    api->DeleteBrush(brush);
                }
            }
            if (stroke_alpha > 0 && stroke_width > 0.0f) {
                GpPen* pen = nullptr;
                if (api->CreatePen1(MakeGdiplusARGB(stroke_color, stroke_alpha), stroke_width, kUnitPixel, &pen) == 0 && pen) {
                    api->DrawEllipse(g, pen, cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
                    api->DeletePen(pen);
                }
            }
        } else {
            HBRUSH hbrush = (fill_alpha > 0) ? CreateSolidBrush(fill_color) : reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
            HPEN hpen = CreatePen(PS_SOLID, static_cast<int>(std::max(1.0f, std::round(stroke_width))), stroke_color);
            HGDIOBJ old_b = SelectObject(dc, hbrush);
            HGDIOBJ old_p = SelectObject(dc, hpen);
            int r = static_cast<int>(std::lround(radius));
            int icx = static_cast<int>(std::lround(cx));
            int icy = static_cast<int>(std::lround(cy));
            Ellipse(dc, icx - r, icy - r, icx + r + 1, icy + r + 1);
            SelectObject(dc, old_p);
            SelectObject(dc, old_b);
            DeleteObject(hpen);
            if (fill_alpha > 0) DeleteObject(hbrush);
        }
    }
};

} // namespace

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

    AntialiasedOverlayRenderer aa(dc);

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
            const POINT c1 = screen(out);
            const POINT c2 = screen(in);
            aa.draw_bezier(static_cast<float>(start.x), static_cast<float>(start.y),
                           static_cast<float>(c1.x), static_cast<float>(c1.y),
                           static_cast<float>(c2.x), static_cast<float>(c2.y),
                           static_cast<float>(end.x), static_cast<float>(end.y),
                           ink, 1.2f);
        } else {
            aa.draw_line(static_cast<float>(start.x), static_cast<float>(start.y),
                         static_cast<float>(end.x), static_cast<float>(end.y),
                         ink, 1.2f);
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
        if (curved) {
            const POINT c1 = screen(start_control);
            const POINT c2 = preview_world_coordinates ? screen_world(preview_control) : screen(preview_control);
            aa.draw_bezier(static_cast<float>(start.x), static_cast<float>(start.y),
                           static_cast<float>(c1.x), static_cast<float>(c1.y),
                           static_cast<float>(c2.x), static_cast<float>(c2.y),
                           static_cast<float>(end.x), static_cast<float>(end.y),
                           preview_color, 1.0f, 220);
        } else {
            aa.draw_line(static_cast<float>(start.x), static_cast<float>(start.y),
                         static_cast<float>(end.x), static_cast<float>(end.y),
                         preview_color, 1.0f, 220, kDashStyleDash);
        }

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
                aa.draw_line(static_cast<float>(rs.x), static_cast<float>(rs.y),
                             static_cast<float>(re.x), static_cast<float>(re.y),
                             theme_ == ThemeMode::CyberDark ? RGB(56, 189, 248) : RGB(14, 165, 233),
                             1.0f, 200, kDashStyleDot);
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
    HGDIOBJ prev_brush = SelectObject(dc, anchor_brush);
    HPEN anchor_pen = CreatePen(PS_SOLID, 1, theme_ == ThemeMode::CyberDark ? RGB(15, 23, 42) : RGB(255, 255, 255));
    HGDIOBJ prev_pen = SelectObject(dc, anchor_pen);
    for (const auto& node : draft.nodes) {
        const POINT point = screen(node.anchor);
        Rectangle(dc, point.x - 3, point.y - 3, point.x + 4, point.y + 4);
    }
    if (pen_close_candidate_ && !draft.nodes.empty()) {
        const POINT first_pt = screen(draft.nodes.front().anchor);
        aa.draw_circle(static_cast<float>(first_pt.x), static_cast<float>(first_pt.y), 7.5f,
                       RGB(0, 242, 195), RGB(0, 0, 0), 1.5f, 255, 0);
    }
    if (preview_anchor) {
        const POINT point = preview_world_coordinates ? screen_world(*preview_anchor)
                                                     : screen(*preview_anchor);
        aa.draw_circle(static_cast<float>(point.x), static_cast<float>(point.y), 4.5f,
                       preview_color, preview_color, 1.0f);
        if (preview_incoming || preview_outgoing) {
            const auto draw_handle = [&](std::optional<geometry::Point2D> handle) {
                if (!handle) return;
                const POINT hp = preview_world_coordinates ? screen_world(*handle) : screen(*handle);
                aa.draw_line(static_cast<float>(point.x), static_cast<float>(point.y),
                             static_cast<float>(hp.x), static_cast<float>(hp.y),
                             RGB(245, 158, 11), 1.0f, 220, kDashStyleDot);
                aa.draw_circle(static_cast<float>(hp.x), static_cast<float>(hp.y), 3.0f,
                               RGB(15, 23, 42), RGB(245, 158, 11), 1.0f);
            };
            draw_handle(preview_incoming);
            draw_handle(preview_outgoing);
        }
    }

    SelectObject(dc, prev_pen);
    SelectObject(dc, prev_brush);
    DeleteObject(anchor_pen);
    DeleteObject(anchor_brush);

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

    const auto tr = po.transform;
    const auto drag_delta = node_tool_->tool().drag_delta();
    const bool is_dragging_node = (node_tool_->tool().drag_mode() == vector::tools::node::NodeDragMode::MoveNode);
    const bool is_dragging_handle = (node_tool_->tool().drag_mode() == vector::tools::node::NodeDragMode::MoveHandle);
    const bool is_dragging_bend = (node_tool_->tool().drag_mode() == vector::tools::node::NodeDragMode::BendSegment &&
                                   node_tool_->tool().active_segment_drag().has_value());

    AntialiasedOverlayRenderer aa(dc);

    const auto outline_col = theme_ == ThemeMode::CyberDark ? RGB(0, 242, 195) : RGB(14, 165, 233);
    const auto sel_seg_col = RGB(245, 158, 11); // Amber-500 for selected segment
    const auto hov_seg_col = RGB(56, 189, 248); // Sky-400 for hovered segment

    const auto draw_segment = [&](const document::EditableSubpath& sub, size_t i, COLORREF col, float width, BYTE alpha = 255) {
        const size_t n = sub.nodes.size();
        if (n == 0 || i >= sub.segments.size()) return;
        const size_t next_i = (i + 1) % n;
        const auto p0_w = tr.map_point(sub.nodes[i].anchor);
        const auto p3_w = tr.map_point(sub.nodes[next_i].anchor);
        const auto& seg = sub.segments[i];

        if (seg.kind == document::PathSegmentKind::CubicBezier ||
            sub.nodes[i].has_outgoing_handle || sub.nodes[next_i].has_incoming_handle) {
            const auto [s_in, s_out] = document::editable_path_handles_for(sub, i);
            const auto [e_in, e_out] = document::editable_path_handles_for(sub, next_i);
            (void)s_in; (void)e_out;
            const POINT p0 = screen(p0_w);
            const POINT c1 = screen(tr.map_point(s_out));
            const POINT c2 = screen(tr.map_point(e_in));
            const POINT p3 = screen(p3_w);
            aa.draw_bezier(static_cast<float>(p0.x), static_cast<float>(p0.y),
                           static_cast<float>(c1.x), static_cast<float>(c1.y),
                           static_cast<float>(c2.x), static_cast<float>(c2.y),
                           static_cast<float>(p3.x), static_cast<float>(p3.y),
                           col, width, alpha);
        } else if (seg.kind == document::PathSegmentKind::QuadraticBezier) {
            const auto q_w = tr.map_point(seg.quadratic_control);
            const auto c1_w = p0_w + (q_w - p0_w) * (2.0 / 3.0);
            const auto c2_w = p3_w + (q_w - p3_w) * (2.0 / 3.0);
            const POINT p0 = screen(p0_w);
            const POINT c1 = screen(c1_w);
            const POINT c2 = screen(c2_w);
            const POINT p3 = screen(p3_w);
            aa.draw_bezier(static_cast<float>(p0.x), static_cast<float>(p0.y),
                           static_cast<float>(c1.x), static_cast<float>(c1.y),
                           static_cast<float>(c2.x), static_cast<float>(c2.y),
                           static_cast<float>(p3.x), static_cast<float>(p3.y),
                           col, width, alpha);
        } else {
            const POINT p0 = screen(p0_w);
            const POINT p3 = screen(p3_w);
            aa.draw_line(static_cast<float>(p0.x), static_cast<float>(p0.y),
                         static_cast<float>(p3.x), static_cast<float>(p3.y),
                         col, width, alpha);
        }
    };

    // 0. Draw overall path outline when in Node Tool (highlighting editable path boundary)
    if (active_tool_ == 11 && !is_dragging_node && !is_dragging_handle) {
        for (const auto& sub : po.authoring_source->subpaths) {
            for (size_t i = 0; i < sub.segments.size(); ++i) {
                draw_segment(sub, i, outline_col, 1.4f, 210);
            }
        }
    }

    // 0b. Draw hovered segment highlight
    const auto hover_seg = node_tool_->hover_segment();
    if (hover_seg && !is_dragging_bend && !is_dragging_node && !is_dragging_handle) {
        for (const auto& sub : po.authoring_source->subpaths) {
            if (sub.id != hover_seg->first) continue;
            for (size_t i = 0; i < sub.segments.size(); ++i) {
                if (sub.segments[i].id == hover_seg->second) {
                    draw_segment(sub, i, hov_seg_col, 2.5f, 255);
                    break;
                }
            }
        }
    }

    // 0c. Draw selected segment highlight
    const auto sel_seg = node_tool_->selected_segment();
    if (sel_seg && !is_dragging_bend) {
        for (const auto& sub : po.authoring_source->subpaths) {
            if (sub.id != sel_seg->first) continue;
            for (size_t i = 0; i < sub.segments.size(); ++i) {
                if (sub.segments[i].id == sel_seg->second) {
                    draw_segment(sub, i, sel_seg_col, 3.0f, 255);
                    break;
                }
            }
        }
    }

    // 1b. Live preview for Direct Segment Bending (BendSegment)
    if (is_dragging_bend) {
        const auto asd = *node_tool_->tool().active_segment_drag();
        const auto [c1_w, c2_w] = node_tool_->tool().current_segment_bend_handles();
        const COLORREF bend_col = sel_seg_col;
        const COLORREF handle_col = RGB(0, 210, 210);

        const POINT p0 = screen(asd.p0_world);
        const POINT c1 = screen(c1_w);
        const POINT c2 = screen(c2_w);
        const POINT p3 = screen(asd.p3_world);

        // Draw ghost baseline for the segment being deformed
        aa.draw_line(static_cast<float>(p0.x), static_cast<float>(p0.y),
                     static_cast<float>(p3.x), static_cast<float>(p3.y),
                     RGB(148, 163, 184), 1.0f, 120, kDashStyleDot);

        // Draw real-time bending curve preview
        aa.draw_bezier(static_cast<float>(p0.x), static_cast<float>(p0.y),
                       static_cast<float>(c1.x), static_cast<float>(c1.y),
                       static_cast<float>(c2.x), static_cast<float>(c2.y),
                       static_cast<float>(p3.x), static_cast<float>(p3.y),
                       bend_col, 2.2f);

        aa.draw_line(static_cast<float>(p0.x), static_cast<float>(p0.y),
                     static_cast<float>(c1.x), static_cast<float>(c1.y),
                     handle_col, 1.0f, 220, kDashStyleDot);
        aa.draw_circle(static_cast<float>(c1.x), static_cast<float>(c1.y), 3.0f,
                       RGB(15, 23, 42), handle_col, 1.0f);

        aa.draw_line(static_cast<float>(p3.x), static_cast<float>(p3.y),
                     static_cast<float>(c2.x), static_cast<float>(c2.y),
                     handle_col, 1.0f, 220, kDashStyleDot);
        aa.draw_circle(static_cast<float>(c2.x), static_cast<float>(c2.y), 3.0f,
                       RGB(15, 23, 42), handle_col, 1.0f);
    }

    // 1. Live preview for corner rounding (Live Corners)
    if (node_tool_->tool().is_rounding_corners()) {
        const auto fillets = node_tool_->tool().fillet_preview_segments();
        const COLORREF preview_stroke = theme_ == ThemeMode::CyberDark ? RGB(0, 242, 195) : RGB(14, 165, 233);
        for (const auto& fil : fillets) {
            const POINT p0 = screen(fil.p0);
            const POINT p3 = screen(fil.p3);
            if (fil.is_cubic) {
                const POINT c1 = screen(fil.c1);
                const POINT c2 = screen(fil.c2);
                aa.draw_bezier(static_cast<float>(p0.x), static_cast<float>(p0.y),
                               static_cast<float>(c1.x), static_cast<float>(c1.y),
                               static_cast<float>(c2.x), static_cast<float>(c2.y),
                               static_cast<float>(p3.x), static_cast<float>(p3.y),
                               preview_stroke, 1.2f);
            } else {
                // Chamfer bevel diagonal line
                aa.draw_line(static_cast<float>(p0.x), static_cast<float>(p0.y),
                             static_cast<float>(p3.x), static_cast<float>(p3.y),
                             preview_stroke, 1.2f);
            }
        }

        // Floating radius badge near active corner widget
        const auto active_cw = node_tool_->tool().active_corner_widget();
        if (active_cw) {
            const auto active_pos_w = active_cw->widget_position + active_cw->bisector * node_tool_->tool().current_corner_radius();
            const POINT wp = screen(active_pos_w);
            const std::wstring str = L"R: " + format_mm_display(
                node_tool_->tool().current_corner_radius(), display_length_unit_, 5);
            HFONT font = font_secondary_ ? font_secondary_ : font_be_vietnam_;
            HGDIOBJ ofn = SelectObject(dc, font);
            SIZE text_size{};
            GetTextExtentPoint32W(dc, str.c_str(), static_cast<int>(str.size()), &text_size);
            const int badge_width = std::max(81, static_cast<int>(text_size.cx) + 16);
            RECT badge_rc{wp.x + 14, wp.y - 12, wp.x + 14 + badge_width, wp.y + 12};
            HBRUSH badge_bg = CreateSolidBrush(theme_ == ThemeMode::CyberDark ? RGB(15, 23, 42) : RGB(248, 250, 252));
            HPEN badge_pen = CreatePen(PS_SOLID, 1, RGB(14, 165, 233));
            HGDIOBJ obg = SelectObject(dc, badge_bg);
            HGDIOBJ opn = SelectObject(dc, badge_pen);
            RoundRect(dc, badge_rc.left, badge_rc.top, badge_rc.right, badge_rc.bottom, 4, 4);
            SelectObject(dc, obg);
            SelectObject(dc, opn);
            DeleteObject(badge_bg);
            DeleteObject(badge_pen);

            SetTextColor(dc, theme_ == ThemeMode::CyberDark ? RGB(56, 189, 248) : RGB(14, 165, 233));
            DrawTextW(dc, str.c_str(), -1, &badge_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(dc, ofn);
        }
    }

    // 2. Visible handles query (includes real-time handle drag position and opposite handle mirroring in world space)
    const auto handles = node_tool_->tool().visible_handles();
    auto get_handle_pos_w = [&](document::SubpathId sid, document::NodeId nid, bool is_inc, geometry::Point2D default_local) {
        for (const auto& h : handles) {
            if (h.subpath_id == sid && h.node_id == nid && h.is_incoming == is_inc) {
                return h.position;
            }
        }
        return tr.map_point(default_local);
    };

    // 3. Draw live preview connecting curves for moving nodes or deforming Bézier handles
    if ((is_dragging_node && (drag_delta.x != 0.0 || drag_delta.y != 0.0)) || is_dragging_handle) {
        const COLORREF move_col = theme_ == ThemeMode::CyberDark ? RGB(0, 242, 195) : RGB(14, 165, 233);

        for (const auto& sub : po.authoring_source->subpaths) {
            const size_t n = sub.nodes.size();
            if (n == 0) continue;
            const auto get_anchor_w = [&](size_t idx) {
                const auto& nd = sub.nodes[idx];
                const auto base_w = tr.map_point(nd.anchor);
                return (is_dragging_node && node_tool_->is_node_selected(sub.id, nd.id)) ? (base_w + drag_delta) : base_w;
            };

            for (size_t i = 0; i < sub.segments.size() && i + 1 < n; ++i) {
                const auto& n1 = sub.nodes[i];
                const auto& n2 = sub.nodes[i + 1];
                const auto p0_w = get_anchor_w(i);
                const auto p3_w = get_anchor_w(i + 1);
                const bool is_bezier = (sub.segments[i].kind == document::PathSegmentKind::CubicBezier) ||
                                       n1.has_outgoing_handle || n2.has_incoming_handle;
                const POINT p0 = screen(p0_w);
                const POINT p3 = screen(p3_w);
                if (is_bezier) {
                    const auto c1_w = n1.has_outgoing_handle ? get_handle_pos_w(sub.id, n1.id, false, n1.outgoing_handle) : p0_w;
                    const auto c2_w = n2.has_incoming_handle ? get_handle_pos_w(sub.id, n2.id, true, n2.incoming_handle) : p3_w;
                    const POINT c1 = screen(c1_w);
                    const POINT c2 = screen(c2_w);
                    aa.draw_bezier(static_cast<float>(p0.x), static_cast<float>(p0.y),
                                   static_cast<float>(c1.x), static_cast<float>(c1.y),
                                   static_cast<float>(c2.x), static_cast<float>(c2.y),
                                   static_cast<float>(p3.x), static_cast<float>(p3.y),
                                   move_col, 1.2f);
                } else {
                    aa.draw_line(static_cast<float>(p0.x), static_cast<float>(p0.y),
                                 static_cast<float>(p3.x), static_cast<float>(p3.y),
                                 move_col, 1.0f);
                }
            }
            if (sub.closed && !sub.segments.empty() && sub.segments.size() >= n) {
                const auto& n1 = sub.nodes[n - 1];
                const auto& n2 = sub.nodes[0];
                const auto p0_w = get_anchor_w(n - 1);
                const auto p3_w = get_anchor_w(0);
                const bool is_bezier = (sub.segments[n - 1].kind == document::PathSegmentKind::CubicBezier) ||
                                       n1.has_outgoing_handle || n2.has_incoming_handle;
                const POINT p0 = screen(p0_w);
                const POINT p3 = screen(p3_w);
                if (is_bezier) {
                    const auto c1_w = n1.has_outgoing_handle ? get_handle_pos_w(sub.id, n1.id, false, n1.outgoing_handle) : p0_w;
                    const auto c2_w = n2.has_incoming_handle ? get_handle_pos_w(sub.id, n2.id, true, n2.incoming_handle) : p3_w;
                    const POINT c1 = screen(c1_w);
                    const POINT c2 = screen(c2_w);
                    aa.draw_bezier(static_cast<float>(p0.x), static_cast<float>(p0.y),
                                   static_cast<float>(c1.x), static_cast<float>(c1.y),
                                   static_cast<float>(c2.x), static_cast<float>(c2.y),
                                   static_cast<float>(p3.x), static_cast<float>(p3.y),
                                   move_col, 1.2f);
                } else {
                    aa.draw_line(static_cast<float>(p0.x), static_cast<float>(p0.y),
                                 static_cast<float>(p3.x), static_cast<float>(p3.y),
                                 move_col, 1.0f);
                }
            }
        }
    }

    // 4. Draw handles for selected nodes (using tool().visible_handles() which includes drag delta and mirroring)
    if (!handles.empty()) {
        const COLORREF handle_color = RGB(0, 210, 210);

        for (const auto& h : handles) {
            geometry::Point2D base_anchor_w{};
            for (const auto& sub : po.authoring_source->subpaths) {
                if (sub.id != h.subpath_id) continue;
                for (const auto& node : sub.nodes) {
                    if (node.id == h.node_id) {
                        const auto bw = tr.map_point(node.anchor);
                        base_anchor_w = is_dragging_node && node_tool_->is_node_selected(sub.id, node.id)
                            ? (bw + drag_delta) : bw;
                        break;
                    }
                }
            }
            const POINT a = screen(base_anchor_w);
            const POINT hp = screen(h.position);
            aa.draw_line(static_cast<float>(a.x), static_cast<float>(a.y),
                         static_cast<float>(hp.x), static_cast<float>(hp.y),
                         handle_color, 1.0f, 220, kDashStyleDot);
            aa.draw_circle(static_cast<float>(hp.x), static_cast<float>(hp.y), 3.0f,
                           RGB(15, 23, 42), handle_color, 1.0f);
        }
    }

    // 5. Draw Illustrator-style Live Corner Rounding widgets (transformed to world space)
    const auto corner_widgets = node_tool_->tool().corner_widgets(camera.zoom());
    for (const auto& cw : corner_widgets) {
        const bool is_active_corner = (node_tool_->tool().is_rounding_corners() &&
            node_tool_->tool().active_corner_widget() &&
            node_tool_->tool().active_corner_widget()->node_id == cw.node_id);
        const auto pos_w = is_active_corner
            ? (cw.widget_position + cw.bisector * node_tool_->tool().current_corner_radius())
            : cw.widget_position;
        const POINT cp = screen(pos_w);

        COLORREF border_color = RGB(14, 165, 233); // Sky-500 cyan
        COLORREF fill_color = is_active_corner ? RGB(14, 165, 233) : RGB(255, 255, 255);
        COLORREF dot_color = is_active_corner ? RGB(255, 255, 255) : RGB(14, 165, 233);

        aa.draw_circle(static_cast<float>(cp.x), static_cast<float>(cp.y), 4.5f,
                       border_color, fill_color, 1.0f);
        aa.draw_circle(static_cast<float>(cp.x), static_cast<float>(cp.y), 1.8f,
                       dot_color, dot_color, 1.0f);
    }

    // 6. Draw nodes (anchor points) in world space (only in Node tool)
    if (active_tool_ == 11) {
        HPEN node_pen = CreatePen(PS_SOLID, 1, RGB(15, 23, 42));
        HBRUSH sel_brush = CreateSolidBrush(RGB(0, 242, 195));
        HBRUSH unsel_brush = CreateSolidBrush(RGB(255, 255, 255));

        for (const auto& sub : po.authoring_source->subpaths) {
            for (const auto& node : sub.nodes) {
                const bool is_sel = node_tool_->is_node_selected(sub.id, node.id);
                const auto base_w = tr.map_point(node.anchor);
                const auto pt_world = (is_sel && is_dragging_node) ? (base_w + drag_delta) : base_w;
                const POINT pt = screen(pt_world);
                SelectObject(dc, node_pen);
                SelectObject(dc, is_sel ? sel_brush : unsel_brush);
                Rectangle(dc, pt.x - 4, pt.y - 4, pt.x + 5, pt.y + 5);
            }
        }
        DeleteObject(node_pen);
        DeleteObject(sel_brush);
        DeleteObject(unsel_brush);
    }

    // 7. Draw marquee rectangle if active
    if (node_tool_->is_marquee()) {
        const auto rect = node_tool_->marquee_rect();
        const POINT p1 = screen({rect.x, rect.y});
        const POINT p2 = screen({rect.x + rect.width, rect.y + rect.height});
        HPEN marquee_pen = CreatePen(PS_DOT, 1, RGB(0, 196, 255));
        HGDIOBJ old_pen = SelectObject(dc, marquee_pen);
        HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, std::min(p1.x, p2.x), std::min(p1.y, p2.y),
                      std::max(p1.x, p2.x), std::max(p1.y, p2.y));
        SelectObject(dc, old_brush);
        SelectObject(dc, old_pen);
        DeleteObject(marquee_pen);
    }

    RestoreDC(dc, saved);
}

void NativeWindow::paint_contour_canvas_overlay(HDC dc, const RECT& area) {
    if (!contour_tool_ || !contour_tool_->active() || !contour_tool_->has_preview() || contour_applied_)
        return;

    const auto& res = contour_tool_->preview_result();
    if (res.steps.empty()) return;

    const auto target_id = contour_tool_->target_object();
    const auto snapshot = root().document().current_snapshot();
    const auto* object = snapshot ? snapshot->objects().lookup_ref(target_id) : nullptr;
    const auto* path_obj = object ? std::get_if<document::PathObject>(object) : nullptr;
    const auto obj_transform = path_obj ? path_obj->transform : geometry::Transform2D::identity();

    const auto& camera = root().renderer().camera();
    const auto screen = [&](geometry::Point2D pt) {
        const auto world_pt = obj_transform.map_point(pt);
        const auto local = camera.world_to_screen(world_pt);
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

void NativeWindow::paint_dimension_canvas_overlay(HDC dc, const RECT& area) {
    if (!dimension_tool_ || !dimension_tool_->active())
        return;

    const auto& camera = root().renderer().camera();
    const auto screen = [&](geometry::Point2D pt) {
        const auto local = camera.world_to_screen(pt);
        return POINT{area.left + static_cast<LONG>(std::lround(local.x)),
                     area.top + static_cast<LONG>(std::lround(local.y))};
    };

    const int saved = SaveDC(dc);
    SetBkMode(dc, TRANSPARENT);

    const COLORREF col_cyan = RGB(0, 242, 195);
    const COLORREF col_text = theme_ == ThemeMode::CyberDark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    const COLORREF col_bg = theme_ == ThemeMode::CyberDark ? RGB(15, 23, 42) : RGB(255, 255, 255);

    // If picking second point, show start marker and dashed rubberband
    if (dimension_tool_->state() == tools::DimensionSessionState::PickSecond) {
        const auto p1 = screen(dimension_tool_->first_point());
        const auto p2 = screen(dimension_tool_->snap_active() ? dimension_tool_->snap_point() : pointer_current_world_);

        HPEN dash_pen = CreatePen(PS_DOT, 1, col_cyan);
        HGDIOBJ old_pen = SelectObject(dc, dash_pen);
        MoveToEx(dc, p1.x, p1.y, nullptr);
        LineTo(dc, p2.x, p2.y);
        SelectObject(dc, old_pen);
        DeleteObject(dash_pen);

        HBRUSH mark_brush = CreateSolidBrush(col_cyan);
        HGDIOBJ old_brush = SelectObject(dc, mark_brush);
        HPEN mark_pen = CreatePen(PS_SOLID, 1, col_cyan);
        old_pen = SelectObject(dc, mark_pen);
        Ellipse(dc, p1.x - 4, p1.y - 4, p1.x + 5, p1.y + 5);
        SelectObject(dc, old_brush);
        SelectObject(dc, old_pen);
        DeleteObject(mark_brush);
        DeleteObject(mark_pen);
    }

    // Smart Snap Indicator badge
    if (dimension_tool_->snap_active()) {
        const auto sp = screen(dimension_tool_->snap_point());
        HPEN snap_pen = CreatePen(PS_SOLID, 2, col_cyan);
        HGDIOBJ old_p = SelectObject(dc, snap_pen);
        HBRUSH null_br = (HBRUSH)GetStockObject(NULL_BRUSH);
        HGDIOBJ old_b = SelectObject(dc, null_br);

        const int sz = 6;
        const auto st = dimension_tool_->snap_type();
        if (st == tools::DimensionSnapType::Node || st == tools::DimensionSnapType::BoundingBoxCorner) {
            Rectangle(dc, sp.x - sz, sp.y - sz, sp.x + sz + 1, sp.y + sz + 1);
        } else if (st == tools::DimensionSnapType::Midpoint) {
            POINT pts[3] = { {sp.x, sp.y - sz}, {sp.x + sz, sp.y + sz}, {sp.x - sz, sp.y + sz} };
            Polygon(dc, pts, 3);
        } else if (st == tools::DimensionSnapType::Center) {
            Ellipse(dc, sp.x - sz, sp.y - sz, sp.x + sz + 1, sp.y + sz + 1);
            MoveToEx(dc, sp.x - sz - 2, sp.y, nullptr);
            LineTo(dc, sp.x + sz + 3, sp.y);
            MoveToEx(dc, sp.x, sp.y - sz - 2, nullptr);
            LineTo(dc, sp.x, sp.y + sz + 3);
        } else if (st == tools::DimensionSnapType::Perpendicular) {
            // Right-angle marker
            MoveToEx(dc, sp.x, sp.y - sz, nullptr);
            LineTo(dc, sp.x, sp.y);
            LineTo(dc, sp.x + sz, sp.y);
            MoveToEx(dc, sp.x + sz * 0.5, sp.y - sz * 0.5, nullptr);
            LineTo(dc, sp.x + sz * 0.5, sp.y);
        } else if (st == tools::DimensionSnapType::Quadrant) {
            POINT pts[4] = { {sp.x, sp.y - sz}, {sp.x + sz, sp.y}, {sp.x, sp.y + sz}, {sp.x - sz, sp.y} };
            Polygon(dc, pts, 4);
        } else if (st == tools::DimensionSnapType::Intersection) {
            MoveToEx(dc, sp.x - sz, sp.y - sz, nullptr);
            LineTo(dc, sp.x + sz, sp.y + sz);
            MoveToEx(dc, sp.x + sz, sp.y - sz, nullptr);
            LineTo(dc, sp.x - sz, sp.y + sz);
        }
        SelectObject(dc, old_b);
        SelectObject(dc, old_p);
        DeleteObject(snap_pen);

        const bool is_vi = language_ == UILanguage::Vietnamese;
        const wchar_t* snap_lbl = L"Snap";
        if (st == tools::DimensionSnapType::Node) snap_lbl = is_vi ? L"Đỉnh mút (Node)" : L"Node";
        else if (st == tools::DimensionSnapType::Midpoint) snap_lbl = is_vi ? L"Trung điểm (Midpoint)" : L"Midpoint";
        else if (st == tools::DimensionSnapType::Center) snap_lbl = is_vi ? L"Tâm (Center)" : L"Center";
        else if (st == tools::DimensionSnapType::Perpendicular) snap_lbl = is_vi ? L"Vuông góc (Perp)" : L"Perpendicular";
        else if (st == tools::DimensionSnapType::Quadrant) snap_lbl = is_vi ? L"1/4 Đ.tròn (Quad)" : L"Quadrant";
        else if (st == tools::DimensionSnapType::Intersection) snap_lbl = is_vi ? L"Giao điểm (Int)" : L"Intersection";
        else if (st == tools::DimensionSnapType::BoundingBoxCorner) snap_lbl = is_vi ? L"Góc khung bao" : L"Box Corner";

        ensure_ui_fonts();
        SelectObject(dc, font_secondary_);
        SetTextColor(dc, col_text);
        RECT rc_lbl{sp.x + 10, sp.y - 8, sp.x + 100, sp.y + 12};
        DrawTextW(dc, snap_lbl, -1, &rc_lbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // ORTHO mode indicator badge
    if (dimension_tool_->ortho_active()) {
        const auto cp = screen(pointer_current_world_);
        RECT rc_ortho{cp.x + 14, cp.y + 14, cp.x + 72, cp.y + 32};
        HBRUSH o_br = CreateSolidBrush(col_cyan);
        HPEN o_pn = CreatePen(PS_SOLID, 1, col_cyan);
        HGDIOBJ old_b = SelectObject(dc, o_br);
        HGDIOBJ old_p = SelectObject(dc, o_pn);
        RoundRect(dc, rc_ortho.left, rc_ortho.top, rc_ortho.right, rc_ortho.bottom, 4, 4);
        SelectObject(dc, old_b);
        SelectObject(dc, old_p);
        DeleteObject(o_br);
        DeleteObject(o_pn);

        ensure_ui_fonts();
        SelectObject(dc, font_secondary_);
        SetTextColor(dc, RGB(15, 23, 42));
        DrawTextW(dc, L"ORTHO", -1, &rc_ortho, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // If preview layout is valid, render lines, arrows, and measurement text
    if (dimension_tool_->has_preview()) {
        const auto& layout = dimension_tool_->preview_layout();
        if (layout.valid) {
            HPEN line_pen = CreatePen(PS_SOLID, 1, col_cyan);
            HGDIOBJ old_pen = SelectObject(dc, line_pen);

            for (const auto& line : layout.lines) {
                POINT start_pt{0, 0};
                for (const auto& el : line.elements()) {
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

            HBRUSH arr_brush = CreateSolidBrush(col_cyan);
            HGDIOBJ old_brush = SelectObject(dc, arr_brush);
            for (const auto& arr : layout.arrows) {
                std::vector<POINT> pts;
                pts.reserve(arr.elements().size());
                for (const auto& el : arr.elements()) {
                    if (el.verb == geometry::PathVerb::MoveTo || el.verb == geometry::PathVerb::LineTo) {
                        pts.push_back(screen(el.pt));
                    }
                }
                if (pts.size() >= 3) {
                    Polygon(dc, pts.data(), static_cast<int>(pts.size()));
                } else if (pts.size() == 2) {
                    MoveToEx(dc, pts[0].x, pts[0].y, nullptr);
                    LineTo(dc, pts[1].x, pts[1].y);
                }
            }
            SelectObject(dc, old_brush);
            DeleteObject(arr_brush);

            SelectObject(dc, old_pen);
            DeleteObject(line_pen);

            if (!layout.formatted_text.empty()) {
                const POINT tp = screen(layout.text_position);
                std::wstring wtext(layout.formatted_text.begin(), layout.formatted_text.end());

                ensure_ui_fonts();
                SelectObject(dc, font_secondary_);

                SIZE text_size{0, 0};
                GetTextExtentPoint32W(dc, wtext.c_str(), static_cast<int>(wtext.size()), &text_size);
                const int pad_x = 6;
                const int pad_y = 3;
                RECT badge_rc{tp.x - text_size.cx / 2 - pad_x,
                              tp.y - text_size.cy / 2 - pad_y,
                              tp.x + text_size.cx / 2 + pad_x,
                              tp.y + text_size.cy / 2 + pad_y};

                HBRUSH bg_brush = CreateSolidBrush(col_bg);
                HPEN border_pen = CreatePen(PS_SOLID, 1, col_cyan);
                old_brush = SelectObject(dc, bg_brush);
                old_pen = SelectObject(dc, border_pen);
                RoundRect(dc, badge_rc.left, badge_rc.top, badge_rc.right, badge_rc.bottom, 4, 4);
                SelectObject(dc, old_brush);
                SelectObject(dc, old_pen);
                DeleteObject(bg_brush);
                DeleteObject(border_pen);

                SetTextColor(dc, col_text);
                DrawTextW(dc, wtext.c_str(), -1, &badge_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
        }
    }

    RestoreDC(dc, saved);
}

void NativeWindow::paint_gradient_canvas_overlay(HDC dc, const RECT& area) {
    if (!gradient_tool_ || !gradient_tool_->active())
        return;
    if (gradient_tool_->target_objects().size() != 1)
        return;

    const auto* paint = gradient_tool_->active_draft_paint();
    if (!paint) return;

    const auto& camera = root().renderer().camera();
    const auto screen = [&](geometry::Point2D pt) {
        const auto world = gradient_tool_->object_to_world(pt);
        if (!world) return POINT{area.right + 64, area.bottom + 64};
        const auto local = camera.world_to_screen(*world);
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

    const auto preview_ptr = effective_color_preview();
    const auto to_preview_cr = [&](const color::ColorValue& cv) -> COLORREF {
        const auto preview = preview_ptr ? preview_ptr->convert(cv) : color::to_preview_rgba(cv);
        const auto dev = preview.to_device();
        return RGB(dev.r, dev.g, dev.b);
    };
    const uint64_t selected_stop_id = gradient_tool_->presenter().selected_stop_id();

    auto draw_diamond = [&](POINT pt, int r, COLORREF fill_cr, COLORREF border_cr) {
        POINT pts[4] = {
            {pt.x, pt.y - r},
            {pt.x + r, pt.y},
            {pt.x, pt.y + r},
            {pt.x - r, pt.y}
        };
        HBRUSH br = CreateSolidBrush(fill_cr);
        HPEN pen = CreatePen(PS_SOLID, 1, border_cr);
        auto old_br = SelectObject(dc, br);
        auto old_pen = SelectObject(dc, pen);
        Polygon(dc, pts, 4);
        SelectObject(dc, old_br);
        SelectObject(dc, old_pen);
        DeleteObject(pen);
        DeleteObject(br);
    };

    for (const auto& h : handles) {
        const auto pt = screen(h.position_object);
        if (h.kind == vector::tools::GradientHandleKind::LinearStart ||
            h.kind == vector::tools::GradientHandleKind::LinearEnd ||
            h.kind == vector::tools::GradientHandleKind::RadialCenter ||
            h.kind == vector::tools::GradientHandleKind::RadialRadius ||
            h.kind == vector::tools::GradientHandleKind::RadialAspect) {
            COLORREF fill_col = RGB(255, 255, 255);
            if (h.kind == vector::tools::GradientHandleKind::LinearStart && !paint->stops.empty()) {
                fill_col = to_preview_cr(paint->stops.front().color);
            } else if (h.kind == vector::tools::GradientHandleKind::LinearEnd && !paint->stops.empty()) {
                fill_col = to_preview_cr(paint->stops.back().color);
            }
            if (h.kind == vector::tools::GradientHandleKind::RadialAspect) {
                draw_diamond(pt, 5, fill_col, RGB(0, 242, 195));
            } else {
                HBRUSH br = CreateSolidBrush(fill_col);
                HPEN pen = CreatePen(PS_SOLID, 2, RGB(0, 242, 195));
                auto old_br = SelectObject(dc, br);
                auto old_pen = SelectObject(dc, pen);
                Rectangle(dc, pt.x - 5, pt.y - 5, pt.x + 6, pt.y + 6);
                SelectObject(dc, old_br);
                SelectObject(dc, old_pen);
                DeleteObject(pen);
                DeleteObject(br);
            }
        } else if (h.kind == vector::tools::GradientHandleKind::Midpoint) {
            draw_diamond(pt, 5, RGB(245, 245, 250), RGB(20, 24, 30));
        } else if (h.kind == vector::tools::GradientHandleKind::Stop) {
            const bool is_selected = h.stop_index < paint->stops.size() &&
                                     paint->stops[h.stop_index].id == selected_stop_id;
            if (is_selected) {
                HPEN halo_pen = CreatePen(PS_SOLID, 2, RGB(0, 242, 195));
                HBRUSH null_br = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
                auto old_pen = SelectObject(dc, halo_pen);
                auto old_br = SelectObject(dc, null_br);
                Ellipse(dc, pt.x - 8, pt.y - 8, pt.x + 9, pt.y + 9);
                SelectObject(dc, old_br);
                SelectObject(dc, old_pen);
                DeleteObject(halo_pen);
            }
            COLORREF stop_col = h.stop_index < paint->stops.size()
                ? to_preview_cr(paint->stops[h.stop_index].color)
                : RGB(255, 255, 255);
            HBRUSH br = CreateSolidBrush(stop_col);
            HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
            auto old_br = SelectObject(dc, br);
            auto old_pen = SelectObject(dc, pen);
            Ellipse(dc, pt.x - 5, pt.y - 5, pt.x + 6, pt.y + 6);
            HPEN border_pen = CreatePen(PS_SOLID, 1, RGB(30, 35, 45));
            SelectObject(dc, border_pen);
            SelectObject(dc, GetStockObject(NULL_BRUSH));
            Ellipse(dc, pt.x - 5, pt.y - 5, pt.x + 6, pt.y + 6);
            SelectObject(dc, old_br);
            SelectObject(dc, old_pen);
            DeleteObject(border_pen);
            DeleteObject(pen);
            DeleteObject(br);
        }
    }
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
    const COLORREF col_sel_guide = is_dark ? RGB(248, 113, 113) : RGB(220, 38, 38);
    const COLORREF col_sel_tag_bg = is_dark ? RGB(69, 10, 10) : RGB(254, 226, 226);

    int saved = SaveDC(dc);
    IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);

    // 1. Draw existing guidelines if show_guidelines_ is true
    if (show_guidelines_) {
        HPEN guide_pen = CreatePen(PS_DOT, 1, col_guide);
        HPEN sel_guide_pen = CreatePen(PS_SOLID, 2, col_sel_guide);
        HGDIOBJ old_pen = SelectObject(dc, guide_pen);
        HFONT guide_font = font_secondary_ ? font_secondary_ : font_be_vietnam_;
        HGDIOBJ old_font = SelectObject(dc, guide_font);
        SetBkMode(dc, TRANSPARENT);

        for (size_t i = 0; i < guidelines_.size(); ++i) {
            const auto& g = guidelines_[i];
            const bool is_sel = (static_cast<int>(i) == selected_guideline_index_);
            SelectObject(dc, is_sel ? sel_guide_pen : guide_pen);
            const COLORREF active_col = is_sel ? col_sel_guide : col_guide;
            const COLORREF active_tag_bg = is_sel ? col_sel_tag_bg : col_tag_bg;
            if (g.orientation == GuidelineOrientation::Horizontal) {
                const auto screen_pt = camera.world_to_screen({0.0, g.position_mm});
                const int sy = area.top + static_cast<int>(std::lround(screen_pt.y));
                if (sy >= area.top && sy <= area.bottom) {
                    MoveToEx(dc, area.left, sy, nullptr);
                    LineTo(dc, area.right, sy);

                    // Measurement tag near left ruler
                    const std::wstring tag = L"Y " + format_mm_display(
                        g.position_mm, display_length_unit_, 5);
                    SIZE tag_size{};
                    GetTextExtentPoint32W(dc, tag.c_str(), static_cast<int>(tag.size()), &tag_size);
                    const int max_tag_width = std::max(1, static_cast<int>(area.right - area.left - 8));
                    const int tag_width = std::min(max_tag_width, static_cast<int>(tag_size.cx) + 8);
                    RECT rc_tag = { area.left + 4, sy - 14,
                                    area.left + 4 + tag_width, sy - 1 };
                    HBRUSH tag_br = CreateSolidBrush(active_tag_bg);
                    FillRect(dc, &rc_tag, tag_br);
                    DeleteObject(tag_br);
                    SetTextColor(dc, active_col);
                    DrawTextW(dc, tag.c_str(), -1, &rc_tag, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
            } else {
                const auto screen_pt = camera.world_to_screen({g.position_mm, 0.0});
                const int sx = area.left + static_cast<int>(std::lround(screen_pt.x));
                if (sx >= area.left && sx <= area.right) {
                    MoveToEx(dc, sx, area.top, nullptr);
                    LineTo(dc, sx, area.bottom);

                    // Measurement tag near top ruler
                    const std::wstring tag = L"X " + format_mm_display(
                        g.position_mm, display_length_unit_, 5);
                    SIZE tag_size{};
                    GetTextExtentPoint32W(dc, tag.c_str(), static_cast<int>(tag.size()), &tag_size);
                    const int max_tag_width = std::max(1, static_cast<int>(area.right - area.left - 8));
                    const int tag_width = std::min(max_tag_width, static_cast<int>(tag_size.cx) + 8);
                    const int max_tag_left = std::max(static_cast<int>(area.left + 2),
                                                      static_cast<int>(area.right - 2) - tag_width);
                    const int tag_left = std::clamp(
                        static_cast<int>(sx) + 2, static_cast<int>(area.left) + 2, max_tag_left);
                    RECT rc_tag = { tag_left, area.top + 2,
                                    tag_left + tag_width, area.top + 15 };
                    HBRUSH tag_br = CreateSolidBrush(active_tag_bg);
                    FillRect(dc, &rc_tag, tag_br);
                    DeleteObject(tag_br);
                    SetTextColor(dc, active_col);
                    DrawTextW(dc, tag.c_str(), -1, &rc_tag, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
            }
        }

        SelectObject(dc, old_font);
        SelectObject(dc, old_pen);
        DeleteObject(guide_pen);
        DeleteObject(sel_guide_pen);
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
                    << format_mm_display(drag_guideline_pos_mm_, display_length_unit_, 5);
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
                    << format_mm_display(drag_guideline_pos_mm_, display_length_unit_, 5);
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
    const auto unit_scale = geometry::units::millimeters_per_unit(display_length_unit_).value_or(1.0);
    const auto [minor_x_unit, major_x_unit] = choose_step(visible_width / unit_scale);
    const auto [minor_y_unit, major_y_unit] = choose_step(visible_height / unit_scale);
    const double minor_x = minor_x_unit * unit_scale;
    const double major_x = major_x_unit * unit_scale;
    const double minor_y = minor_y_unit * unit_scale;
    const double major_y = major_y_unit * unit_scale;
    const auto& page = root().document().current_snapshot()->page();

    const COLORREF base = theme_ == ThemeMode::CyberDark ? RGB(15, 23, 34) : RGB(226, 232, 240);
    const COLORREF tick = theme_ == ThemeMode::CyberDark ? RGB(82, 98, 118) : RGB(100, 116, 139);
    const COLORREF text = theme_ == ThemeMode::CyberDark ? RGB(171, 185, 202) : RGB(71, 85, 105);
    HBRUSH base_brush = CreateSolidBrush(base);
    const int tb_w = toolbox_width();
    RECT top_ruler{area.left, 102, area.right, area.top};
    RECT left_ruler{tb_w, area.top, area.left, area.bottom};
    RECT corner{tb_w, 102, area.left, area.top};
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
    const auto unit_label = NativeWindow::utf8_to_wide(
        std::string(display_length_unit_suffix(display_length_unit_)));
    TextOutW(dc, tb_w + 3, 104, unit_label.c_str(), static_cast<int>(unit_label.size()));
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
            const auto label = NativeWindow::utf8_to_wide(
                format_ruler_value_mm(value, display_length_unit_, major_x_unit));
            TextOutW(dc, x + 2, 103, label.c_str(), static_cast<int>(label.size()));
        }
    }
    const double bottom_world = camera.screen_to_world(
        {0.0f, static_cast<float>(area.bottom - area.top)}).y;
    const double top_world = camera.screen_to_world({0.0f, 0.0f}).y;
    const double first_y = std::floor(bottom_world / minor_y) * minor_y;
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, tb_w, area.top, area.left, area.bottom);
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
            const auto label = NativeWindow::utf8_to_wide(
                format_ruler_value_mm(value, display_length_unit_, major_y_unit));
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

void NativeWindow::paint_annotation_canvas_overlay(HDC dc, const RECT& area) {
    if (!annotation_tool_ || !annotation_tool_->active() || !annotation_tool_->has_preview())
        return;

    const auto& camera = root().renderer().camera();
    const auto screen = [&](geometry::Point2D pt) {
        const auto local = camera.world_to_screen(pt);
        return POINT{area.left + static_cast<LONG>(std::lround(local.x)),
                     area.top + static_cast<LONG>(std::lround(local.y))};
    };

    const int saved = SaveDC(dc);
    SetBkMode(dc, TRANSPARENT);

    const COLORREF col_accent = RGB(0, 242, 195);
    const COLORREF col_bg = theme_ == ThemeMode::CyberDark ? RGB(15, 23, 42) : RGB(255, 255, 255);
    const COLORREF col_text = theme_ == ThemeMode::CyberDark ? RGB(255, 255, 255) : RGB(30, 41, 59);

    auto draw_path = [&](const geometry::Path2D& path, COLORREF col, int pen_style) {
        HPEN pen = CreatePen(pen_style, 1, col);
        HGDIOBJ old_pen = SelectObject(dc, pen);
        POINT start_pt{0, 0};
        for (const auto& el : path.elements()) {
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
        SelectObject(dc, old_pen);
        DeleteObject(pen);
    };

    if (annotation_tool_->mode() == tools::AnnotationToolMode::DetailView) {
        const auto& dv_layout = annotation_tool_->preview_detail_view_layout();
        draw_path(dv_layout.marker_path, col_accent, PS_DOT);
        draw_path(dv_layout.frame_path, col_accent, PS_SOLID);
        draw_path(dv_layout.connector_path, col_accent, PS_DOT);

        if (!dv_layout.formatted_label.empty()) {
            const POINT lp = screen(dv_layout.label_origin);
            std::wstring wlabel(dv_layout.formatted_label.begin(), dv_layout.formatted_label.end());
            ensure_ui_fonts();
            SelectObject(dc, font_secondary_);
            SIZE text_size{0, 0};
            GetTextExtentPoint32W(dc, wlabel.c_str(), static_cast<int>(wlabel.size()), &text_size);
            RECT text_rc{lp.x - text_size.cx / 2 - 4, lp.y - text_size.cy / 2 - 2,
                         lp.x + text_size.cx / 2 + 4, lp.y + text_size.cy / 2 + 2};
            HBRUSH bg_brush = CreateSolidBrush(col_bg);
            HPEN border_pen = CreatePen(PS_SOLID, 1, col_accent);
            HGDIOBJ old_brush = SelectObject(dc, bg_brush);
            HGDIOBJ old_pen = SelectObject(dc, border_pen);
            RoundRect(dc, text_rc.left, text_rc.top, text_rc.right, text_rc.bottom, 4, 4);
            SelectObject(dc, old_brush);
            SelectObject(dc, old_pen);
            DeleteObject(bg_brush);
            DeleteObject(border_pen);

            SetTextColor(dc, col_text);
            DrawTextW(dc, wlabel.c_str(), -1, &text_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    } else {
        const auto& ann_layout = annotation_tool_->preview_annotation_layout();
        draw_path(ann_layout.leader_path, col_accent, PS_SOLID);

        if (!ann_layout.arrow_path.empty()) {
            HBRUSH arr_brush = CreateSolidBrush(col_accent);
            HPEN arr_pen = CreatePen(PS_SOLID, 1, col_accent);
            HGDIOBJ old_brush = SelectObject(dc, arr_brush);
            HGDIOBJ old_pen = SelectObject(dc, arr_pen);
            std::vector<POINT> pts;
            for (const auto& el : ann_layout.arrow_path.elements()) {
                if (el.verb == geometry::PathVerb::MoveTo || el.verb == geometry::PathVerb::LineTo) {
                    pts.push_back(screen(el.pt));
                }
            }
            if (pts.size() >= 3) {
                Polygon(dc, pts.data(), static_cast<int>(pts.size()));
            }
            SelectObject(dc, old_brush);
            SelectObject(dc, old_pen);
            DeleteObject(arr_brush);
            DeleteObject(arr_pen);
        }

        if (!ann_layout.formatted_text.empty()) {
            const POINT tp = screen(ann_layout.text_origin);
            std::wstring wtext(ann_layout.formatted_text.begin(), ann_layout.formatted_text.end());
            ensure_ui_fonts();
            SelectObject(dc, font_secondary_);
            SIZE text_size{0, 0};
            GetTextExtentPoint32W(dc, wtext.c_str(), static_cast<int>(wtext.size()), &text_size);
            RECT text_rc{tp.x, tp.y - text_size.cy, tp.x + text_size.cx + 6, tp.y + 4};
            HBRUSH bg_brush = CreateSolidBrush(col_bg);
            HPEN border_pen = CreatePen(PS_SOLID, 1, col_accent);
            HGDIOBJ old_brush = SelectObject(dc, bg_brush);
            HGDIOBJ old_pen = SelectObject(dc, border_pen);
            RoundRect(dc, text_rc.left, text_rc.top, text_rc.right, text_rc.bottom, 4, 4);
            SelectObject(dc, old_brush);
            SelectObject(dc, old_pen);
            DeleteObject(bg_brush);
            DeleteObject(border_pen);

            SetTextColor(dc, col_text);
            RECT draw_rc{text_rc.left + 3, text_rc.top + 2, text_rc.right - 3, text_rc.bottom - 2};
            DrawTextW(dc, wtext.c_str(), -1, &draw_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    RestoreDC(dc, saved);
}

#endif

} // namespace pikdraw::desktop
