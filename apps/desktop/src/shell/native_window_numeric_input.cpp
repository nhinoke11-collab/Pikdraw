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

void NativeWindow::start_numeric_input(NumericFieldId field) {
    if (active_num_field_ == field)
        return;
    if (active_num_field_ != NumericFieldId::None && active_num_field_ != field) {
        commit_numeric_field();
        if (active_num_field_ != NumericFieldId::None)
            return;
    }
    if (field == NumericFieldId::ObjectOpacity && !workflow().can_set_selected_object_opacity()) {
        const auto snapshot = root().document().current_snapshot();
        const auto appearance = snapshot
            ? selection_appearance(*snapshot, root().vector_controller().selection().selected_ids())
            : SelectionAppearance{};
        status_msg_ = language_ == UILanguage::Vietnamese
            ? (appearance.selected_count == 0
                ? "Chọn Path hoặc Text để sửa độ mờ"
                : appearance.contains_locked
                    ? "Không thể sửa Opacity: vùng chọn có đối tượng bị khóa"
                    : "Không thể sửa Opacity: chỉ hỗ trợ Path/Text trong toàn bộ vùng chọn")
            : (appearance.selected_count == 0
                ? "Select a Path or Text object to edit opacity"
                : appearance.contains_locked
                    ? "Opacity is unavailable because the selection contains a locked object"
                    : "Opacity requires the entire selection to contain editable Path/Text objects");
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
        return;
    }
    if ((field == NumericFieldId::FillOpacity &&
         !workflow().can_set_selected_fill_opacity()) ||
        (field == NumericFieldId::StrokeOpacity &&
         !workflow().can_set_selected_stroke_opacity())) {
        const auto snapshot = root().document().current_snapshot();
        const auto summary = snapshot
            ? selection_appearance(*snapshot, root().vector_controller().selection().selected_ids())
            : SelectionAppearance{};
        const bool stroke = field == NumericFieldId::StrokeOpacity;
        status_msg_ = language_ == UILanguage::Vietnamese
            ? (summary.selected_count == 0
                ? "Chọn đối tượng để sửa độ mờ màu tô hoặc viền"
                : summary.contains_locked
                    ? "Vùng chọn có đối tượng khóa · không cập nhật một phần"
                    : stroke
                        ? "Độ mờ viền chỉ hỗ trợ Path không khóa; Text không có viền được render"
                        : "Độ mờ màu tô chỉ hỗ trợ Path/Text không khóa")
            : (summary.selected_count == 0
                ? "Select an object to edit fill or stroke opacity"
                : summary.contains_locked
                    ? "Selection contains a locked object · partial updates are disabled"
                    : stroke
                        ? "Stroke opacity supports unlocked Paths only; Text stroke is not rendered"
                        : "Fill opacity supports unlocked Path/Text objects only");
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
        return;
    }
    const bool effect_numeric = field == NumericFieldId::EffectSigmaX ||
        field == NumericFieldId::EffectSigmaY || field == NumericFieldId::EffectOffsetX ||
        field == NumericFieldId::EffectOffsetY || field == NumericFieldId::EffectOpacity;
    if (effect_numeric) {
        const auto snapshot = root().document().current_snapshot();
        const auto selection = root().vector_controller().selection().selected_ids();
        const auto appearance = snapshot
            ? selection_appearance(*snapshot, selection) : SelectionAppearance{};
        const bool shadow_field = field == NumericFieldId::EffectOffsetX ||
            field == NumericFieldId::EffectOffsetY || field == NumericFieldId::EffectOpacity;
        const auto *shadow = appearance.can_edit_effect(property_effect_index_)
            ? std::get_if<document::DropShadowEffect>(
                &appearance.effects[property_effect_index_].parameters) : nullptr;
        if (!workflow().can_edit_selected_effect(property_effect_index_) ||
            (shadow_field && !shadow)) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Hiệu ứng đã đổi hoặc vùng chọn không đồng nhất · chưa áp dụng"
                : "Effect stack changed or selection is mixed · no value was applied";
#ifdef _WIN32
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
            return;
        }
    }
    active_num_field_ = field;
    num_input_buffer_.clear();
    numeric_object_opacity_targets_.clear();
    numeric_paint_opacity_targets_.clear();
    numeric_effect_targets_.clear();

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);

    if (field == NumericFieldId::PageWidth) {
        ss << active_tab().page_width_mm;
        num_input_buffer_ = ss.str();
    } else if (field == NumericFieldId::PageHeight) {
        ss << active_tab().page_height_mm;
        num_input_buffer_ = ss.str();
    } else if (field == NumericFieldId::PolygonSides) {
        num_input_buffer_ = std::to_string(polygon_sides_);
    } else if (field == NumericFieldId::PolygonRotation) {
        ss << polygon_rotation_degrees_;
        num_input_buffer_ = ss.str();
    } else if (field == NumericFieldId::ModalWidth) {
        ss << new_doc_w_;
        num_input_buffer_ = ss.str();
    } else if (field == NumericFieldId::ModalHeight) {
        ss << new_doc_h_;
        num_input_buffer_ = ss.str();
    } else if (field == NumericFieldId::DistributeSpacing) {
        ss << distribute_spacing_mm_;
        num_input_buffer_ = ss.str();
    } else if (field == NumericFieldId::PenExpression) {
        num_input_buffer_ = pen_numeric_buffer_;
    } else if (field == NumericFieldId::PenLength) {
        num_input_buffer_ = pen_numeric_length_;
    } else if (field == NumericFieldId::PenAngle) {
        num_input_buffer_ = pen_numeric_angle_;
    } else if (field == NumericFieldId::ObjectOpacity) {
        numeric_object_opacity_targets_ =
            root().vector_controller().selection().selected_ids();
    } else if (field == NumericFieldId::FillOpacity ||
               field == NumericFieldId::StrokeOpacity) {
        numeric_paint_opacity_targets_ =
            root().vector_controller().selection().selected_ids();
    } else if (effect_numeric) {
        numeric_effect_index_ = property_effect_index_;
        numeric_effect_targets_ = root().vector_controller().selection().selected_ids();
        const auto snapshot = root().document().current_snapshot();
        const auto summary = selection_appearance(
            *snapshot, root().vector_controller().selection().selected_ids());
        const auto &effect = summary.effects[numeric_effect_index_];
        const bool is_blur = std::holds_alternative<document::GaussianBlurEffect>(effect.parameters);
        bool mixed = false;
        double value = 0.0;
        if (field == NumericFieldId::EffectSigmaX) {
            mixed = effect.mixed_sigma_x;
            value = is_blur ? std::get<document::GaussianBlurEffect>(effect.parameters).sigma_x_mm
                            : std::get<document::DropShadowEffect>(effect.parameters).sigma_x_mm;
        } else if (field == NumericFieldId::EffectSigmaY) {
            mixed = effect.mixed_sigma_y;
            value = is_blur ? std::get<document::GaussianBlurEffect>(effect.parameters).sigma_y_mm
                            : std::get<document::DropShadowEffect>(effect.parameters).sigma_y_mm;
        } else if (field == NumericFieldId::EffectOffsetX) {
            const auto &shadow = std::get<document::DropShadowEffect>(effect.parameters);
            mixed = effect.mixed_offset_x;
            value = shadow.dx_mm;
        } else if (field == NumericFieldId::EffectOffsetY) {
            const auto &shadow = std::get<document::DropShadowEffect>(effect.parameters);
            mixed = effect.mixed_offset_y;
            value = shadow.dy_mm;
        } else {
            const auto &shadow = std::get<document::DropShadowEffect>(effect.parameters);
            mixed = effect.mixed_opacity;
            value = shadow.opacity * 100.0;
        }
        if (!mixed) {
            std::ostringstream effect_value;
            effect_value << std::setprecision(10) << value;
            num_input_buffer_ = effect_value.str();
        }
    } else {
        const auto ids = root().vector_controller().selection().selected_ids();
        if (!ids.empty()) {
            const auto snapshot = root().document().current_snapshot();
            const auto obj = snapshot->objects().get(ids.front());
            if (obj) {
                const auto bounds_opt = std::visit([](const auto& val) -> std::optional<geometry::Rect2D> {
                    using T = std::decay_t<decltype(val)>;
                    if constexpr (std::is_same_v<T, document::GroupObject>) return std::nullopt;
                    else return val.bounds();
                }, *obj);
                if (bounds_opt) {
                    if (field == NumericFieldId::ObjectX) ss << bounds_opt->x;
                    else if (field == NumericFieldId::ObjectY) ss << bounds_opt->y;
                    else if (field == NumericFieldId::ObjectWidth) ss << bounds_opt->width;
                    else if (field == NumericFieldId::ObjectHeight) ss << bounds_opt->height;
                    num_input_buffer_ = ss.str();
                }
            }
        }
    }

    if (num_input_buffer_.size() > 2 && num_input_buffer_.substr(num_input_buffer_.size() - 2) == ".0") {
        num_input_buffer_.erase(num_input_buffer_.size() - 2);
    }

    if (field == NumericFieldId::ObjectOpacity) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Nhập Opacity 0–100% (dấu phẩy hoặc chấm) · Enter áp dụng · Esc hủy · Ctrl+Z hoàn tác"
            : "Enter Opacity from 0–100% · comma or dot decimals · Enter applies · Esc cancels · Ctrl+Z undoes";
    } else if (field == NumericFieldId::PenLength) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Nhập chiều dài đoạn thẳng (mm) · Enter tạo đoạn · Tab nhập góc (°)"
            : "Enter line length (mm) · Enter creates line · Tab sets angle (°)";
    } else if (field == NumericFieldId::PenAngle) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Nhập góc theo trục X (°) · Enter tạo đoạn · Tab nhập chiều dài"
            : "Enter angle from X axis (°) · Enter creates line · Tab sets length";
    } else if (field == NumericFieldId::FillOpacity ||
               field == NumericFieldId::StrokeOpacity) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Độ mờ 0–100% · Enter áp dụng · Esc hủy · Ctrl+Z hoàn tác"
            : "Opacity 0–100% · Enter applies · Esc cancels · Ctrl+Z undoes";
    } else if (effect_numeric) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Nhập mm (σ ≥ 0, lệch ±1.000.000) hoặc Opacity 0–100% · Enter áp dụng · Tab ô kế · Esc hủy · Ctrl+Z hoàn tác"
            : "Enter mm values (σ ≥ 0, offset ±1,000,000) or Opacity 0–100% · Enter applies · Tab next field · Esc cancels · Ctrl+Z undoes";
    } else {
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đang nhập số: gõ phím số, Enter để áp dụng, Tab chuyển ô, Esc để hủy"
            : "Numeric input: type value, Enter to apply, Tab to next field, Esc to cancel";
    }

#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::start_effect_numeric_input(NumericFieldId field, size_t effect_index) {
    if (active_num_field_ == field && numeric_effect_index_ == effect_index)
        return;
    if (active_num_field_ != NumericFieldId::None) {
        commit_numeric_field();
        if (active_num_field_ != NumericFieldId::None)
            return;
    }
    property_effect_index_ = effect_index;
    start_numeric_input(field);
}

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

void NativeWindow::cancel_numeric_field() {
    const bool cancel_pen_expression = active_num_field_ == NumericFieldId::PenExpression;
    const bool cancel_pen_dimension = (active_num_field_ == NumericFieldId::PenLength ||
                                       active_num_field_ == NumericFieldId::PenAngle);
    active_num_field_ = NumericFieldId::None;
    num_input_buffer_.clear();
    numeric_object_opacity_targets_.clear();
    numeric_paint_opacity_targets_.clear();
    numeric_effect_targets_.clear();
    if (cancel_pen_expression) {
        pen_numeric_buffer_.clear();
        if (pen_tool_ && pen_tool_->active())
            (void)pen_tool_->pointer_move(pen_last_pointer_world_, false);
    }
    if (cancel_pen_dimension) {
        pen_numeric_length_.clear();
        pen_numeric_angle_.clear();
        if (pen_tool_ && pen_tool_->active())
            (void)pen_tool_->pointer_move(pen_last_pointer_world_, false);
        else if ((active_tool_ == 6 || active_tool_ == 7) && drawing_tool_ && drawing_tool_->active())
            drawing_tool_->pointer_move(pen_last_pointer_world_, root().renderer().camera(),
                window_dpi_scale(), false, false);
    }
    status_msg_ = (language_ == UILanguage::Vietnamese) ? "Đã hủy nhập số" : "Cancelled numeric input";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::handle_numeric_char_input(wchar_t ch) {
    if (active_num_field_ == NumericFieldId::None) return;

    if (active_num_field_ == NumericFieldId::PenLength || active_num_field_ == NumericFieldId::PenAngle) {
        if (ch == 8) {
            if (!num_input_buffer_.empty()) num_input_buffer_.pop_back();
        } else if (ch == 13) {
            commit_numeric_field();
            return;
        } else if (ch == 27) {
            cancel_numeric_field();
            return;
        } else if (ch == 9) { // Tab: toggle between Length and Angle
            if (active_num_field_ == NumericFieldId::PenLength) {
                pen_numeric_length_ = num_input_buffer_;
                active_num_field_ = NumericFieldId::PenAngle;
                num_input_buffer_ = pen_numeric_angle_;
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Nhập góc theo trục X (°) · Enter tạo đoạn · Tab nhập chiều dài"
                    : "Enter angle from X axis (°) · Enter creates line · Tab sets length";
            } else {
                pen_numeric_angle_ = num_input_buffer_;
                active_num_field_ = NumericFieldId::PenLength;
                num_input_buffer_ = pen_numeric_length_;
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Nhập chiều dài đoạn thẳng (mm) · Enter tạo đoạn · Tab nhập góc (°)"
                    : "Enter line length (mm) · Enter creates line · Tab sets angle (°)";
            }
            update_pen_dimension_candidate();
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        } else if ((ch >= L'0' && ch <= L'9') || ch == L'.' || ch == L',' ||
                   (active_num_field_ == NumericFieldId::PenAngle && ch == L'-')) {
            if ((ch == L'.' || ch == L',') &&
                (num_input_buffer_.find('.') != std::string::npos || num_input_buffer_.find(',') != std::string::npos)) {
                return;
            }
            if (ch == L'-' && !num_input_buffer_.empty()) {
                return;
            }
            if (num_input_buffer_.size() < 12) {
                num_input_buffer_.push_back(static_cast<char>(ch));
            }
        }
        if (active_num_field_ == NumericFieldId::PenLength) {
            pen_numeric_length_ = num_input_buffer_;
        } else {
            pen_numeric_angle_ = num_input_buffer_;
        }
        update_pen_dimension_candidate();
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (active_num_field_ == NumericFieldId::PenExpression) {
        if (ch == 8) {
            if (!num_input_buffer_.empty()) num_input_buffer_.pop_back();
        } else if (ch == 13 || ch == 9) {
            commit_numeric_field();
            return;
        } else if (ch == 27) {
            cancel_numeric_field();
            return;
        } else if (ch >= 0x20 && ch <= 0x7e && num_input_buffer_.size() < 256) {
            num_input_buffer_.push_back(static_cast<char>(ch));
        }
        pen_numeric_buffer_ = num_input_buffer_;
        if (pen_tool_ && pen_tool_->active() && !pen_numeric_buffer_.empty()) {
            const auto result = pen_tool_->update_numeric_candidate(pen_numeric_buffer_);
            if (result.code == vector::tools::pen::PenDrawingResultCode::InvalidInput)
                status_msg_ = result.message;
            else
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Xem trước số · Enter đặt điểm · Esc khôi phục"
                    : "Numeric preview · Enter places point · Esc restores";
        }
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
        return;
    }

    if (ch == 8) { // Backspace
        if (!num_input_buffer_.empty()) {
            num_input_buffer_.pop_back();
        }
    } else if (ch == 13) { // Enter
        commit_numeric_field();
        return;
    } else if (ch == 27) { // Escape
        cancel_numeric_field();
        return;
    } else if (ch == 9) { // Tab: cycle to next field
        NumericFieldId next = NumericFieldId::None;
        switch (active_num_field_) {
            case NumericFieldId::PageWidth: next = NumericFieldId::PageHeight; break;
            case NumericFieldId::PageHeight: next = NumericFieldId::PageWidth; break;
            case NumericFieldId::PolygonSides: next = NumericFieldId::PolygonRotation; break;
            case NumericFieldId::PolygonRotation: next = NumericFieldId::PolygonSides; break;
            case NumericFieldId::ModalWidth: next = NumericFieldId::ModalHeight; break;
            case NumericFieldId::ModalHeight: next = NumericFieldId::ModalWidth; break;
            case NumericFieldId::ObjectX: next = NumericFieldId::ObjectY; break;
            case NumericFieldId::ObjectY: next = NumericFieldId::ObjectWidth; break;
            case NumericFieldId::ObjectWidth: next = NumericFieldId::ObjectHeight; break;
            case NumericFieldId::ObjectHeight: next = NumericFieldId::ObjectX; break;
            case NumericFieldId::EffectOffsetX: next = NumericFieldId::EffectOffsetY; break;
            case NumericFieldId::EffectOffsetY: next = NumericFieldId::EffectSigmaX; break;
            case NumericFieldId::EffectSigmaX: next = NumericFieldId::EffectSigmaY; break;
            case NumericFieldId::EffectSigmaY: {
                const auto snapshot = root().document().current_snapshot();
                const auto appearance = selection_appearance(
                    *snapshot, root().vector_controller().selection().selected_ids());
                const bool shadow = appearance.can_edit_effect(numeric_effect_index_) &&
                    std::holds_alternative<document::DropShadowEffect>(
                        appearance.effects[numeric_effect_index_].parameters);
                next = shadow ? NumericFieldId::EffectOpacity : NumericFieldId::EffectSigmaX;
                break;
            }
            case NumericFieldId::EffectOpacity: next = NumericFieldId::EffectOffsetX; break;
            default: break;
        }
        commit_numeric_field();
        if (active_num_field_ != NumericFieldId::None)
            return;
        if (next != NumericFieldId::None) {
            start_numeric_input(next);
        }
        return;
    } else if ((ch >= L'0' && ch <= L'9') ||
               (active_num_field_ != NumericFieldId::PolygonSides && ch == L'.') ||
               (ch == L',' && active_num_field_ == NumericFieldId::DistributeSpacing) ||
               (ch == L',' && active_num_field_ == NumericFieldId::ObjectOpacity) ||
               (ch == L',' && (active_num_field_ == NumericFieldId::FillOpacity ||
                               active_num_field_ == NumericFieldId::StrokeOpacity)) ||
               (ch == L',' && (active_num_field_ == NumericFieldId::EffectSigmaX ||
                               active_num_field_ == NumericFieldId::EffectSigmaY ||
                               active_num_field_ == NumericFieldId::EffectOffsetX ||
                               active_num_field_ == NumericFieldId::EffectOffsetY ||
                               active_num_field_ == NumericFieldId::EffectOpacity)) ||
               (active_num_field_ != NumericFieldId::PolygonSides && ch == L'-')) {
        if ((ch == L'.' || ch == L',') &&
            (num_input_buffer_.find('.') != std::string::npos || num_input_buffer_.find(',') != std::string::npos)) {
            return;
        }
        if (ch == L'-' && !num_input_buffer_.empty()) {
            return;
        }
        if (num_input_buffer_.size() < 12) {
            num_input_buffer_.push_back(static_cast<char>(ch));
        }
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

#ifdef _WIN32
void NativeWindow::capture_screenshot_bmp(const std::string& path) {
    int w = width_, h = height_;
    HDC screen_dc = GetDC(NULL);
    HDC mem_dc = CreateCompatibleDC(screen_dc);
    HBITMAP mem_bmp = CreateCompatibleBitmap(screen_dc, w, h);
    HGDIOBJ old_bmp = SelectObject(mem_dc, mem_bmp);

    paint(mem_dc, w, h);

    BITMAPINFOHEADER bi = { sizeof(BITMAPINFOHEADER), w, h, 1, 24, BI_RGB, 0, 0, 0, 0, 0 };
    int row_stride = ((w * 3 + 3) / 4) * 4;
    int data_size = row_stride * h;
    bi.biSizeImage = data_size;

    std::vector<uint8_t> pixel_data(data_size);
    GetDIBits(mem_dc, mem_bmp, 0, h, pixel_data.data(), reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);

    BITMAPFILEHEADER bfh = { 0x4D42, static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + data_size), 0, 0, sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) };

    std::ofstream out(path, std::ios::binary);
    if (out.is_open()) {
        out.write(reinterpret_cast<const char*>(&bfh), sizeof(bfh));
        out.write(reinterpret_cast<const char*>(&bi), sizeof(bi));
        out.write(reinterpret_cast<const char*>(pixel_data.data()), data_size);
    }

    SelectObject(mem_dc, old_bmp);
    DeleteObject(mem_bmp);
    DeleteDC(mem_dc);
    ReleaseDC(NULL, screen_dc);
}
#else
void NativeWindow::capture_screenshot_bmp(const std::string&) {}
#endif

void NativeWindow::handle_array_char(char c) {
    if (!workflow().is_array_session_active()) return;
    if (array_input_buffer_.size() < 16) {
        array_input_buffer_ += c;
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Mảng sao chép: " + array_input_buffer_ + " (Nhấn Enter áp dụng, Esc kết thúc)"
            : "Array: " + array_input_buffer_ + " (Press Enter to apply, Esc to finish)";
    }
}

void NativeWindow::handle_array_backspace() {
    if (!array_input_buffer_.empty()) {
        array_input_buffer_.pop_back();
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Mảng sao chép: " + array_input_buffer_ + " (Nhấn Enter áp dụng, Esc kết thúc)"
            : "Array: " + array_input_buffer_ + " (Press Enter to apply, Esc to finish)";
    }
}

void NativeWindow::commit_array_input() {
    if (!workflow().is_array_session_active() || array_input_buffer_.empty()) return;
    std::string buf = array_input_buffer_;
    array_input_buffer_.clear();

    bool is_div = false;
    int count = 0;
    if (buf[0] == '/') {
        is_div = true;
        try { count = std::stoi(buf.substr(1)); } catch (...) { return; }
    } else if (buf[0] == '*' || buf[0] == 'x' || buf[0] == 'X') {
        is_div = false;
        try { count = std::stoi(buf.substr(1)); } catch (...) { return; }
    } else {
        try { count = std::stoi(buf); } catch (...) { return; }
    }

    if (count <= 0) return;

    if (is_div) {
        if (workflow().apply_array_divider(count)) {
            status_msg_ = (language_ == UILanguage::Vietnamese)
                ? "Đã chia đều " + std::to_string(count) + " khoảng (/" + std::to_string(count) + ") · Gõ tiếp để đổi hoặc Esc để chốt"
                : "Divided by /" + std::to_string(count) + " · Type to change or Esc to finish";
        }
    } else {
        if (workflow().apply_array_multiplier(count)) {
            status_msg_ = (language_ == UILanguage::Vietnamese)
                ? "Đã nhân " + std::to_string(count) + " bản sao (*" + std::to_string(count) + ") · Gõ tiếp để đổi hoặc Esc để chốt"
                : "Multiplied by *" + std::to_string(count) + " · Type to change or Esc to finish";
        }
    }
}

void NativeWindow::end_quick_dimension() noexcept {
    quick_dimension_active_ = false;
    quick_dimension_object_id_ = document::kInvalidObjectId;
    quick_dimension_buffer_.clear();
}

bool NativeWindow::apply_quick_dimension() {
    if (!quick_dimension_active_ || quick_dimension_object_id_ == document::kInvalidObjectId) {
        return false;
    }
    const auto snapshot = root().document().current_snapshot();
    const auto obj = snapshot->objects().get(quick_dimension_object_id_);
    if (!obj) {
        end_quick_dimension();
        return false;
    }
    const auto bounds_opt = workflow().get_object_bounds(quick_dimension_object_id_);
    if (!bounds_opt || bounds_opt->width <= 0.0 || bounds_opt->height <= 0.0) {
        end_quick_dimension();
        return false;
    }

    double new_w = bounds_opt->width;
    double new_h = bounds_opt->height;

    // Phân tích chuỗi quick_dimension_buffer_
    // Định dạng: "30,40" hoặc "30, 40" hoặc "30;40" hoặc "30"
    std::string s = quick_dimension_buffer_;
    s.erase(0, s.find_first_not_of(" \t\r\n"));
    s.erase(s.find_last_not_of(" \t\r\n") + 1);
    if (s.empty()) {
        end_quick_dimension();
        return false;
    }

    size_t sep = s.find_first_of(",; ");
    if (sep != std::string::npos) {
        std::string part_w = s.substr(0, sep);
        std::string part_h = s.substr(sep + 1);
        part_w.erase(0, part_w.find_first_not_of(" \t\r\n"));
        part_w.erase(part_w.find_last_not_of(" \t\r\n") + 1);
        part_h.erase(0, part_h.find_first_not_of(" \t\r\n"));
        part_h.erase(part_h.find_last_not_of(" \t\r\n") + 1);
        try {
            if (!part_w.empty()) new_w = std::stod(part_w);
            if (!part_h.empty()) new_h = std::stod(part_h);
        } catch (...) {
            end_quick_dimension();
            return false;
        }
    } else {
        try {
            new_w = std::stod(s);
            if (active_tool_ == 2 || active_tool_ == 9) {
                new_h = new_w;
            }
        } catch (...) {
            end_quick_dimension();
            return false;
        }
    }

    if (new_w <= 0.0 || new_h <= 0.0) {
        end_quick_dimension();
        return false;
    }

    new_w = std::clamp(new_w, 0.1, geometry::kMaxObjectDimensionMm);
    new_h = std::clamp(new_h, 0.1, geometry::kMaxObjectDimensionMm);

    const double cur_w = bounds_opt->width;
    const double cur_h = bounds_opt->height;
    const double sx = new_w / cur_w;
    const double sy = new_h / cur_h;

    // Scale giữ cố định điểm gốc ban đầu quick_dimension_origin_
    const auto delta = geometry::Transform2D::translation(quick_dimension_origin_.x, quick_dimension_origin_.y) *
                       geometry::Transform2D::scale(sx, sy) *
                       geometry::Transform2D::translation(-quick_dimension_origin_.x, -quick_dimension_origin_.y);

    auto cmd = std::make_unique<document::TransformObjectCommand>(quick_dimension_object_id_, delta);
    const auto res = root().document().execute_command(std::move(cmd));
    if (res.success) {
        root().vector_controller().set_selection({quick_dimension_object_id_}, *root().document().current_snapshot());
        workflow().sync_status_bar();
        std::ostringstream ss_w, ss_h;
        ss_w << std::fixed << std::setprecision(1) << new_w;
        ss_h << std::fixed << std::setprecision(1) << new_h;
        std::string w_str = ss_w.str();
        std::string h_str = ss_h.str();
        if (w_str.size() > 2 && w_str.substr(w_str.size() - 2) == ".0") w_str.erase(w_str.size() - 2);
        if (h_str.size() > 2 && h_str.substr(h_str.size() - 2) == ".0") h_str.erase(h_str.size() - 2);
        status_msg_ = (language_ == UILanguage::Vietnamese)
            ? "Đã áp dụng kích thước: " + w_str + " × " + h_str + " mm"
            : "Applied dimensions: " + w_str + " × " + h_str + " mm";
    }
    end_quick_dimension();
    return res.success;
}
} // namespace pikdraw::desktop
