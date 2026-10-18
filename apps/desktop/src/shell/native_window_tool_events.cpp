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
    if (active_dock_tab_ == 0 && x >= width_ - 280 && x <= width_ - 14 &&
        y >= 162 && y <= 310) {
        const int dock_left = width_ - 280;
        const auto selected_position = node_tool_->single_selected_node_position();
        if (y >= 186 && y <= 218 && x >= dock_left + 132) {
            if (selected_position) start_numeric_input(NumericFieldId::ObjectX);
            return true;
        }
        if (y >= 226 && y <= 258 && x >= dock_left + 132) {
            if (selected_position) start_numeric_input(NumericFieldId::ObjectY);
            return true;
        }
        if (y >= 274 && y <= 302 && node_tool_->selected_node_count() > 0) {
            const int panel_left = dock_left + AlignPanelMetrics::content_left;
            const int button_width = (AlignPanelMetrics::content_width - 12) / 3;
            const int item = std::clamp((x - panel_left) / (button_width + 6), 0, 2);
            constexpr document::PathNodeMode modes[] = {
                document::PathNodeMode::Cusp,
                document::PathNodeMode::Smooth,
                document::PathNodeMode::Symmetric};
            if (x >= panel_left && x < panel_left + AlignPanelMetrics::content_width &&
                node_tool_->set_selected_mode(modes[item])) {
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Đã đổi mode cho node được chọn · Ctrl+Z hoàn tác"
                    : "Updated selected node mode · Ctrl+Z undoes";
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return true;
        }
    }
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
    constexpr int properties_top = 38 + 28 + 36 + 36;
    constexpr int panel_width = 280;
    const int panel_left = width_ - panel_width;
    if (active_dock_tab_ == 0 && x >= panel_left && x < width_ &&
        y >= properties_top && y < height_ - 26) {
        const auto target = contour_tool_->target_object();
        const auto snapshot = root().document().current_snapshot();
        const auto* object = snapshot ? snapshot->objects().lookup_ref(target) : nullptr;
        const auto* path = object ? std::get_if<document::PathObject>(object) : nullptr;
        const bool can_edit = path && !path->locked;
        const bool has_contour = can_edit &&
            document::get_object_contour(root().document(), target).has_value();
        const int local_y = y - properties_top;
        const int local_x = x - (panel_left + 14);
        const auto commit_active = [&]() {
            if (active_num_field_ == NumericFieldId::None) return true;
            commit_numeric_field();
            return active_num_field_ == NumericFieldId::None;
        };
        const auto edit_field = [&](int row, NumericFieldId field, bool enabled) {
            if (local_y < row || local_y >= row + 32) return false;
            if (enabled) start_numeric_input(field);
            else status_msg_ = language_ == UILanguage::Vietnamese
                ? "Trường này không áp dụng cho đối tượng hoặc kiểu góc đang chọn"
                : "This field is unavailable for the selected object or join type";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        };
        if (edit_field(104, NumericFieldId::DistributeSpacing, can_edit) ||
            edit_field(140, NumericFieldId::PolygonSides, can_edit) ||
            edit_field(228, NumericFieldId::PolygonRotation,
                       can_edit && contour_tool_->join_type() == geometry::JoinType::Miter))
            return true;

        const int triple_width = (252 - 12) / 3;
        const int action_width = (252 - 18) / 4;
        if (local_y >= 316 && local_y < 346 && local_x >= 0 && local_x < 252) {
            const int index = local_x / (action_width + 6);
            if (index < 4 && local_x % (action_width + 6) < action_width) {
                if (index == 3) {
                    if (!can_edit) {
                        status_msg_ = language_ == UILanguage::Vietnamese
                            ? "Chọn một Path không khóa để hủy bản nháp Contour"
                            : "Select an unlocked Path to cancel the Contour draft";
                    } else {
                        if (active_num_field_ != NumericFieldId::None) cancel_numeric_field();
                        contour_tool_->cancel();
                        if (!document::get_object_contour(root().document(), target)) {
                            contour_tool_->set_direction(geometry::ContourDirection::Outside);
                            contour_tool_->set_spacing_mm(1.0);
                            contour_tool_->set_steps(1);
                            contour_tool_->set_join_type(geometry::JoinType::Miter);
                            contour_tool_->set_miter_limit(4.0);
                        }
                        status_msg_ = language_ == UILanguage::Vietnamese
                            ? "Đã khôi phục thông số Contour đã lưu hoặc giá trị mặc định"
                            : "Restored saved Contour settings or defaults";
                    }
                    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
                    return true;
                }
                if (!commit_active()) return true;
                if (index == 0) {
                    status_msg_ = can_edit && contour_tool_->has_preview() && contour_tool_->apply()
                        ? (language_ == UILanguage::Vietnamese
                            ? "Đã áp dụng Contour · Ctrl+Z để hoàn tác"
                            : "Applied Contour · Ctrl+Z to undo")
                        : (language_ == UILanguage::Vietnamese
                            ? "Không thể áp dụng · cần Path không khóa có preview hợp lệ"
                            : "Could not apply · an unlocked Path with a valid preview is required");
                } else if (!has_contour) {
                    status_msg_ = language_ == UILanguage::Vietnamese
                        ? "Path này chưa có Contour đã lưu"
                        : "This Path has no saved Contour";
                } else if (index == 1 && contour_tool_->remove()) {
                    status_msg_ = language_ == UILanguage::Vietnamese
                        ? "Đã xóa Contour · Ctrl+Z để hoàn tác"
                        : "Removed Contour · Ctrl+Z to undo";
                } else if (index == 2 && contour_tool_->break_apart()) {
                    status_msg_ = language_ == UILanguage::Vietnamese
                        ? "Đã tách Contour thành các Path độc lập · Ctrl+Z để hoàn tác"
                        : "Broke Contour into independent Paths · Ctrl+Z to undo";
                } else {
                    status_msg_ = language_ == UILanguage::Vietnamese
                        ? "Không thể hoàn thành thao tác Contour"
                        : "Could not complete the Contour action";
                }
                if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            }
        }
        if (!commit_active()) return true;
        if (local_y >= 68 && local_y < 96 && local_x >= 0 && local_x < 252 && can_edit) {
            const int index = std::min(2, local_x / (triple_width + 6));
            constexpr geometry::ContourDirection directions[] = {
                geometry::ContourDirection::Outside, geometry::ContourDirection::Inside,
                geometry::ContourDirection::ToCenter};
            contour_tool_->set_direction(directions[index]);
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đã cập nhật hướng Contour · Apply để lưu"
                : "Updated Contour direction · Apply to save";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (local_y >= 196 && local_y < 224 && local_x >= 0 && local_x < 252 && can_edit) {
            const int index = std::min(2, local_x / (triple_width + 6));
            constexpr geometry::JoinType joins[] = {
                geometry::JoinType::Miter, geometry::JoinType::Round, geometry::JoinType::Bevel};
            contour_tool_->set_join_type(joins[index]);
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đã cập nhật góc nối Contour · Apply để lưu"
                : "Updated Contour join · Apply to save";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        return true; // Keep pointer input inside Properties away from the canvas.
    }

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
        contour_tool_->set_spacing_mm(std::min(100000.0, contour_tool_->spacing_mm() + 0.5));
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Khoảng cách viền: " + std::to_string(contour_tool_->spacing_mm()) + " mm"
            : "Contour spacing: " + std::to_string(contour_tool_->spacing_mm()) + " mm";
    } else if (x >= 445 && x < 470) {
        contour_tool_->set_steps(std::max(1, contour_tool_->steps() - 1));
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Số bước viền: " + std::to_string(contour_tool_->steps())
            : "Contour steps: " + std::to_string(contour_tool_->steps());
    } else if (x >= 540 && x < 565) {
        contour_tool_->set_steps(std::min(99, contour_tool_->steps() + 1));
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

bool NativeWindow::handle_gradient_pointer_down(int x, int y, unsigned modifiers) {
    if (active_tool_ != 13 || !gradient_tool_ || !gradient_tool_->active() || is_panning_)
        return false;
    if (active_num_field_ != NumericFieldId::None) {
        commit_numeric_field();
        if (active_num_field_ != NumericFieldId::None)
            return true;
    }
    if (gradient_tool_->presenter().is_dirty()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Bản nháp Gradient đang sửa · nhấn Apply hoặc Cancel trước khi kéo trên canvas"
            : "Gradient draft has edits · Apply or Cancel before dragging on the canvas";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
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
