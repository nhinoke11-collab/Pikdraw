#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_file_menu.hpp"
#include "native_window_menus.hpp"
#include "native_window_page_presets.hpp"
#include "../tools/drawing_tool_controller.hpp"
#include "../tools/pen_tool_controller.hpp"
#include "../tools/node_tool_controller.hpp"
#include "../tools/contour_tool_controller.hpp"
#include "../tools/gradient_tool_controller.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windowsx.h>
#endif

namespace pikdraw::desktop {

#ifdef _WIN32

void NativeWindow::update_pen_dimension_candidate() {
    geometry::Point2D base_world{0.0, 0.0};
    bool has_base = false;
    if (active_tool_ == 8 && pen_tool_ && pen_tool_->active()) {
        if (pen_tool_->anchor_count() > 0 && !pen_tool_->draft().nodes.empty()) {
            base_world = pen_tool_->world_point_from_source(pen_tool_->draft().nodes.back().anchor);
            has_base = true;
        }
    } else if ((active_tool_ == 6 || active_tool_ == 7) && drawing_tool_ && drawing_tool_->active()) {
        const auto state = drawing_tool_->visual_state();
        if (!state.points.empty()) {
            base_world = state.points.back();
            has_base = true;
        }
    }
    if (!has_base) return;

    // Get length value
    std::string len_str = (active_num_field_ == NumericFieldId::PenLength) ? num_input_buffer_ : pen_numeric_length_;
    std::replace(len_str.begin(), len_str.end(), ',', '.');
    double length_mm = 0.0;
    bool has_length = false;
    if (!len_str.empty()) {
        try {
            size_t p = 0;
            double v = std::stod(len_str, &p);
            if (p == len_str.size() && std::isfinite(v) && v > 0.0) {
                length_mm = v;
                has_length = true;
            }
        } catch (...) {}
    }

    // Get angle value
    std::string ang_str = (active_num_field_ == NumericFieldId::PenAngle) ? num_input_buffer_ : pen_numeric_angle_;
    std::replace(ang_str.begin(), ang_str.end(), ',', '.');
    double angle_deg = 0.0;
    bool has_angle = false;
    if (!ang_str.empty()) {
        try {
            size_t p = 0;
            double v = std::stod(ang_str, &p);
            if (p == ang_str.size() && std::isfinite(v)) {
                angle_deg = v;
                has_angle = true;
            }
        } catch (...) {}
    }

    constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
    constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
    geometry::Point2D target_world = pen_last_pointer_world_;

    if (has_length && has_angle) {
        // Direct reference to horizontal X-axis: dx = L*cos(theta), dy = L*sin(theta)
        const double rad = angle_deg * kDegToRad;
        target_world = {
            base_world.x + length_mm * std::cos(rad),
            base_world.y + length_mm * std::sin(rad)
        };
    } else if (has_length) {
        const auto delta = pen_last_pointer_world_ - base_world;
        const double dist = std::hypot(delta.x, delta.y);
        geometry::Point2D dir{1.0, 0.0};
        if (dist > 1e-6) {
            // AutoCAD polar tracking snap (15-degree steps)
            double raw_deg = std::atan2(delta.y, delta.x) * kRadToDeg;
            if (raw_deg < 0.0) raw_deg += 360.0;
            double snap_deg = std::round(raw_deg / 15.0) * 15.0;
            if (snap_deg >= 360.0) snap_deg -= 360.0;
            double diff = std::abs(raw_deg - snap_deg);
            if (diff > 180.0) diff = 360.0 - diff;
            if (diff <= 3.5) {
                const double rad = snap_deg * kDegToRad;
                dir = {std::cos(rad), std::sin(rad)};
            } else {
                dir = {delta.x / dist, delta.y / dist};
            }
        }
        target_world = {
            base_world.x + length_mm * dir.x,
            base_world.y + length_mm * dir.y
        };
    } else if (has_angle) {
        const auto delta = pen_last_pointer_world_ - base_world;
        double dist = std::hypot(delta.x, delta.y);
        if (dist < 1.0) dist = 20.0;
        const double rad = angle_deg * kDegToRad;
        target_world = {
            base_world.x + dist * std::cos(rad),
            base_world.y + dist * std::sin(rad)
        };
    }

    if (active_tool_ == 8 && pen_tool_) {
        (void)pen_tool_->pointer_move(target_world, false);
    } else if ((active_tool_ == 6 || active_tool_ == 7) && drawing_tool_) {
        drawing_tool_->pointer_move(target_world, root().renderer().camera(),
            window_dpi_scale(), false, false);
    }
}

bool NativeWindow::handle_pen_pointer_down(int x, int y, unsigned modifiers) {
    if (active_tool_ != 8 || !pen_tool_ || !pen_tool_->active() ||
        !is_canvas_point(x, y))
        return false;

    pen_pointer_start_x_ = x;
    pen_pointer_start_y_ = y;
    pen_pointer_dragged_ = false;
    pen_last_pointer_world_ = pen_snap_point(canvas_to_world(x, y), modifiers);
    pen_close_candidate_ = false;

    const auto& draft = pen_tool_->draft();
    if (!pen_tool_->resuming() && draft.nodes.size() >= 2) {
        const auto area = canvas_rect(width_, height_);
        const auto first_world = pen_tool_->world_point_from_source(draft.nodes.front().anchor);
        const auto first_screen = root().renderer().camera().world_to_screen(first_world);
        const double scale = window_dpi_scale();
        const double dx = static_cast<double>(x - area.left) - first_screen.x;
        const double dy = static_cast<double>(y - area.top) - first_screen.y;
        if (std::hypot(dx, dy) <= 8.0 * scale) {
            pen_close_candidate_ = true;
            if (hwnd_) SetCapture(hwnd_);
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Nhấp điểm đầu để đóng đường · Enter để kết thúc mở"
                : "Click the first point to close · Enter finishes open";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
    }

    pen_numeric_buffer_.clear();
    const auto result = pen_tool_->pointer_down(pen_last_pointer_world_);
    if (result.code == vector::tools::pen::PenDrawingResultCode::InvalidInput ||
        result.code == vector::tools::pen::PenDrawingResultCode::Stale) {
        status_msg_ = result.message;
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    (void)modifiers;
    if (hwnd_) SetCapture(hwnd_);
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Kéo để tạo tay nắm cong · Alt tách tay nắm · nhả để đặt điểm"
        : "Drag for curve handles · Alt separates handles · release to place";
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool NativeWindow::handle_pen_pointer_move(int x, int y, unsigned modifiers) {
    if (active_tool_ != 8 || !pen_tool_ || !pen_tool_->active() || is_panning_)
        return false;
    const bool gesture = pen_close_candidate_ || pen_tool_->pointer_pending();
    if (!gesture && !is_canvas_point(x, y))
        return false;

    const double scale = window_dpi_scale();
    const double dx = static_cast<double>(x - pen_pointer_start_x_);
    const double dy = static_cast<double>(y - pen_pointer_start_y_);
    if (gesture && !pen_pointer_dragged_ && std::hypot(dx, dy) >= 3.0 * scale)
        pen_pointer_dragged_ = true;
    const auto raw_world = canvas_to_world(x, y);
    pen_last_pointer_world_ = gesture ? raw_world :
        pen_numeric_buffer_.empty() ? pen_snap_point(raw_world, modifiers) : raw_world;

    if (pen_close_candidate_) {
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    const tools::PenGestureModifiers pen_modifiers{
        (modifiers & MK_SHIFT) != 0,
        (modifiers & MK_CONTROL) != 0,
        (GetKeyState(VK_MENU) & 0x8000) != 0};
    vector::tools::pen::PenDrawingResult result;
    if (pen_tool_->pointer_pending()) {
        result = pen_tool_->pointer_move(pen_last_pointer_world_, pen_pointer_dragged_,
                                         pen_modifiers);
    } else if (active_num_field_ == NumericFieldId::PenLength ||
               active_num_field_ == NumericFieldId::PenAngle) {
        update_pen_dimension_candidate();
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    } else if (!pen_numeric_buffer_.empty()) {
        result = pen_tool_->update_numeric_candidate(pen_numeric_buffer_);
    } else {
        result = pen_tool_->pointer_move(pen_last_pointer_world_, false,
                                         pen_modifiers);
    }
    if (result.code == vector::tools::pen::PenDrawingResultCode::InvalidInput ||
        result.code == vector::tools::pen::PenDrawingResultCode::Stale ||
        result.code == vector::tools::pen::PenDrawingResultCode::BudgetExceeded) {
        status_msg_ = result.message;
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

geometry::Point2D NativeWindow::pen_snap_point(geometry::Point2D raw_world,
                                               unsigned modifiers) {
    if (active_tool_ == 8 && pen_tool_ && pen_tool_->active()) {
        const auto& draft = pen_tool_->draft();
        if (!draft.nodes.empty()) {
            const auto base_world = pen_tool_->world_point_from_source(draft.nodes.back().anchor);
            const auto delta = raw_world - base_world;
            const double dist = std::hypot(delta.x, delta.y);
            if (dist > 0.5) {
                constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
                constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
                double raw_deg = std::atan2(delta.y, delta.x) * kRadToDeg;
                if (raw_deg < 0.0) raw_deg += 360.0;
                double snap_deg = std::round(raw_deg / 15.0) * 15.0;
                if (snap_deg >= 360.0) snap_deg -= 360.0;
                double diff = std::abs(raw_deg - snap_deg);
                if (diff > 180.0) diff = 360.0 - diff;
                if (diff <= 3.5 || (modifiers & MK_SHIFT) != 0) {
                    const double rad = snap_deg * kDegToRad;
                    return geometry::Point2D{
                        base_world.x + dist * std::cos(rad),
                        base_world.y + dist * std::sin(rad)
                    };
                }
            }
        }
        return raw_world;
    }
    if (!drawing_tool_ || !drawing_tool_->active()) return raw_world;
    const bool shift = (modifiers & MK_SHIFT) != 0;
    const bool control = (modifiers & MK_CONTROL) != 0;
    drawing_tool_->pointer_move(raw_world, root().renderer().camera(),
        window_dpi_scale(), shift, control);
    const auto state = drawing_tool_->visual_state();
    return state.active && state.candidate ? *state.candidate : raw_world;
}

bool NativeWindow::handle_pen_pointer_up(int x, int y, unsigned modifiers) {
    if (active_tool_ != 8 || !pen_tool_ || !pen_tool_->active() ||
        (!pen_close_candidate_ && !pen_tool_->pointer_pending()))
        return false;

    const double scale = window_dpi_scale();
    const double dx = static_cast<double>(x - pen_pointer_start_x_);
    const double dy = static_cast<double>(y - pen_pointer_start_y_);
    if (!pen_pointer_dragged_ && std::hypot(dx, dy) >= 3.0 * scale)
        pen_pointer_dragged_ = true;
    pen_last_pointer_world_ = canvas_to_world(x, y);
    const bool close_candidate = pen_close_candidate_;
    pen_close_candidate_ = false;

    if (close_candidate) {
        if (GetCapture() == hwnd_) ReleaseCapture();
        const auto area = canvas_rect(width_, height_);
        const auto& draft = pen_tool_->draft();
        bool on_start = false;
        if (!draft.nodes.empty()) {
            const auto first_world = pen_tool_->world_point_from_source(draft.nodes.front().anchor);
            const auto first_screen = root().renderer().camera().world_to_screen(first_world);
            const double sx = static_cast<double>(x - area.left) - first_screen.x;
            const double sy = static_cast<double>(y - area.top) - first_screen.y;
            on_start = std::hypot(sx, sy) <= 8.0 * scale;
        }
        if (!pen_pointer_dragged_ && on_start)
            return finish_pen_session(true);
        pen_pointer_dragged_ = false;
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đường chưa đóng · nhấp đúng điểm đầu hoặc nhấn Enter"
            : "Path remains open · click the first point or press Enter";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    const tools::PenGestureModifiers pen_modifiers{
        (modifiers & MK_SHIFT) != 0,
        (modifiers & MK_CONTROL) != 0,
        (GetKeyState(VK_MENU) & 0x8000) != 0};
    (void)pen_tool_->pointer_move(pen_last_pointer_world_, pen_pointer_dragged_, pen_modifiers);
    const auto result = pen_tool_->pointer_up();
    if (GetCapture() == hwnd_) ReleaseCapture();
    pen_pointer_dragged_ = false;
    if (result.code == vector::tools::pen::PenDrawingResultCode::InvalidInput ||
        result.code == vector::tools::pen::PenDrawingResultCode::Stale ||
        result.code == vector::tools::pen::PenDrawingResultCode::BudgetExceeded) {
        status_msg_ = result.message;
    } else if (!pen_numeric_buffer_.empty()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã đặt điểm · gõ @L<góc hoặc #X;Y · Enter kết thúc"
            : "Point placed · type @L<angle or #X;Y · Enter finishes";
    } else {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Nhấp điểm kế · kéo tạo cong · Enter kết thúc · Esc kết thúc / Shift+Esc hủy"
            : "Click next point · drag for curve · Enter finishes · Esc finishes / Shift+Esc cancels";
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool NativeWindow::handle_node_pointer_down(int x, int y, unsigned modifiers) {
    if (active_tool_ != 11 || !node_tool_ || !node_tool_->active() || is_panning_)
        return false;
    if (!is_canvas_point(x, y))
        return false;
    const auto world_pt = canvas_to_world(x, y);
    const bool shift = (modifiers & MK_SHIFT) != 0;
    const double zoom = root().renderer().camera().zoom();
    const double tol_mm = (zoom > 1e-4) ? (8.0 / zoom) : 5.0;
    const bool handled = node_tool_->pointer_down(world_pt, shift, tol_mm);
    if (handled) {
        if (hwnd_) SetCapture(hwnd_);
        const size_t count = node_tool_->selected_node_count();
        if (count > 0) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đang chọn " + std::to_string(count) + " node · Kéo để di chuyển hoặc kéo tiếp tuyến Bézier"
                : "Selected " + std::to_string(count) + " node(s) · Drag to move or adjust Bézier handles";
        }
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    }
    return handled;
}

bool NativeWindow::handle_node_pointer_move(int x, int y, unsigned modifiers) {
    (void)modifiers;
    if (active_tool_ != 11 || !node_tool_ || !node_tool_->active() || is_panning_)
        return false;
    if (GetCapture() != hwnd_)
        return false;
    const auto world_pt = canvas_to_world(x, y);
    const bool handled = node_tool_->pointer_move(world_pt);
    if (handled && hwnd_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    return handled;
}

bool NativeWindow::handle_node_pointer_up(int x, int y, unsigned modifiers) {
    (void)modifiers;
    if (active_tool_ != 11 || !node_tool_ || !node_tool_->active())
        return false;
    if (GetCapture() == hwnd_)
        ReleaseCapture();
    const auto world_pt = canvas_to_world(x, y);
    const bool handled = node_tool_->pointer_up(world_pt);
    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    return handled;
}

bool NativeWindow::handle_node_property_click(int x, int y) {
    if (active_tool_ != 11 || !node_tool_ || !node_tool_->active())
        return false;
    const int topbar_h = 32;
    const int tabbar_h = 32;
    const int content_top = topbar_h + tabbar_h + 40;
    if (y < topbar_h + tabbar_h || y > content_top)
        return false;

    if (x >= 90 && x < 165) {
        if (node_tool_->add_node_at_midpoint()) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã thêm điểm node tại trung điểm đoạn" : "Added node at midpoint";
        }
    } else if (x >= 170 && x < 245) {
        if (node_tool_->delete_selected()) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã xóa các điểm node được chọn" : "Deleted selected nodes";
        }
    } else if (x >= 250 && x < 325) {
        if (node_tool_->break_selected()) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã tách đường tại node" : "Broken path at node";
        }
    } else if (x >= 330 && x < 405) {
        if (node_tool_->join_selected(true)) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã nối 2 node đầu mút" : "Joined 2 end nodes";
        }
    } else if (x >= 410 && x < 485) {
        if (node_tool_->close_selected()) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã đóng kín đường cong" : "Closed subpath";
        }
    } else if (x >= 490 && x < 570) {
        if (node_tool_->set_selected_mode(document::PathNodeMode::Cusp)) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đổi sang chế độ Node Cusp (Góc nhọn)" : "Changed node to Cusp mode";
        }
    } else if (x >= 575 && x < 655) {
        if (node_tool_->set_selected_mode(document::PathNodeMode::Smooth)) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đổi sang chế độ Node Smooth (Mượt trơn)" : "Changed node to Smooth mode";
        }
    } else if (x >= 660 && x < 740) {
        if (node_tool_->set_selected_mode(document::PathNodeMode::Symmetric)) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đổi sang chế độ Node Symmetric (Đối xứng)" : "Changed node to Symmetric mode";
        }
    } else if (x >= 745 && x < 830) {
        if (node_tool_->convert_selected_segment(document::PathSegmentKind::Line)) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Chuyển đoạn cong thành đoạn thẳng" : "Converted segment to Line";
        }
    } else if (x >= 835 && x < 920) {
        if (node_tool_->convert_selected_segment(document::PathSegmentKind::CubicBezier)) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Chuyển đoạn thẳng thành đường cong Bézier" : "Converted segment to Cubic Bézier";
        }
    } else if (x >= 925 && x < 1010) {
        if (node_tool_->reverse_selected()) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã đảo chiều thứ tự đường vẽ" : "Reversed subpath direction";
        }
    } else {
        return false;
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool NativeWindow::handle_contour_property_click(int x, int y) {
    if (active_tool_ != 12 || !contour_tool_ || !contour_tool_->active())
        return false;
    const int topbar_h = 32;
    const int tabbar_h = 32;
    const int content_top = topbar_h + tabbar_h + 40;
    if (y < topbar_h + tabbar_h || y > content_top)
        return false;

    if (x >= 80 && x < 145) {
        contour_tool_->set_direction(geometry::ContourDirection::Outside);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đổi hướng: Viền ngoài (Outside)" : "Direction: Outside";
    } else if (x >= 150 && x < 215) {
        contour_tool_->set_direction(geometry::ContourDirection::Inside);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đổi hướng: Viền trong (Inside)" : "Direction: Inside";
    } else if (x >= 220 && x < 295) {
        contour_tool_->set_direction(geometry::ContourDirection::ToCenter);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đổi hướng: Viền vào tâm (To Center)" : "Direction: To Center";
    } else if (x >= 305 && x < 330) {
        contour_tool_->set_spacing_mm(std::max(0.1, contour_tool_->spacing_mm() - 0.5));
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Khoảng cách viền: " + std::to_string(contour_tool_->spacing_mm()) + " mm"
            : "Contour spacing: " + std::to_string(contour_tool_->spacing_mm()) + " mm";
    } else if (x >= 410 && x < 435) {
        contour_tool_->set_spacing_mm(contour_tool_->spacing_mm() + 0.5);
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Khoảng cách viền: " + std::to_string(contour_tool_->spacing_mm()) + " mm"
            : "Contour spacing: " + std::to_string(contour_tool_->spacing_mm()) + " mm";
    } else if (x >= 445 && x < 470) {
        contour_tool_->set_steps(std::max(1, contour_tool_->steps() - 1));
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Số bước viền: " + std::to_string(contour_tool_->steps())
            : "Contour steps: " + std::to_string(contour_tool_->steps());
    } else if (x >= 540 && x < 565) {
        contour_tool_->set_steps(contour_tool_->steps() + 1);
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Số bước viền: " + std::to_string(contour_tool_->steps())
            : "Contour steps: " + std::to_string(contour_tool_->steps());
    } else if (x >= 575 && x < 630) {
        contour_tool_->set_join_type(geometry::JoinType::Miter);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Góc nối: Nhọn (Miter)" : "Corner join: Miter";
    } else if (x >= 635 && x < 690) {
        contour_tool_->set_join_type(geometry::JoinType::Round);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Góc nối: Tròn (Round)" : "Corner join: Round";
    } else if (x >= 695 && x < 750) {
        contour_tool_->set_join_type(geometry::JoinType::Bevel);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Góc nối: Vát (Bevel)" : "Corner join: Bevel";
    } else if (x >= 760 && x < 825) {
        if (contour_tool_->apply()) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đã áp dụng viền Contour vào đối tượng" : "Applied contour to object";
        }
    } else if (x >= 830 && x < 940) {
        trigger_break_contour_apart();
    } else {
        return false;
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool NativeWindow::handle_gradient_property_click(int x, int y) {
    if (active_tool_ != 13 || !gradient_tool_ || !gradient_tool_->active())
        return false;
    const int topbar_h = 32;
    const int tabbar_h = 32;
    const int content_top = topbar_h + tabbar_h + 40;
    if (y < topbar_h + tabbar_h || y > content_top)
        return false;

    if (x >= 80 && x < 145) {
        gradient_tool_->presenter().set_kind(document::GradientKind::Linear);
        gradient_tool_->tool().set_kind(document::GradientKind::Linear);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Kiểu: Tuyến tính (Linear)" : "Kind: Linear";
    } else if (x >= 150 && x < 215) {
        gradient_tool_->presenter().set_kind(document::GradientKind::Radial);
        gradient_tool_->tool().set_kind(document::GradientKind::Radial);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Kiểu: Tỏa tròn (Radial)" : "Kind: Radial";
    } else if (x >= 225 && x < 275) {
        gradient_tool_->presenter().set_spread(document::GradientSpread::Pad);
        gradient_tool_->tool().set_spread(document::GradientSpread::Pad);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Tràn viền: Chặn (Pad)" : "Spread: Pad";
    } else if (x >= 280 && x < 345) {
        gradient_tool_->presenter().set_spread(document::GradientSpread::Repeat);
        gradient_tool_->tool().set_spread(document::GradientSpread::Repeat);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Tràn viền: Lặp lại (Repeat)" : "Spread: Repeat";
    } else if (x >= 350 && x < 420) {
        gradient_tool_->presenter().set_spread(document::GradientSpread::Reflect);
        gradient_tool_->tool().set_spread(document::GradientSpread::Reflect);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Tràn viền: Đối xứng (Reflect)" : "Spread: Reflect";
    } else if (x >= 430 && x < 500) {
        gradient_tool_->presenter().reverse_stops();
        gradient_tool_->tool().reverse_stops();
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đã đảo chiều dải màu" : "Reversed gradient stops";
    } else if (x >= 505 && x < 575) {
        const auto bounds = root().vector_controller().selection().bounds(*root().document().current_snapshot()).value_or(geometry::Rect2D{0, 0, 100, 100});
        gradient_tool_->presenter().fit_to_bounds(bounds);
        gradient_tool_->tool().fit_to_object_bounds(bounds);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đã khớp dải màu theo khung đối tượng" : "Fit gradient to bounds";
    } else if (x >= 585 && x < 665) {
        auto proj = gradient_tool_->presenter().geometry_projection();
        proj.angle_degrees = std::fmod(proj.angle_degrees + 45.0, 360.0);
        gradient_tool_->presenter().set_angle_degrees(proj.angle_degrees);
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Góc quay: " + std::to_string(static_cast<int>(proj.angle_degrees)) + "°"
            : "Angle: " + std::to_string(static_cast<int>(proj.angle_degrees)) + "°";
    } else if (x >= 675 && x < 735) {
        gradient_tool_->presenter().add_stop(0.5, color::ColorValue{color::RGBColor{1.0, 0.784, 0.196, 1.0}});
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đã thêm điểm dừng màu ở vị trí 50%" : "Added color stop at 50%";
    } else if (x >= 745 && x < 815) {
        if (gradient_tool_->apply()) {
            status_msg_ = language_ == UILanguage::Vietnamese ? "Đã áp dụng chuyển sắc vào đối tượng" : "Applied gradient fill";
        }
    } else if (x >= 820 && x < 880) {
        gradient_tool_->cancel();
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đã hủy thay đổi chuyển sắc" : "Cancelled gradient edits";
    } else {
        return false;
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool NativeWindow::handle_gradient_pointer_down(int x, int y, unsigned modifiers) {
    if (active_tool_ != 13 || !gradient_tool_ || !gradient_tool_->active() || is_panning_)
        return false;
    if (!is_canvas_point(x, y))
        return false;
    const auto world_pt = canvas_to_world(x, y);
    const bool shift = (modifiers & MK_SHIFT) != 0;
    const bool ctrl = (modifiers & MK_CONTROL) != 0;
    gradient_tool_->on_pointer_down(world_pt, shift, ctrl);
    if (hwnd_) SetCapture(hwnd_);
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Đang kéo chỉnh Gradient · Giữ Shift khóa góc 15° · Thả chuột để áp dụng"
        : "Dragging Gradient · Hold Shift for 15° snap · Release mouse to apply";
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool NativeWindow::handle_gradient_pointer_move(int x, int y, unsigned modifiers) {
    if (active_tool_ != 13 || !gradient_tool_ || !gradient_tool_->active() || is_panning_)
        return false;
    if (GetCapture() != hwnd_)
        return false;
    const auto world_pt = canvas_to_world(x, y);
    const bool shift = (modifiers & MK_SHIFT) != 0;
    const bool ctrl = (modifiers & MK_CONTROL) != 0;
    gradient_tool_->on_pointer_move(world_pt, shift, ctrl);
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool NativeWindow::handle_gradient_pointer_up(int x, int y, unsigned modifiers) {
    (void)modifiers;
    if (active_tool_ != 13 || !gradient_tool_ || !gradient_tool_->active())
        return false;
    if (GetCapture() == hwnd_)
        ReleaseCapture();
    const auto world_pt = canvas_to_world(x, y);
    const bool committed = gradient_tool_->on_pointer_up(world_pt);
    if (committed) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã áp dụng chuyển sắc vào đối tượng · Ctrl+Z để hoàn tác"
            : "Applied gradient fill to object · Ctrl+Z to undo";
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool NativeWindow::handle_pen_character(char value) {
    if (active_tool_ != 8 || !pen_tool_ || !pen_tool_->active() ||
        active_num_field_ != NumericFieldId::None ||
        (GetKeyState(VK_CONTROL) & 0x8000) != 0 ||
        (GetKeyState(VK_MENU) & 0x8000) != 0)
        return false;

    const bool numeric_start = (value >= '0' && value <= '9') || value == '-' ||
        value == '+' || value == '@' || value == '#' || value == '.';
    const bool expression_char = numeric_start ||
        (!pen_numeric_buffer_.empty() &&
         ((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
          value == '<' || value == '>' || value == ';' || value == '/' ||
          value == '*' || value == '(' || value == ')' || value == ' ' || value == ','));
    if (!expression_char) return false;
    if (pen_numeric_buffer_.size() >= 256) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Biểu thức Pen vượt giới hạn 256 ký tự"
            : "Pen expression exceeds the 256-character limit";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    pen_numeric_buffer_.push_back(value);
    const auto result = pen_tool_->update_numeric_candidate(pen_numeric_buffer_);
    status_msg_ = result.code == vector::tools::pen::PenDrawingResultCode::InvalidInput
        ? result.message
        : language_ == UILanguage::Vietnamese
            ? "Xem trước số · Enter đặt điểm · Enter lần nữa kết thúc"
            : "Numeric preview · Enter places point · Enter again finishes";
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool NativeWindow::finish_pen_session(bool close_path, bool cancel) {
    if (!pen_tool_ || !pen_tool_->active()) return false;
    if (active_num_field_ == NumericFieldId::PenExpression ||
        active_num_field_ == NumericFieldId::PenLength ||
        active_num_field_ == NumericFieldId::PenAngle) {
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
    }
    pen_numeric_buffer_.clear();
    pen_numeric_length_.clear();
    pen_numeric_angle_.clear();
    if (cancel) {
        pen_tool_->cancel();
        workflow().cancel_effect_draft(); set_active_tool(0);
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã hủy bản nháp Pen · tài liệu không thay đổi"
            : "Pen draft cancelled · document unchanged";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    const auto result = close_path ? pen_tool_->close() : pen_tool_->finish_open();
    if (!result.committed() || !result.object_id) {
        status_msg_ = result.message.empty()
            ? (language_ == UILanguage::Vietnamese ? "Không thể kết thúc đường Pen" : "Could not finish the Pen path")
            : result.message;
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    const auto snapshot = root().document().current_snapshot();
    if (snapshot)
        root().vector_controller().set_selection({*result.object_id}, *snapshot);
    workflow().cancel_effect_draft(); set_active_tool(0);
    status_msg_ = close_path
        ? (language_ == UILanguage::Vietnamese ? "Đã đóng đường Pen" : "Pen path closed")
        : (language_ == UILanguage::Vietnamese ? "Đã hoàn tất đường Pen" : "Pen path finished");
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}
#endif

} // namespace pikdraw::desktop
