#include "pikdraw/desktop/shell/native_window.hpp"
#include "../automation/native_window_automation.hpp"
#include "pikdraw/desktop/actions/action_id.hpp"
#include "native_window_menus.hpp"
#include "native_window_page_presets.hpp"
#include "native_canvas_frame.hpp"
#ifdef _WIN32
#include "../clipboard/native_clipboard.hpp"
#endif
#include "../tools/drawing_tool_controller.hpp"
#include "../tools/pen_tool_controller.hpp"
#include "../tools/node_tool_controller.hpp"
#include "../tools/contour_tool_controller.hpp"
#include "../tools/gradient_tool_controller.hpp"
#include "pikdraw/vector/tools/shape_tools.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pikdraw::desktop {

void NativeWindow::commit_numeric_field() {
    if (active_num_field_ == NumericFieldId::None) return;

    if (active_num_field_ == NumericFieldId::PenLength || active_num_field_ == NumericFieldId::PenAngle) {
        if (active_num_field_ == NumericFieldId::PenLength) {
            pen_numeric_length_ = num_input_buffer_;
        } else {
            pen_numeric_angle_ = num_input_buffer_;
        }
        update_pen_dimension_candidate();

        if (active_tool_ == 8 && pen_tool_ && pen_tool_->active()) {
            const auto placed = pen_tool_->confirm_candidate();
            if (placed.code != vector::tools::pen::PenDrawingResultCode::Applied &&
                placed.code != vector::tools::pen::PenDrawingResultCode::PreviewUpdated) {
                status_msg_ = placed.message;
                if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
            active_num_field_ = NumericFieldId::None;
            pen_numeric_length_.clear();
            pen_numeric_angle_.clear();
            num_input_buffer_.clear();
            (void)pen_tool_->pointer_move(pen_last_pointer_world_, false);
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đã tạo đoạn thẳng · Phím L nhập tiếp · Enter để kết thúc"
                : "Segment created · Press L for next · Enter to finish";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        } else if ((active_tool_ == 6 || active_tool_ == 7) && drawing_tool_ && drawing_tool_->active()) {
            const auto result = drawing_tool_->confirm_candidate();
            if (result.committed() && result.object_id) {
                const auto snapshot = root().document().current_snapshot();
                if (snapshot)
                    root().vector_controller().set_selection({*result.object_id}, *snapshot);
            }
            active_num_field_ = NumericFieldId::None;
            pen_numeric_length_.clear();
            pen_numeric_angle_.clear();
            num_input_buffer_.clear();
            status_msg_ = drawing_tool_->visual_state().message;
            if (!drawing_tool_->active()) finish_drawing_tool(true);
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
    }

    if (active_num_field_ == NumericFieldId::PenExpression) {
        if (!pen_tool_ || !pen_tool_->active() || num_input_buffer_.empty()) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Nhập @L<góc, @ΔX;ΔY hoặc #X;Y trước khi đặt điểm"
                : "Enter @L<angle, @dX;dY or #X;Y before placing a point";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        const auto preview = pen_tool_->update_numeric_candidate(num_input_buffer_);
        if (preview.code == vector::tools::pen::PenDrawingResultCode::InvalidInput ||
            preview.code == vector::tools::pen::PenDrawingResultCode::Stale ||
            preview.code == vector::tools::pen::PenDrawingResultCode::BudgetExceeded) {
            status_msg_ = preview.message;
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        const auto placed = pen_tool_->confirm_candidate();
        if (placed.code != vector::tools::pen::PenDrawingResultCode::Applied &&
            placed.code != vector::tools::pen::PenDrawingResultCode::PreviewUpdated) {
            status_msg_ = placed.message;
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        active_num_field_ = NumericFieldId::None;
        pen_numeric_buffer_.clear();
        num_input_buffer_.clear();
        (void)pen_tool_->pointer_move(pen_last_pointer_world_, false);
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã đặt điểm theo kích thước · Enter để kết thúc"
            : "Point placed from dimensions · Enter finishes";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (active_num_field_ == NumericFieldId::PolygonSides) {
        try {
            size_t parsed = 0;
            const int value = std::stoi(num_input_buffer_, &parsed);
            if (parsed != num_input_buffer_.size() ||
                value < vector::ShapeFactory::kMinimumPolygonSides ||
                value > vector::ShapeFactory::kMaximumPolygonSides) {
                throw std::out_of_range("polygon sides");
            }
            polygon_sides_ = value;
        } catch (...) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Số cạnh phải là số nguyên từ 3 đến 256"
                : "Sides must be a whole number from 3 to 256";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã cập nhật số cạnh đa giác · chỉ áp dụng cho hình tạo tiếp theo"
            : "Polygon sides updated · applies to the next shape";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (active_num_field_ == NumericFieldId::PolygonRotation) {
        try {
            size_t parsed = 0;
            const double value = std::stod(num_input_buffer_, &parsed);
            if (parsed != num_input_buffer_.size() || !std::isfinite(value) ||
                value < -36000.0 || value > 36000.0) {
                throw std::out_of_range("polygon rotation");
            }
            polygon_rotation_degrees_ = value;
        } catch (...) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Góc xoay phải nằm trong khoảng −36000° đến 36000°"
                : "Rotation must be between −36000° and 36000°";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã cập nhật góc xoay đa giác · chỉ áp dụng cho hình tạo tiếp theo"
            : "Polygon rotation updated · applies to the next shape";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (active_num_field_ == NumericFieldId::DistributeSpacing) {
        try {
            std::string normalized = num_input_buffer_;
            std::replace(normalized.begin(), normalized.end(), ',', '.');
            const double value = std::stod(normalized);
            if (!std::isfinite(value) || value < 0.0 || value > geometry::kMaxCanvasDimensionMm) {
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Khoảng hở phải từ 0 đến giới hạn kích thước bản vẽ (mm)"
                    : "Gap must be between 0 and the maximum canvas dimension (mm)";
                if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
            distribute_spacing_mm_ = value;
        } catch (...) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Nhập khoảng hở hợp lệ theo mm"
                : "Enter a valid gap in mm";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã cập nhật khoảng hở cố định (mm)"
            : "Updated fixed gap (mm)";
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
        return;
    }

    if (active_num_field_ == NumericFieldId::EffectSigmaX ||
        active_num_field_ == NumericFieldId::EffectSigmaY ||
        active_num_field_ == NumericFieldId::EffectOffsetX ||
        active_num_field_ == NumericFieldId::EffectOffsetY ||
        active_num_field_ == NumericFieldId::EffectOpacity) {
        const auto field = active_num_field_;
        try {
            std::string normalized = num_input_buffer_;
            std::replace(normalized.begin(), normalized.end(), ',', '.');
            size_t parsed = 0;
            const double value = std::stod(normalized, &parsed);
            if (parsed != normalized.size() || !std::isfinite(value))
                throw std::out_of_range("appearance parameter");

            const bool opacity = field == NumericFieldId::EffectOpacity;
            const bool offset = field == NumericFieldId::EffectOffsetX ||
                                field == NumericFieldId::EffectOffsetY;
            const bool in_range = opacity ? value >= 0.0 && value <= 100.0
                : offset ? std::abs(value) <= geometry::limits::kCanvasMaxMm
                         : value >= 0.0 && value <= geometry::limits::kCanvasMaxMm;
            if (!in_range)
                throw std::out_of_range("appearance parameter range");

            const auto &current_targets = root().vector_controller().selection().selected_ids();
            bool applied = current_targets == numeric_effect_targets_ &&
                workflow().can_edit_selected_effect(numeric_effect_index_);
            if (applied) {
                if (field == NumericFieldId::EffectSigmaX)
                    applied = workflow().set_selected_effect_sigma(numeric_effect_index_, true, value);
                else if (field == NumericFieldId::EffectSigmaY)
                    applied = workflow().set_selected_effect_sigma(numeric_effect_index_, false, value);
                else if (field == NumericFieldId::EffectOffsetX)
                    applied = workflow().set_selected_effect_offset(numeric_effect_index_, true, value);
                else if (field == NumericFieldId::EffectOffsetY)
                    applied = workflow().set_selected_effect_offset(numeric_effect_index_, false, value);
                else
                    applied = workflow().set_selected_effect_opacity(numeric_effect_index_, value / 100.0);
            }
            if (!applied) {
                active_num_field_ = NumericFieldId::None;
                num_input_buffer_.clear();
                numeric_effect_targets_.clear();
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Vùng chọn hoặc thứ tự hiệu ứng đã đổi · thông số chưa áp dụng"
                    : "Selection or effect order changed · parameter was not applied";
                if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
        } catch (...) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Giá trị không hợp lệ · Sigma: 0–1.000.000 mm; lệch: ±1.000.000 mm; Opacity: 0–100%"
                : "Invalid value · Sigma: 0–1,000,000 mm; offset: ±1,000,000 mm; opacity: 0–100%";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
        numeric_effect_targets_.clear();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã cập nhật thông số Appearance · Ctrl+Z để hoàn tác"
            : "Updated Appearance parameter · Ctrl+Z to undo";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (active_num_field_ == NumericFieldId::ObjectOpacity) {
        std::string normalized = num_input_buffer_;
        std::replace(normalized.begin(), normalized.end(), ',', '.');
        try {
            size_t parsed = 0;
            const double percent = std::stod(normalized, &parsed);
            if (parsed != normalized.size() || !std::isfinite(percent) ||
                percent < 0.0 || percent > 100.0) {
                throw std::out_of_range("object opacity percent");
            }
            const auto& current_targets = root().vector_controller().selection().selected_ids();
            if (current_targets != numeric_object_opacity_targets_ ||
                !workflow().set_selected_object_opacity(percent / 100.0)) {
                active_num_field_ = NumericFieldId::None;
                num_input_buffer_.clear();
                numeric_object_opacity_targets_.clear();
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Vùng chọn đã đổi hoặc không còn sửa được · Opacity chưa áp dụng"
                    : "Selection changed or is no longer editable · opacity was not applied";
                if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
        } catch (...) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Opacity phải là số hữu hạn từ 0 đến 100% · giá trị chưa được áp dụng"
                : "Opacity must be a finite number from 0 to 100% · value was not applied";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
        numeric_object_opacity_targets_.clear();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã cập nhật Opacity cho toàn bộ vùng chọn · Ctrl+Z để hoàn tác"
            : "Updated opacity for the entire selection · Ctrl+Z to undo";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (active_num_field_ == NumericFieldId::FillOpacity ||
        active_num_field_ == NumericFieldId::StrokeOpacity) {
        const auto field = active_num_field_;
        std::string normalized = num_input_buffer_;
        std::replace(normalized.begin(), normalized.end(), ',', '.');
        try {
            size_t parsed = 0;
            const double percent = std::stod(normalized, &parsed);
            if (parsed != normalized.size() || !std::isfinite(percent) ||
                percent < 0.0 || percent > 100.0)
                throw std::out_of_range("paint opacity percent");
            const auto &current_targets = root().vector_controller().selection().selected_ids();
            const bool applied = current_targets == numeric_paint_opacity_targets_ &&
                (field == NumericFieldId::FillOpacity
                    ? workflow().set_selected_fill_opacity(percent / 100.0)
                    : workflow().set_selected_stroke_opacity(percent / 100.0));
            if (!applied) {
                active_num_field_ = NumericFieldId::None;
                num_input_buffer_.clear();
                numeric_paint_opacity_targets_.clear();
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Vùng chọn đã đổi hoặc không còn hỗ trợ · độ mờ chưa áp dụng"
                    : "Selection changed or no longer supports this paint · opacity was not applied";
                if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
        } catch (...) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Độ mờ phải là số hữu hạn từ 0 đến 100% · giá trị chưa được áp dụng"
                : "Opacity must be a finite number from 0 to 100% · value was not applied";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
        numeric_paint_opacity_targets_.clear();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? (field == NumericFieldId::FillOpacity
                ? "Đã cập nhật độ mờ màu tô cho toàn bộ vùng chọn · Ctrl+Z hoàn tác"
                : "Đã cập nhật độ mờ viền cho toàn bộ vùng chọn · Ctrl+Z hoàn tác")
            : (field == NumericFieldId::FillOpacity
                ? "Updated fill opacity for the complete selection · Ctrl+Z undoes"
                : "Updated stroke opacity for the complete selection · Ctrl+Z undoes");
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (!num_input_buffer_.empty()) {
        try {
            double val = std::stod(num_input_buffer_);
            const auto field = active_num_field_;

            if (field == NumericFieldId::PageWidth) {
                set_active_page_size(val, active_tab().page_height_mm);
            } else if (field == NumericFieldId::PageHeight) {
                set_active_page_size(active_tab().page_width_mm, val);
            } else if (field == NumericFieldId::ModalWidth) {
                new_doc_w_ = std::clamp(val, 10.0, geometry::kMaxObjectDimensionMm);
            } else if (field == NumericFieldId::ModalHeight) {
                new_doc_h_ = std::clamp(val, 10.0, geometry::kMaxObjectDimensionMm);
            } else {
                const auto ids = root().vector_controller().selection().selected_ids();
                if (!ids.empty()) {
                    const auto id = ids.front();
                    const auto snapshot = root().document().current_snapshot();
                    const auto obj = snapshot->objects().get(id);
                    if (obj) {
                        const auto bounds_opt = std::visit([](const auto& val) -> std::optional<geometry::Rect2D> {
                            using T = std::decay_t<decltype(val)>;
                            if constexpr (std::is_same_v<T, document::GroupObject>) return std::nullopt;
                            else return val.bounds();
                        }, *obj);
                        if (bounds_opt) {
                            if (field == NumericFieldId::ObjectX) {
                                double target_x = std::clamp(val, -geometry::kMaxCanvasDimensionMm * 0.5, geometry::kMaxCanvasDimensionMm * 0.5);
                                move_selected_object(target_x - bounds_opt->x, 0.0);
                            } else if (field == NumericFieldId::ObjectY) {
                                double target_y = std::clamp(val, -geometry::kMaxCanvasDimensionMm * 0.5, geometry::kMaxCanvasDimensionMm * 0.5);
                                move_selected_object(0.0, target_y - bounds_opt->y);
                            } else if (field == NumericFieldId::ObjectWidth) {
                                double new_w = std::clamp(val, 1.0, geometry::kMaxObjectDimensionMm);
                                double new_h = bounds_opt->height;
                                if (aspect_ratio_locked_ && bounds_opt->width > 0.001) {
                                    new_h = std::clamp(bounds_opt->height * (new_w / bounds_opt->width), 1.0, geometry::kMaxObjectDimensionMm);
                                }
                                workflow().resize_object(id, new_w, new_h);
                            } else if (field == NumericFieldId::ObjectHeight) {
                                double new_h = std::clamp(val, 1.0, geometry::kMaxObjectDimensionMm);
                                double new_w = bounds_opt->width;
                                if (aspect_ratio_locked_ && bounds_opt->height > 0.001) {
                                    new_w = std::clamp(bounds_opt->width * (new_h / bounds_opt->height), 1.0, geometry::kMaxObjectDimensionMm);
                                }
                                workflow().resize_object(id, new_w, new_h);
                            }
                        }
                    }
                }
            }
        } catch (...) {
            // Ignore format errors
        }
    }

    active_num_field_ = NumericFieldId::None;
    num_input_buffer_.clear();

#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}


} // namespace pikdraw::desktop
