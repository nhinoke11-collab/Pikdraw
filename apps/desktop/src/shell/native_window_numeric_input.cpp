#include "pikdraw/desktop/shell/native_window.hpp"
#include "../automation/native_window_automation.hpp"
#include "pikdraw/desktop/actions/action_id.hpp"
#include "native_window_menus.hpp"
#include "native_window_page_presets.hpp"
#include "native_window_text_properties.hpp"
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
#include <limits>

namespace pikdraw::desktop {

void NativeWindow::start_numeric_input(NumericFieldId field) {
    if (active_num_field_ == field)
        return;
    if (active_num_field_ != NumericFieldId::None && active_num_field_ != field) {
        commit_numeric_field();
        if (active_num_field_ != NumericFieldId::None)
            return;
    }
    if ((field == text_numeric_field(TextPropertyField::FontSize) ||
         field == text_numeric_field(TextPropertyField::FrameWidth) ||
         field == text_numeric_field(TextPropertyField::LineSpacing)) &&
        active_dock_tab_ == 0 && has_text_properties_target(*this)) {
        const auto view = text_properties_view(*this);
        if (field == text_numeric_field(TextPropertyField::FontSize) && view.mixed_font) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Kiểu chữ hỗn hợp · chọn một đối tượng hoặc thống nhất trước"
                : "Font settings are mixed · select one object or unify them first";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (!view.editable) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Văn bản bị khóa hoặc ẩn · không thể sửa thuộc tính"
                : "Text is locked or hidden · properties cannot be edited";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        active_num_field_ = field;
        numeric_effect_index_ = std::numeric_limits<size_t>::max();
        numeric_effect_targets_ = root().vector_controller().selection().selected_ids();
        num_input_buffer_.clear();
        if (field == text_numeric_field(TextPropertyField::FontSize))
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Cỡ chữ 1–2880 pt · Enter áp dụng · Esc hủy"
                : "Font size 1–2880 pt · Enter applies · Esc cancels";
        else if (field == text_numeric_field(TextPropertyField::FrameWidth))
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Khung chữ 0–1.000.000 mm · 0 = chữ tự do · Enter áp dụng · Esc hủy"
                : "Text frame 0–1,000,000 mm · 0 = point text · Enter applies · Esc cancels";
        else
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Giãn dòng 25–1000% · Enter áp dụng · Esc hủy"
                : "Line spacing 25–1000% · Enter applies · Esc cancels";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (active_tool_ == 12 &&
        (field == NumericFieldId::DistributeSpacing ||
         field == NumericFieldId::PolygonSides ||
         field == NumericFieldId::PolygonRotation)) {
        const auto target = contour_tool_ ? contour_tool_->target_object() : document::kInvalidObjectId;
        const auto snapshot = root().document().current_snapshot();
        const auto* object = snapshot ? snapshot->objects().lookup_ref(target) : nullptr;
        const auto* path = object ? std::get_if<document::PathObject>(object) : nullptr;
        if (!contour_tool_ || !contour_tool_->active() || !path || path->locked) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Chọn Path không khóa để sửa thông số Contour"
                : "Select an unlocked Path to edit Contour settings";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        active_num_field_ = field;
        num_input_buffer_.clear();
        std::ostringstream value;
        value << std::fixed << std::setprecision(field == NumericFieldId::PolygonSides ? 0 : 3);
        if (field == NumericFieldId::DistributeSpacing) value << contour_tool_->spacing_mm();
        else if (field == NumericFieldId::PolygonSides) value << contour_tool_->steps();
        else value << contour_tool_->miter_limit();
        num_input_buffer_ = value.str();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Contour · nhập giá trị · Enter cập nhật xem trước · Tab sang trường kế · Esc bỏ nhập"
            : "Contour · enter a value · Enter updates preview · Tab moves to next field · Esc cancels editing";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (active_tool_ == 13 &&
        (field == NumericFieldId::EffectOffsetX || field == NumericFieldId::EffectOpacity ||
         field == NumericFieldId::PolygonSides || field == NumericFieldId::PolygonRotation ||
         field == NumericFieldId::ObjectHeight || field == NumericFieldId::ObjectX ||
         field == NumericFieldId::ObjectY)) {
        if (!gradient_tool_ || !gradient_tool_->active())
            return;
        auto& presenter = gradient_tool_->presenter();
        const auto& summary = presenter.summary();
        const auto* paint = presenter.draft_paint();
        const bool stop_field = field == NumericFieldId::EffectOffsetX ||
            field == NumericFieldId::EffectOpacity || field == NumericFieldId::PolygonSides;
        if (!summary.can_edit_gradient() || !paint ||
            (stop_field && summary.mixed_stops && summary.supported_count > 1)) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Thuộc tính Gradient này đang khóa do chưa chọn Path hoặc stop bị hỗn hợp"
                : "This Gradient property is unavailable without a Path or while stops are mixed";
#ifdef _WIN32
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
            return;
        }
        const auto stop_it = std::find_if(paint->stops.begin(), paint->stops.end(),
            [&](const auto& stop) { return stop.id == presenter.selected_stop_id(); });
        const size_t stop_index = stop_it == paint->stops.end()
            ? paint->stops.size() : static_cast<size_t>(stop_it - paint->stops.begin());
        if (stop_field && (stop_index >= paint->stops.size() ||
            (field == NumericFieldId::PolygonSides && stop_index + 1 >= paint->stops.size()))) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Chọn một stop có đoạn kế tiếp để sửa trung điểm"
                : "Select a stop with a following segment to edit its midpoint";
#ifdef _WIN32
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
            return;
        }
        const bool geometry_field = field == NumericFieldId::PolygonRotation ||
            field == NumericFieldId::ObjectHeight || field == NumericFieldId::ObjectX ||
            field == NumericFieldId::ObjectY;
        const bool geometry_mixed = summary.supported_count > 1 &&
            (summary.mixed_angle || summary.mixed_length || summary.mixed_center ||
             summary.mixed_aspect_ratio);
        if (geometry_field && geometry_mixed) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Hình học Gradient đang hỗn hợp · chọn một Path để sửa riêng"
                : "Gradient geometry is mixed · select one Path to edit safely";
#ifdef _WIN32
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
            return;
        }
        active_num_field_ = field;
        num_input_buffer_.clear();
        std::ostringstream value;
        value << std::fixed << std::setprecision(2);
        if (field == NumericFieldId::EffectOffsetX)
            value << paint->stops[stop_index].offset * 100.0;
        else if (field == NumericFieldId::EffectOpacity)
            value << paint->stops[stop_index].opacity * 100.0;
        else if (field == NumericFieldId::PolygonSides)
            value << paint->stops[stop_index].midpoint_to_next * 100.0;
        else {
            const auto projection = presenter.geometry_projection();
            const bool mixed = (field == NumericFieldId::PolygonRotation && summary.mixed_angle) ||
                               (field == NumericFieldId::ObjectHeight && summary.mixed_length);
            if (!mixed) {
                if (field == NumericFieldId::PolygonRotation) value << projection.angle_degrees;
                else if (field == NumericFieldId::ObjectHeight) value << projection.length_mm;
                else if (field == NumericFieldId::ObjectX) value << projection.center_mm.x;
                else value << projection.center_mm.y;
                num_input_buffer_ = value.str();
            }
        }
        if (stop_field)
            num_input_buffer_ = value.str();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Nhập Gradient · Enter cập nhật bản nháp · Tab chuyển ô · Apply ghi tài liệu · Esc hủy ô"
            : "Edit Gradient · Enter updates the draft · Tab next · Apply writes the document · Esc cancels field";
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
        return;
    }
    if (active_tool_ == 11 &&
        (field == NumericFieldId::ObjectX || field == NumericFieldId::ObjectY) &&
        (!node_tool_ || !node_tool_->single_selected_node_position())) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chọn đúng một node để nhập tọa độ X/Y"
            : "Select exactly one node before entering X/Y coordinates";
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
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
    } else if (active_tool_ == 11 &&
               (field == NumericFieldId::ObjectX || field == NumericFieldId::ObjectY)) {
        // The current coordinate remains visible in Properties; typing replaces it.
        num_input_buffer_.clear();
    } else if (field == NumericFieldId::RectCornerRadiusTL ||
               field == NumericFieldId::RectCornerRadiusTR ||
               field == NumericFieldId::RectCornerRadiusBR ||
               field == NumericFieldId::RectCornerRadiusBL) {
        double r = 0.0;
        if (active_tool_ == 1 && root().vector_controller().selection().selected_ids().empty()) {
            if (field == NumericFieldId::RectCornerRadiusTL) r = creation_rect_corner_tl_;
            else if (field == NumericFieldId::RectCornerRadiusTR) r = creation_rect_corner_tr_;
            else if (field == NumericFieldId::RectCornerRadiusBR) r = creation_rect_corner_br_;
            else if (field == NumericFieldId::RectCornerRadiusBL) r = creation_rect_corner_bl_;
        } else {
            sync_rect_corner_controls_from_selection();
            if (field == NumericFieldId::RectCornerRadiusTL) r = rect_corner_tl_;
            else if (field == NumericFieldId::RectCornerRadiusTR) r = rect_corner_tr_;
            else if (field == NumericFieldId::RectCornerRadiusBR) r = rect_corner_br_;
            else if (field == NumericFieldId::RectCornerRadiusBL) r = rect_corner_bl_;
        }
        ss << std::fixed << std::setprecision(1) << r;
        num_input_buffer_ = ss.str();
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
                    else if (field == NumericFieldId::ObjectRotation) {
                        const auto transform = std::visit([](const auto& val) -> geometry::Transform2D {
                            using T = std::decay_t<decltype(val)>;
                            if constexpr (std::is_same_v<T, document::GroupObject>) return {};
                            else return val.transform;
                        }, *obj);
                        double deg = std::atan2(transform.b, transform.a) * (180.0 / 3.14159265358979323846);
                        if (deg < 0.0) deg += 360.0;
                        ss << std::fixed << std::setprecision(1) << deg;
                    }
                    num_input_buffer_ = ss.str();
                }
            }
        }
    }

    if (num_input_buffer_.size() > 2 && num_input_buffer_.substr(num_input_buffer_.size() - 2) == ".0") {
        num_input_buffer_.erase(num_input_buffer_.size() - 2);
    }

    if (active_tool_ == 11 &&
        (field == NumericFieldId::ObjectX || field == NumericFieldId::ObjectY)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Nhập tọa độ node (mm) · Enter áp dụng · Tab đổi X/Y · Esc hủy · Ctrl+Z hoàn tác"
            : "Enter node coordinate in mm · Enter applies · Tab switches X/Y · Esc cancels · Ctrl+Z undoes";
    } else if (field == NumericFieldId::ObjectOpacity) {
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
    } else if (field == NumericFieldId::RectCornerRadiusTL ||
               field == NumericFieldId::RectCornerRadiusTR ||
               field == NumericFieldId::RectCornerRadiusBR ||
               field == NumericFieldId::RectCornerRadiusBL) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? (rect_corners_locked_
                ? "Bán kính bo góc (mm) · Khóa 4 góc đang bật · Enter áp dụng · Tab đổi ô · Esc hủy"
                : "Bán kính bo góc này (mm) · Khóa đang mở · Enter áp dụng · Tab đổi ô · Esc hủy")
            : (rect_corners_locked_
                ? "Corner radius (mm) · 4 corners locked together · Enter applies · Tab cycles · Esc cancels"
                : "Corner radius (mm) · Independent corner · Enter applies · Tab cycles · Esc cancels");
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
        if (numeric_effect_index_ == std::numeric_limits<size_t>::max() &&
            (active_num_field_ == text_numeric_field(TextPropertyField::FontSize) ||
             active_num_field_ == text_numeric_field(TextPropertyField::FrameWidth) ||
             active_num_field_ == text_numeric_field(TextPropertyField::LineSpacing))) {
            next = active_num_field_ == text_numeric_field(TextPropertyField::FontSize)
                ? text_numeric_field(TextPropertyField::FrameWidth)
                : active_num_field_ == text_numeric_field(TextPropertyField::FrameWidth)
                    ? text_numeric_field(TextPropertyField::LineSpacing)
                    : text_numeric_field(TextPropertyField::FontSize);
            commit_numeric_field();
            if (active_num_field_ != NumericFieldId::None) return;
            start_numeric_input(next);
            return;
        }
        if (active_num_field_ == NumericFieldId::RectCornerRadiusTL ||
            active_num_field_ == NumericFieldId::RectCornerRadiusTR ||
            active_num_field_ == NumericFieldId::RectCornerRadiusBR ||
            active_num_field_ == NumericFieldId::RectCornerRadiusBL) {
            if (active_num_field_ == NumericFieldId::RectCornerRadiusTL) next = NumericFieldId::RectCornerRadiusTR;
            else if (active_num_field_ == NumericFieldId::RectCornerRadiusTR) next = NumericFieldId::RectCornerRadiusBR;
            else if (active_num_field_ == NumericFieldId::RectCornerRadiusBR) next = NumericFieldId::RectCornerRadiusBL;
            else next = NumericFieldId::RectCornerRadiusTL;
            commit_numeric_field();
            if (active_num_field_ != NumericFieldId::None) return;
            start_numeric_input(next);
            return;
        }
        if (active_tool_ == 13) {
            switch (active_num_field_) {
                case NumericFieldId::EffectOffsetX: next = NumericFieldId::EffectOpacity; break;
                case NumericFieldId::EffectOpacity: next = NumericFieldId::PolygonSides; break;
                case NumericFieldId::PolygonSides: next = NumericFieldId::PolygonRotation; break;
                case NumericFieldId::PolygonRotation: next = NumericFieldId::ObjectHeight; break;
                case NumericFieldId::ObjectHeight: next = NumericFieldId::ObjectX; break;
                case NumericFieldId::ObjectX: next = NumericFieldId::ObjectY; break;
                case NumericFieldId::ObjectY: next = NumericFieldId::EffectOffsetX; break;
                default: break;
            }
            commit_numeric_field();
            if (active_num_field_ != NumericFieldId::None)
                return;
            if (next != NumericFieldId::None)
                start_numeric_input(next);
            return;
        }
        if (active_tool_ == 12) {
            switch (active_num_field_) {
                case NumericFieldId::DistributeSpacing: next = NumericFieldId::PolygonSides; break;
                case NumericFieldId::PolygonSides: next = NumericFieldId::PolygonRotation; break;
                case NumericFieldId::PolygonRotation: next = NumericFieldId::DistributeSpacing; break;
                default: break;
            }
            commit_numeric_field();
            if (active_num_field_ != NumericFieldId::None) return;
            if (next != NumericFieldId::None) start_numeric_input(next);
            return;
        }
        switch (active_num_field_) {
            case NumericFieldId::PageWidth: next = NumericFieldId::PageHeight; break;
            case NumericFieldId::PageHeight: next = NumericFieldId::PageWidth; break;
            case NumericFieldId::PolygonSides: next = NumericFieldId::PolygonRotation; break;
            case NumericFieldId::PolygonRotation: next = NumericFieldId::PolygonSides; break;
            case NumericFieldId::ModalWidth: next = NumericFieldId::ModalHeight; break;
            case NumericFieldId::ModalHeight: next = NumericFieldId::ModalWidth; break;
            case NumericFieldId::ObjectX: next = NumericFieldId::ObjectY; break;
            case NumericFieldId::ObjectY: next = active_tool_ == 11
                ? NumericFieldId::ObjectX : NumericFieldId::ObjectWidth; break;
            case NumericFieldId::ObjectWidth: next = NumericFieldId::ObjectHeight; break;
            case NumericFieldId::ObjectHeight: next = NumericFieldId::ObjectRotation; break;
            case NumericFieldId::ObjectRotation: next = NumericFieldId::ObjectX; break;
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
               ((active_num_field_ != NumericFieldId::PolygonSides || active_tool_ == 13) && ch == L'.') ||
               (ch == L',' && active_num_field_ == NumericFieldId::DistributeSpacing) ||
               (ch == L',' && (active_num_field_ == text_numeric_field(TextPropertyField::FontSize) ||
                               active_num_field_ == text_numeric_field(TextPropertyField::FrameWidth) ||
                               active_num_field_ == text_numeric_field(TextPropertyField::LineSpacing))) ||
               (ch == L',' && active_tool_ == 11 &&
                (active_num_field_ == NumericFieldId::ObjectX ||
                 active_num_field_ == NumericFieldId::ObjectY)) ||
               (ch == L',' && active_num_field_ == NumericFieldId::ObjectOpacity) ||
               (ch == L',' && (active_num_field_ == NumericFieldId::FillOpacity ||
                               active_num_field_ == NumericFieldId::StrokeOpacity)) ||
               (ch == L',' && (active_num_field_ == NumericFieldId::EffectSigmaX ||
                               active_num_field_ == NumericFieldId::EffectSigmaY ||
                               active_num_field_ == NumericFieldId::EffectOffsetX ||
                               active_num_field_ == NumericFieldId::EffectOffsetY ||
                               active_num_field_ == NumericFieldId::EffectOpacity)) ||
               (ch == L',' && active_tool_ == 13 &&
                (active_num_field_ == NumericFieldId::PolygonSides ||
                 active_num_field_ == NumericFieldId::PolygonRotation ||
                 active_num_field_ == NumericFieldId::ObjectHeight ||
                 active_num_field_ == NumericFieldId::ObjectX ||
                 active_num_field_ == NumericFieldId::ObjectY)) ||
               (ch == L',' && active_tool_ == 12 &&
                (active_num_field_ == NumericFieldId::DistributeSpacing ||
                 active_num_field_ == NumericFieldId::PolygonRotation)) ||
               ((active_num_field_ != NumericFieldId::PolygonSides || active_tool_ == 13) &&
                (active_tool_ != 12 || active_num_field_ != NumericFieldId::PolygonRotation) && ch == L'-')) {
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


} // namespace pikdraw::desktop
