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

void NativeWindow::commit_numeric_field() {
    if (active_num_field_ == NumericFieldId::None) return;

    if (numeric_effect_index_ == std::numeric_limits<size_t>::max() &&
        (active_num_field_ == text_numeric_field(TextPropertyField::FontSize) ||
         active_num_field_ == text_numeric_field(TextPropertyField::FrameWidth) ||
         active_num_field_ == text_numeric_field(TextPropertyField::LineSpacing))) {
        const auto field = active_num_field_;
        const bool font_size = field == text_numeric_field(TextPropertyField::FontSize);
        const bool frame = field == text_numeric_field(TextPropertyField::FrameWidth);
        const bool draft = is_inline_editing() && text_editor_.has_value();
        const auto clear_input = [&]() {
            active_num_field_ = NumericFieldId::None;
            num_input_buffer_.clear();
            numeric_effect_targets_.clear();
            numeric_effect_index_ = 0;
        };
        if (num_input_buffer_.empty()) {
            clear_input();
            return;
        }
        if (!draft && (root().vector_controller().selection().selected_ids() != numeric_effect_targets_ ||
                       !has_text_properties_target(*this))) {
            clear_input();
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Văn bản hoặc vùng chọn đã đổi · giá trị chưa áp dụng"
                : "Text or selection changed · value was not applied";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (!draft && font_size && text_properties_view(*this).mixed_font) {
            clear_input();
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Kiểu chữ đã trở thành hỗn hợp · giá trị chưa áp dụng"
                : "Font settings became mixed · value was not applied";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        double value = 0.0;
        try {
            std::string normalized = num_input_buffer_;
            std::replace(normalized.begin(), normalized.end(), ',', '.');
            size_t parsed = 0;
            value = std::stod(normalized, &parsed);
            const double maximum = font_size ? 2880.0
                : frame ? geometry::limits::kCanvasMaxMm : 1000.0;
            const double minimum = font_size ? 1.0 : frame ? 0.0 : 25.0;
            if (parsed != normalized.size() || !std::isfinite(value) ||
                value < minimum || value > maximum)
                throw std::out_of_range("text layout field");
        } catch (...) {
            status_msg_ = font_size
                ? (language_ == UILanguage::Vietnamese
                    ? "Cỡ chữ phải từ 1 đến 2880 pt"
                    : "Font size must be between 1 and 2880 pt")
                : frame
                ? (language_ == UILanguage::Vietnamese
                    ? "Khung chữ phải từ 0 đến 1.000.000 mm"
                    : "Text frame width must be between 0 and 1,000,000 mm")
                : (language_ == UILanguage::Vietnamese
                    ? "Giãn dòng phải từ 25 đến 1000%"
                    : "Line spacing must be between 25 and 1000%");
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        bool applied = false;
        if (draft && text_editor_) {
            text_editor_->remember();
            if (font_size) text_editor_->draft.font.size_pt = value;
            else if (frame) text_editor_->draft.frame_width_mm = value;
            else text_editor_->draft.line_spacing_ratio = value / 100.0;
            refresh_inline_text_preview();
            applied = is_inline_editing();
        } else if (!draft) {
            if (font_size) {
                const auto view = text_properties_view(*this);
                applied = view.editable && !view.mixed_font &&
                    workflow().set_selected_text_font(view.family, value, view.bold, view.italic);
                if (applied) font_size_pt_ = value;
            } else {
                applied = workflow().set_selected_text_layout(
                    frame ? std::optional<double>{value} : std::nullopt,
                    frame ? std::nullopt : std::optional<double>{value / 100.0});
            }
        }
        if (!applied) {
            clear_input();
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Văn bản hoặc vùng chọn đã đổi · giá trị chưa áp dụng"
                : "Text or selection changed · value was not applied";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        clear_input();
        status_msg_ = draft
            ? (language_ == UILanguage::Vietnamese
                ? "Đã cập nhật bản nháp chữ · Ctrl+Enter lưu một bước Undo"
                : "Text draft updated · Ctrl+Enter saves one undo step")
            : font_size
                ? (language_ == UILanguage::Vietnamese
                    ? "Đã cập nhật cỡ chữ · Ctrl+Z hoàn tác"
                    : "Updated font size · Ctrl+Z undoes")
                : (language_ == UILanguage::Vietnamese
                    ? "Đã cập nhật thuộc tính đoạn chữ · Ctrl+Z hoàn tác"
                    : "Updated text paragraph property · Ctrl+Z undoes");
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (active_tool_ == 12 && contour_tool_ && contour_tool_->active() &&
        (active_num_field_ == NumericFieldId::DistributeSpacing ||
         active_num_field_ == NumericFieldId::PolygonSides ||
         active_num_field_ == NumericFieldId::PolygonRotation)) {
        const auto field = active_num_field_;
        const auto target = contour_tool_->target_object();
        const auto snapshot = root().document().current_snapshot();
        const auto* object = snapshot ? snapshot->objects().lookup_ref(target) : nullptr;
        const auto* path = object ? std::get_if<document::PathObject>(object) : nullptr;
        if (!path || path->locked) {
            active_num_field_ = NumericFieldId::None;
            num_input_buffer_.clear();
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Path Contour đã đổi hoặc bị khóa · giá trị chưa áp dụng"
                : "Contour Path changed or is locked · value was not applied";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        std::string normalized = num_input_buffer_;
        std::replace(normalized.begin(), normalized.end(), ',', '.');
        try {
            if (field == NumericFieldId::PolygonSides) {
                size_t parsed = 0;
                const int value = std::stoi(normalized, &parsed);
                if (parsed != normalized.size() || value < 1 || value > 99)
                    throw std::out_of_range("contour steps");
                contour_tool_->set_steps(value);
            } else {
                size_t parsed = 0;
                const double value = std::stod(normalized, &parsed);
                if (parsed != normalized.size() || !std::isfinite(value))
                    throw std::out_of_range("contour value");
                if (field == NumericFieldId::DistributeSpacing) {
                    if (value < 0.1 || value > 100000.0)
                        throw std::out_of_range("contour spacing");
                    contour_tool_->set_spacing_mm(value);
                } else {
                    if (value < 1.0 || value > 100.0)
                        throw std::out_of_range("contour miter limit");
                    contour_tool_->set_miter_limit(value);
                }
            }
        } catch (...) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? (field == NumericFieldId::DistributeSpacing
                    ? "Khoảng cách phải từ 0,1 đến 100000 mm"
                    : field == NumericFieldId::PolygonSides
                        ? "Số bước phải là số nguyên từ 1 đến 99"
                        : "Miter limit phải từ 1 đến 100")
                : (field == NumericFieldId::DistributeSpacing
                    ? "Spacing must be 0.1–100000 mm"
                    : field == NumericFieldId::PolygonSides
                        ? "Steps must be a whole number from 1 to 99"
                        : "Miter limit must be 1–100");
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
        status_msg_ = contour_tool_->has_preview()
            ? (language_ == UILanguage::Vietnamese
                ? "Đã cập nhật xem trước Contour · Áp dụng để lưu thành một bước Undo"
                : "Updated Contour preview · Apply to save as one undo step")
            : (language_ == UILanguage::Vietnamese
                ? "Đã cập nhật thông số nhưng preview không khả dụng · chọn Path kín hợp lệ"
                : "Setting updated but preview is unavailable · choose a valid closed Path");
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (active_tool_ == 13 && gradient_tool_ && gradient_tool_->active() &&
        (active_num_field_ == NumericFieldId::EffectOffsetX ||
         active_num_field_ == NumericFieldId::EffectOpacity ||
         active_num_field_ == NumericFieldId::PolygonSides ||
         active_num_field_ == NumericFieldId::PolygonRotation ||
         active_num_field_ == NumericFieldId::ObjectHeight ||
         active_num_field_ == NumericFieldId::ObjectX ||
         active_num_field_ == NumericFieldId::ObjectY)) {
        const auto field = active_num_field_;
        auto& presenter = gradient_tool_->presenter();
        const auto* paint = presenter.draft_paint();
        std::string normalized = num_input_buffer_;
        std::replace(normalized.begin(), normalized.end(), ',', '.');
        try {
            size_t parsed = 0;
            const double value = std::stod(normalized, &parsed);
            if (parsed != normalized.size() || !std::isfinite(value))
                throw std::out_of_range("gradient numeric field");
            const auto& current_targets = root().vector_controller().selection().selected_ids();
            if (current_targets != gradient_tool_->target_objects() || !paint)
                throw std::runtime_error("gradient target changed");

            bool applied = false;
            if (field == NumericFieldId::EffectOffsetX ||
                field == NumericFieldId::EffectOpacity ||
                field == NumericFieldId::PolygonSides) {
                const uint64_t stop_id = presenter.selected_stop_id();
                const auto stop = std::find_if(paint->stops.begin(), paint->stops.end(),
                    [stop_id](const auto& item) { return item.id == stop_id; });
                if (stop == paint->stops.end())
                    throw std::runtime_error("gradient stop changed");
                const size_t index = static_cast<size_t>(stop - paint->stops.begin());
                if (field == NumericFieldId::EffectOffsetX && value >= 0.0 && value <= 100.0)
                    applied = presenter.set_stop_offset(index, value / 100.0);
                else if (field == NumericFieldId::EffectOpacity && value >= 0.0 && value <= 100.0)
                    applied = presenter.set_stop_opacity(index, value / 100.0);
                else if (field == NumericFieldId::PolygonSides && value >= 5.0 && value <= 95.0 &&
                         index + 1 < paint->stops.size())
                    applied = presenter.set_segment_midpoint(index, value / 100.0);
                else
                    throw std::out_of_range("gradient stop field range");
                (void)presenter.select_stop(stop_id);
            } else if (field == NumericFieldId::PolygonRotation) {
                if (value < -36000.0 || value > 36000.0)
                    throw std::out_of_range("gradient angle range");
                presenter.set_angle_degrees(value);
                applied = true;
            } else if (field == NumericFieldId::ObjectHeight) {
                if (value < 0.001 || value > geometry::limits::kCanvasMaxMm)
                    throw std::out_of_range("gradient length range");
                presenter.set_length_mm(value);
                applied = true;
            } else {
                const double limit = geometry::limits::kCanvasMaxMm;
                if (value < -limit || value > limit)
                    throw std::out_of_range("gradient center range");
                auto center = presenter.geometry_projection().center_mm;
                if (field == NumericFieldId::ObjectX) center.x = value;
                else center.y = value;
                presenter.set_center_mm(center);
                applied = true;
            }
            if (!applied)
                throw std::runtime_error("gradient presenter rejected value");
        } catch (const std::runtime_error&) {
            const bool selection_stale =
                root().vector_controller().selection().selected_ids() != gradient_tool_->target_objects();
            if (selection_stale) {
                active_num_field_ = NumericFieldId::None;
                num_input_buffer_.clear();
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Vùng chọn Gradient đã đổi · giá trị chưa áp dụng"
                    : "Gradient selection changed · value was not applied";
            } else {
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Stop hoặc bản nháp đã đổi · nhập lại giá trị"
                    : "Gradient stop or draft changed · enter the value again";
            }
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        } catch (...) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Giá trị Gradient không hợp lệ · %: 0–100, midpoint: 5–95, góc: ±36000°, tâm: ±1.000.000 mm"
                : "Invalid Gradient value · %: 0–100, midpoint: 5–95, angle: ±36000°, center: ±1,000,000 mm";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã cập nhật bản nháp Gradient · Apply để ghi vào tài liệu, Cancel để bỏ"
            : "Updated Gradient draft · Apply writes it to the document; Cancel discards it";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (active_tool_ == 11 &&
        (active_num_field_ == NumericFieldId::ObjectX ||
         active_num_field_ == NumericFieldId::ObjectY)) {
        const NumericFieldId field = active_num_field_;
        if (num_input_buffer_.empty()) {
            active_num_field_ = NumericFieldId::None;
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Chưa nhập tọa độ · node không đổi"
                : "No coordinate entered · node unchanged";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        std::string text = num_input_buffer_;
        std::replace(text.begin(), text.end(), ',', '.');
        try {
            size_t parsed = 0;
            const double value = std::stod(text, &parsed);
            const double limit = geometry::kMaxCanvasDimensionMm * 0.5;
            if (parsed != text.size() || !std::isfinite(value) ||
                value < -limit || value > limit)
                throw std::out_of_range("node coordinate");
            const auto position = node_tool_
                ? node_tool_->single_selected_node_position() : std::nullopt;
            if (!position) {
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Node đã đổi hoặc có nhiều node được chọn · chưa áp dụng"
                    : "Node selection changed or is mixed · value not applied";
                if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
            geometry::Point2D updated = *position;
            if (field == NumericFieldId::ObjectX) updated.x = value;
            else updated.y = value;
            if (!node_tool_->set_selected_node_position(updated)) {
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Không thể cập nhật tọa độ node · dữ liệu không đổi"
                    : "Could not update the node coordinate · value unchanged";
                if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
        } catch (...) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Tọa độ phải là số hữu hạn trong khoảng −20.000 đến 20.000 mm"
                : "Coordinate must be finite and within −20,000 to 20,000 mm";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã cập nhật tọa độ node · Ctrl+Z hoàn tác"
            : "Node coordinate updated · Ctrl+Z undoes";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

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
            } else if (field == NumericFieldId::RectCornerRadiusTL ||
                       field == NumericFieldId::RectCornerRadiusTR ||
                       field == NumericFieldId::RectCornerRadiusBR ||
                       field == NumericFieldId::RectCornerRadiusBL) {
                double radius = std::clamp(val, 0.0, 5000.0);
                if (active_tool_ == 1 && root().vector_controller().selection().selected_ids().empty()) {
                    if (creation_rect_corners_locked_) {
                        creation_rect_corner_tl_ = radius;
                        creation_rect_corner_tr_ = radius;
                        creation_rect_corner_br_ = radius;
                        creation_rect_corner_bl_ = radius;
                    } else {
                        if (field == NumericFieldId::RectCornerRadiusTL) creation_rect_corner_tl_ = radius;
                        else if (field == NumericFieldId::RectCornerRadiusTR) creation_rect_corner_tr_ = radius;
                        else if (field == NumericFieldId::RectCornerRadiusBR) creation_rect_corner_br_ = radius;
                        else if (field == NumericFieldId::RectCornerRadiusBL) creation_rect_corner_bl_ = radius;
                    }
                } else {
                    if (rect_corners_locked_) {
                        rect_corner_tl_ = radius;
                        rect_corner_tr_ = radius;
                        rect_corner_br_ = radius;
                        rect_corner_bl_ = radius;
                    } else {
                        if (field == NumericFieldId::RectCornerRadiusTL) rect_corner_tl_ = radius;
                        else if (field == NumericFieldId::RectCornerRadiusTR) rect_corner_tr_ = radius;
                        else if (field == NumericFieldId::RectCornerRadiusBR) rect_corner_br_ = radius;
                        else if (field == NumericFieldId::RectCornerRadiusBL) rect_corner_bl_ = radius;
                    }
                    vector::RectCornerSettings s;
                    s.type = rect_corner_type_;
                    s.tl = rect_corner_tl_;
                    s.tr = rect_corner_tr_;
                    s.br = rect_corner_br_;
                    s.bl = rect_corner_bl_;
                    s.locked = rect_corners_locked_;
                    s.scale_relative = rect_corners_scale_relative_;
                    apply_rect_corner_settings(s);
                }
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
                            } else if (field == NumericFieldId::ObjectRotation) {
                                const auto transform = std::visit([](const auto& v) -> geometry::Transform2D {
                                    using T = std::decay_t<decltype(v)>;
                                    if constexpr (std::is_same_v<T, document::GroupObject>) return {};
                                    else return v.transform;
                                }, *obj);
                                double cur_deg = std::atan2(transform.b, transform.a) * (180.0 / 3.14159265358979323846);
                                if (cur_deg < 0.0) cur_deg += 360.0;
                                double target_deg = std::fmod(val, 360.0);
                                if (target_deg < 0.0) target_deg += 360.0;
                                double delta_deg = target_deg - cur_deg;
                                workflow().rotate_selected_degrees(delta_deg);
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
