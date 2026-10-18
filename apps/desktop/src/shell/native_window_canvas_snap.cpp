#include "pikdraw/desktop/shell/native_window.hpp"
#include "../tools/drawing_tool_controller.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numbers>
#include <sstream>
#include <vector>

namespace pikdraw::desktop {

#ifdef _WIN32

void NativeWindow::paint_drawing_tool_overlays(HDC dc) {
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
