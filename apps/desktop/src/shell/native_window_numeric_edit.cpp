#include "pikdraw/desktop/shell/native_window.hpp"
#include "../automation/native_window_automation.hpp"
#include "pikdraw/desktop/actions/action_id.hpp"
#include "native_window_menus.hpp"
#include "native_window_page_presets.hpp"
#include "native_window_text_properties.hpp"
#include "native_window_numeric_field_descriptor.hpp"
#include "native_window_length_input.hpp"
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
namespace {
template <typename Callback>
struct ScopeExit {
    Callback callback;
    ~ScopeExit() { callback(); }
};
template <typename Callback>
ScopeExit(Callback) -> ScopeExit<Callback>;
} // namespace

void NativeWindow::commit_numeric_field() {
    if (active_num_field_ == NumericFieldId::None) return;
    const ScopeExit clear_numeric_context{[this] {
        if (active_num_field_ == NumericFieldId::None)
            numeric_input_context_ = {};
    }};

    const auto field_uses_document_context = [](NumericFieldId field) {
        return field != NumericFieldId::None && field != NumericFieldId::TacLimitPercent &&
            field != NumericFieldId::ModalWidth && field != NumericFieldId::ModalHeight;
    };
    if (field_uses_document_context(active_num_field_)) {
        const auto snapshot = root().document().current_snapshot();
        const auto& selection = root().vector_controller().selection().selected_ids();
        const auto field = active_num_field_;
        const bool contour_field = field == NumericFieldId::ContourSpacing ||
            field == NumericFieldId::ContourSteps || field == NumericFieldId::ContourTolerance ||
            field == NumericFieldId::ContourMiterLimit;
        const bool gradient_field = field == NumericFieldId::GradientStopOffset ||
            field == NumericFieldId::GradientStopOpacity || field == NumericFieldId::GradientStopMidpoint ||
            field == NumericFieldId::GradientAngle || field == NumericFieldId::GradientLength ||
            field == NumericFieldId::GradientCenterX || field == NumericFieldId::GradientCenterY;
        const bool artboard_field = field == NumericFieldId::ArtboardX ||
            field == NumericFieldId::ArtboardY || field == NumericFieldId::ArtboardWidth ||
            field == NumericFieldId::ArtboardHeight;
        const auto current_contour_target = contour_tool_
            ? contour_tool_->target_object() : document::kInvalidObjectId;
        const uint64_t current_gradient_stop = gradient_tool_ && gradient_tool_->active()
            ? gradient_tool_->presenter().selected_stop_id() : 0;
        const bool stale = !numeric_input_context_.valid ||
            numeric_input_context_.root_identity != static_cast<const void*>(&root()) ||
            numeric_input_context_.page_id != (snapshot ? snapshot->active_page_id() : 0) ||
            numeric_input_context_.document_revision != root().document().revision() ||
            numeric_input_context_.view_generation != view_generation_ ||
            numeric_input_context_.active_tool != active_tool_ ||
            numeric_input_context_.selection_ids != selection ||
            (artboard_field && numeric_input_context_.artboard_id != active_tab().active_artboard_id) ||
            (contour_field && numeric_input_context_.contour_target != current_contour_target) ||
            (gradient_field && numeric_input_context_.gradient_stop_id != current_gradient_stop);
        if (stale) {
            cancel_numeric_field();
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Ngữ cảnh tài liệu, trang, vùng chọn hoặc công cụ đã đổi · giá trị chưa áp dụng"
                : "Document, page, selection, or tool context changed · value was not applied";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
    }

    if (active_num_field_ == NumericFieldId::RasterExposureEv) {
        const double exposure_limit = selected_bitmap_has_light_node() ? 5.0 : 10.0;
        try {
            std::string normalized = num_input_buffer_;
            std::replace(normalized.begin(), normalized.end(), ',', '.');
            size_t parsed = 0;
            const double value = std::stod(normalized, &parsed);
            if (parsed != normalized.size() || !std::isfinite(value) ||
                value < -exposure_limit || value > exposure_limit)
                throw std::out_of_range("raster exposure EV");
            if (!set_selected_bitmap_exposure_ev(value))
                throw std::runtime_error("raster exposure target changed");
        } catch (...) {
            std::ostringstream limit_text;
            limit_text << std::fixed << std::setprecision(0) << exposure_limit;
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Exposure phải trong khoảng ±" + limit_text.str() + " EV; chưa áp dụng"
                : "Exposure must be within ±" + limit_text.str() + " EV; not applied";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã cập nhật Exposure EV · Ctrl+Z hoàn tác"
            : "Updated Exposure EV · Ctrl+Z undoes";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (active_num_field_ == NumericFieldId::RasterColorBalanceRedCyan ||
        active_num_field_ == NumericFieldId::RasterColorBalanceGreenMagenta ||
        active_num_field_ == NumericFieldId::RasterColorBalanceBlueYellow) {
        const auto field = active_num_field_;
        try {
            std::string normalized = num_input_buffer_;
            std::replace(normalized.begin(), normalized.end(), ',', '.');
            size_t parsed = 0;
            const double value = std::stod(normalized, &parsed);
            if (parsed != normalized.size() || !std::isfinite(value) ||
                value < -100.0 || value > 100.0 ||
                !set_selected_bitmap_color_balance_value(field, value))
                throw std::out_of_range("raster color balance");
        } catch (...) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Mỗi trục Cân bằng màu phải từ −100 đến +100%; chưa áp dụng"
                : "Each Color Balance axis must be from −100 to +100%; not applied";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã cập nhật Cân bằng màu · Ctrl+Z hoàn tác"
            : "Updated Color Balance · Ctrl+Z undoes";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (active_num_field_ == NumericFieldId::RasterLightContrast ||
        active_num_field_ == NumericFieldId::RasterLightHighlights ||
        active_num_field_ == NumericFieldId::RasterLightShadows ||
        active_num_field_ == NumericFieldId::RasterLightWhites ||
        active_num_field_ == NumericFieldId::RasterLightBlacks) {
        const auto field = active_num_field_;
        try {
            std::string normalized = num_input_buffer_;
            std::replace(normalized.begin(), normalized.end(), ',', '.');
            size_t parsed = 0;
            const double value = std::stod(normalized, &parsed);
            if (parsed != normalized.size() || !std::isfinite(value) ||
                value < -100.0 || value > 100.0 ||
                !set_selected_bitmap_light_value(field, value))
                throw std::out_of_range("raster Light adjustment");
        } catch (...) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Thông số Ánh sáng phải là số hữu hạn từ −100 đến +100%; chưa áp dụng"
                : "Light value must be finite and between −100 and +100%; not applied";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã cập nhật Ánh sáng · Ctrl+Z hoàn tác"
            : "Updated Light adjustment · Ctrl+Z undoes";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (active_num_field_ == NumericFieldId::RasterHslHue ||
        active_num_field_ == NumericFieldId::RasterHslSaturation ||
        active_num_field_ == NumericFieldId::RasterHslLightness ||
        active_num_field_ == NumericFieldId::RasterVibranceAmount) {
        const auto field = active_num_field_;
        const bool hue = field == NumericFieldId::RasterHslHue;
        try {
            std::string normalized = num_input_buffer_;
            std::replace(normalized.begin(), normalized.end(), ',', '.');
            size_t parsed = 0;
            const double value = std::stod(normalized, &parsed);
            const double minimum = hue ? -180.0 : -100.0;
            const double maximum = hue ? 180.0 : 100.0;
            if (parsed != normalized.size() || !std::isfinite(value) ||
                value < minimum || value > maximum ||
                !set_selected_bitmap_color_adjustment(field, value))
                throw std::out_of_range("raster HSL/Vibrance field");
        } catch (...) {
            status_msg_ = hue
                ? (language_ == UILanguage::Vietnamese
                    ? "Hue phải từ −180 đến +180°; chưa áp dụng"
                    : "Hue must be from −180 to +180°; not applied")
                : (language_ == UILanguage::Vietnamese
                    ? "HSL/Vibrance phải từ −100 đến +100%; chưa áp dụng"
                    : "HSL/Vibrance must be from −100 to +100%; not applied");
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã cập nhật HSL/Vibrance · Ctrl+Z hoàn tác"
            : "Updated HSL/Vibrance · Ctrl+Z undoes";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (active_num_field_ == NumericFieldId::RasterLevelsBlackIn ||
        active_num_field_ == NumericFieldId::RasterLevelsWhiteIn ||
        active_num_field_ == NumericFieldId::RasterLevelsGamma ||
        active_num_field_ == NumericFieldId::RasterLevelsBlackOut ||
        active_num_field_ == NumericFieldId::RasterLevelsWhiteOut) {
        const auto field = active_num_field_;
        const bool gamma = field == NumericFieldId::RasterLevelsGamma;
        try {
            std::string normalized = num_input_buffer_;
            std::replace(normalized.begin(), normalized.end(), ',', '.');
            size_t parsed = 0;
            const double value = std::stod(normalized, &parsed);
            const double minimum = gamma ? 0.1 : 0.0;
            const double maximum = gamma ? 10.0 : 100.0;
            if (parsed != normalized.size() || !std::isfinite(value) ||
                value < minimum || value > maximum ||
                !set_selected_bitmap_levels_value(field, value))
                throw std::out_of_range("raster Levels field or input/output ordering");
        } catch (...) {
            status_msg_ = gamma
                ? (language_ == UILanguage::Vietnamese
                    ? "Gamma phải từ 0,1 đến 10×; chưa áp dụng"
                    : "Gamma must be from 0.1 to 10×; not applied")
                : (language_ == UILanguage::Vietnamese
                    ? "Mức Levels phải từ 0 đến 100% và giữ thứ tự đen < trắng; chưa áp dụng"
                    : "Levels values must be 0–100% and preserve black < white ordering; not applied");
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã cập nhật Levels · Ctrl+Z hoàn tác"
            : "Updated Levels · Ctrl+Z undoes";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (active_num_field_ == NumericFieldId::RasterWhiteBalanceTemperature ||
        active_num_field_ == NumericFieldId::RasterWhiteBalanceTint) {
        try {
            std::string normalized = num_input_buffer_;
            std::replace(normalized.begin(), normalized.end(), ',', '.');
            size_t parsed = 0;
            const double value = std::stod(normalized, &parsed);
            if (parsed != normalized.size() || !std::isfinite(value) ||
                value < -100.0 || value > 100.0 ||
                !set_selected_bitmap_white_balance_value(
                    active_num_field_, value))
                throw std::out_of_range("raster relative white balance");
        } catch (...) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Cân bằng trắng tương đối phải từ −100 đến +100%; chưa áp dụng"
                : "Relative White Balance must be from −100 to +100%; not applied";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã cập nhật Cân bằng trắng · Ctrl+Z hoàn tác"
            : "Updated White Balance · Ctrl+Z undoes";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (active_num_field_ == NumericFieldId::RasterCurvePoint0 ||
        active_num_field_ == NumericFieldId::RasterCurvePoint1 ||
        active_num_field_ == NumericFieldId::RasterCurvePoint2 ||
        active_num_field_ == NumericFieldId::RasterCurvePoint3 ||
        active_num_field_ == NumericFieldId::RasterCurvePoint4) {
        try {
            std::string normalized = num_input_buffer_;
            std::replace(normalized.begin(), normalized.end(), ',', '.');
            size_t parsed = 0;
            const double value = std::stod(normalized, &parsed);
            if (parsed != normalized.size() || !std::isfinite(value) ||
                value < 0.0 || value > 100.0 ||
                !set_selected_bitmap_curve_point(active_num_field_, value))
                throw std::out_of_range("raster Curves output point");
        } catch (...) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Điểm Curves phải từ 0 đến 100%; chưa áp dụng"
                : "Curves output points must be from 0 to 100%; not applied";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã cập nhật Curves · Ctrl+Z hoàn tác"
            : "Updated Curves · Ctrl+Z undoes";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (active_num_field_ == NumericFieldId::TacLimitPercent) {
        std::string normalized = num_input_buffer_;
        std::replace(normalized.begin(), normalized.end(), ',', '.');
        try {
            size_t parsed = 0;
            const double value = std::stod(normalized, &parsed);
            if (parsed != normalized.size() || !std::isfinite(value) ||
                value < 100.0 || value > 400.0 || std::floor(value) != value)
                throw std::out_of_range("manual TAC limit");
            settings_tac_limit_percent_ = value;
            settings_tac_summary_.reset();
            settings_tac_error_.clear();
            active_num_field_ = NumericFieldId::None;
            num_input_buffer_.clear();
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đã đổi ngưỡng TAC thủ công · chưa gắn với profile ICC"
                : "Manual TAC limit changed · not bound to an ICC profile";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        } catch (...) {
            settings_tac_error_ = language_ == UILanguage::Vietnamese
                ? "Ngưỡng TAC phải là số nguyên từ 100 đến 400%"
                : "TAC limit must be a whole number from 100 to 400%";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }

    if (active_num_field_ == NumericFieldId::GridSpacing) {
        double spacing_mm = 0.0;
        try {
            std::string normalized = num_input_buffer_;
            std::replace(normalized.begin(), normalized.end(), ',', '.');
            spacing_mm = parse_length_input_mm(normalized, display_length_unit_);
            if (!tools::is_valid_grid_spacing_mm(spacing_mm))
                throw std::out_of_range("grid interval");
        } catch (...) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Khoảng lưới phải từ 0,001 mm đến 1.000.000.000 mm; thêm hậu tố nếu khác đơn vị trang"
                : "Grid interval must be 0.001–1,000,000,000 mm; add a suffix to override the page unit";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        std::string preference_error;
        if (!drawing_tool_ ||
            !drawing_tool_->set_grid_spacing_mm(spacing_mm, &preference_error)) {
            status_msg_ = preference_error.empty()
                ? (language_ == UILanguage::Vietnamese
                    ? "Không thể lưu khoảng lưới" : "Could not save the grid interval")
                : preference_error;
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã lưu khoảng lưới: " +
                format_mm_display_utf8(spacing_mm, display_length_unit_, 6)
            : "Grid interval saved: " +
                format_mm_display_utf8(spacing_mm, display_length_unit_, 6);
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (active_num_field_ == NumericFieldId::ArtboardX ||
        active_num_field_ == NumericFieldId::ArtboardY ||
        active_num_field_ == NumericFieldId::ArtboardWidth ||
        active_num_field_ == NumericFieldId::ArtboardHeight ||
        active_num_field_ == NumericFieldId::SnapAcquireRadius ||
        active_num_field_ == NumericFieldId::SnapReleaseRadius) {
        (void)commit_artboard_numeric_field(active_num_field_);
        return;
    }

    if (active_num_field_ == text_numeric_field(TextPropertyField::FontSize) ||
         active_num_field_ == text_numeric_field(TextPropertyField::FrameWidth) ||
         active_num_field_ == text_numeric_field(TextPropertyField::LineSpacing)) {
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
        double value = 0.0;
        try {
            std::string normalized = num_input_buffer_;
            std::replace(normalized.begin(), normalized.end(), ',', '.');
            size_t parsed = 0;
            value = frame ? parse_length_input_mm(normalized, display_length_unit_) : std::stod(normalized, &parsed);
            const double maximum = font_size ? 2880.0
                : frame ? geometry::limits::kCanvasMaxMm : 1000.0;
            const double minimum = font_size ? 1.0 : frame ? 0.0 : 25.0;
            if ((!frame && parsed != normalized.size()) || !std::isfinite(value) ||
                value < minimum || value > maximum)
                throw std::out_of_range("text layout field");
        } catch (...) {
            status_msg_ = font_size
                ? (language_ == UILanguage::Vietnamese
                    ? "Cỡ chữ phải từ 1 đến 2880 pt"
                    : "Font size must be between 1 and 2880 pt")
                : frame
                ? (language_ == UILanguage::Vietnamese ? "Khung chữ phải từ " : "Text frame width must be between ") +
                    format_mm_display_utf8(0.0, display_length_unit_, 5) + " and " +
                    format_mm_display_utf8(geometry::limits::kCanvasMaxMm, display_length_unit_, 5)
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
                vector::TextPropertiesDelta delta;
                delta.size_pt = value;
                applied = view.editable && workflow().update_selected_text_properties(delta);
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
        (active_num_field_ == NumericFieldId::ContourSpacing ||
         active_num_field_ == NumericFieldId::ContourSteps ||
         active_num_field_ == NumericFieldId::ContourMiterLimit ||
         active_num_field_ == NumericFieldId::ContourTolerance)) {
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
            if (field == NumericFieldId::ContourSteps) {
                size_t parsed = 0;
                const int value = std::stoi(normalized, &parsed);
                if (parsed != normalized.size() || value < 1 || value > 99)
                    throw std::out_of_range("contour steps");
                contour_tool_->set_steps(value);
            } else {
                size_t parsed = 0;
                const bool length_field = numeric_field_is_length(field);
                const double value = length_field ? parse_length_input_mm(normalized, display_length_unit_)
                                                  : std::stod(normalized, &parsed);
                if ((!length_field && parsed != normalized.size()) || !std::isfinite(value))
                    throw std::out_of_range("contour value");
                if (field == NumericFieldId::ContourSpacing) {
                    if (value < 0.1 || value > 100000.0)
                        throw std::out_of_range("contour spacing");
                    contour_tool_->set_spacing_mm(value);
                } else if (field == NumericFieldId::ContourTolerance) {
                    if (!contour_tool_->set_arc_tolerance_mm(value))
                        throw std::out_of_range("contour arc tolerance");
                } else {
                    if (value < 1.0 || value > 100.0)
                        throw std::out_of_range("contour miter limit");
                    contour_tool_->set_miter_limit(value);
                }
            }
        } catch (...) {
            const auto spacing_range = format_mm_display_utf8(0.1, display_length_unit_, 5) +
                "–" + format_mm_display_utf8(100000.0, display_length_unit_, 5);
            const auto tolerance_range = format_mm_display_utf8(
                geometry::kContourMinimumArcToleranceMm, display_length_unit_, 5) +
                "–" + format_mm_display_utf8(1.0, display_length_unit_, 5);
            if (field == NumericFieldId::ContourSpacing)
                status_msg_ = (language_ == UILanguage::Vietnamese
                    ? "Khoảng cách Contour phải từ " : "Contour spacing must be ") + spacing_range;
            else if (field == NumericFieldId::ContourSteps)
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Số bước phải là số nguyên từ 1 đến 99"
                    : "Steps must be a whole number from 1 to 99";
            else if (field == NumericFieldId::ContourTolerance)
                status_msg_ = (language_ == UILanguage::Vietnamese
                    ? "Arc tolerance phải từ " : "Contour arc tolerance must be ") + tolerance_range;
            else
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Miter limit phải từ 1 đến 100"
                    : "Miter limit must be 1–100";
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
        (active_num_field_ == NumericFieldId::GradientStopOffset ||
         active_num_field_ == NumericFieldId::GradientStopOpacity ||
         active_num_field_ == NumericFieldId::GradientStopMidpoint ||
         active_num_field_ == NumericFieldId::GradientAngle ||
         active_num_field_ == NumericFieldId::GradientLength ||
         active_num_field_ == NumericFieldId::GradientCenterX ||
         active_num_field_ == NumericFieldId::GradientCenterY)) {
        const auto field = active_num_field_;
        auto& presenter = gradient_tool_->presenter();
        const auto* paint = presenter.draft_paint();
        std::string normalized = num_input_buffer_;
        std::replace(normalized.begin(), normalized.end(), ',', '.');
        try {
            size_t parsed = 0;
            const bool length_field = numeric_field_is_length(field);
            const double value = length_field ? parse_length_input_mm(normalized, display_length_unit_)
                                              : std::stod(normalized, &parsed);
            if ((!length_field && parsed != normalized.size()) || !std::isfinite(value))
                throw std::out_of_range("gradient numeric field");
            const auto& current_targets = root().vector_controller().selection().selected_ids();
            if (current_targets != gradient_tool_->target_objects() || !paint)
                throw std::runtime_error("gradient target changed");

            bool applied = false;
            if (field == NumericFieldId::GradientStopOffset ||
                field == NumericFieldId::GradientStopOpacity ||
                field == NumericFieldId::GradientStopMidpoint) {
                const uint64_t stop_id = presenter.selected_stop_id();
                const auto stop = std::find_if(paint->stops.begin(), paint->stops.end(),
                    [stop_id](const auto& item) { return item.id == stop_id; });
                if (stop == paint->stops.end())
                    throw std::runtime_error("gradient stop changed");
                const size_t index = static_cast<size_t>(stop - paint->stops.begin());
                if (field == NumericFieldId::GradientStopOffset && value >= 0.0 && value <= 100.0)
                    applied = presenter.set_stop_offset(index, value / 100.0);
                else if (field == NumericFieldId::GradientStopOpacity && value >= 0.0 && value <= 100.0)
                    applied = presenter.set_stop_opacity(index, value / 100.0);
                else if (field == NumericFieldId::GradientStopMidpoint && value >= 5.0 && value <= 95.0 &&
                         index + 1 < paint->stops.size())
                    applied = presenter.set_segment_midpoint(index, value / 100.0);
                else
                    throw std::out_of_range("gradient stop field range");
                (void)presenter.select_stop(stop_id);
            } else if (field == NumericFieldId::GradientAngle) {
                if (value < -36000.0 || value > 36000.0)
                    throw std::out_of_range("gradient angle range");
                presenter.set_angle_degrees(value);
                applied = true;
            } else if (field == NumericFieldId::GradientLength) {
                if (value < 0.001 || value > geometry::limits::kCanvasMaxMm)
                    throw std::out_of_range("gradient length range");
                presenter.set_length_mm(value);
                applied = true;
            } else {
                const double limit = geometry::limits::kCanvasMaxMm;
                if (value < -limit || value > limit)
                    throw std::out_of_range("gradient center range");
                auto center = presenter.geometry_projection().center_mm;
                if (field == NumericFieldId::GradientCenterX) center.x = value;
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
            const auto center_limit = format_mm_display_utf8(
                geometry::limits::kCanvasMaxMm, display_length_unit_, 5);
            status_msg_ = (language_ == UILanguage::Vietnamese
                ? "Giá trị Gradient không hợp lệ · %: 0–100, midpoint: 5–95, góc: ±36000°, tâm: ±"
                : "Invalid Gradient value · %: 0–100, midpoint: 5–95, angle: ±36000°, center: ±") +
                center_limit;
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (presenter.draft_paint())
            (void)gradient_tool_->tool().set_draft_paint(*presenter.draft_paint());
        if (!gradient_tool_->apply()) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Không thể lưu chỉnh sửa Gradient · bản nháp được giữ lại"
                : "Could not save Gradient edit · draft is preserved";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã lưu chỉnh sửa Gradient · Ctrl+Z để hoàn tác"
            : "Saved Gradient edit · Ctrl+Z to undo";
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
            const double value = parse_length_input_mm(text, display_length_unit_);
            const double limit = geometry::kMaxCanvasDimensionMm * 0.5;
            if (!std::isfinite(value) ||
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
            const auto coordinate_limit = format_mm_display_utf8(
                geometry::kMaxCanvasDimensionMm * 0.5, display_length_unit_, 5);
            status_msg_ = (language_ == UILanguage::Vietnamese
                ? "Tọa độ phải hữu hạn trong khoảng −" : "Coordinate must be finite within −") +
                coordinate_limit + " to " + coordinate_limit;
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
        const auto preview = pen_tool_->update_numeric_candidate(num_input_buffer_,
            vector::precision::numeric::ParseOptions{'.', display_length_unit_});
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
        double spacing_value = 0.0;
        try {
            std::string normalized = num_input_buffer_;
            std::replace(normalized.begin(), normalized.end(), ',', '.');
            spacing_value = parse_length_input_mm(normalized, display_length_unit_);
            if (!std::isfinite(spacing_value) || spacing_value < 0.0 ||
                spacing_value > geometry::kMaxCanvasDimensionMm) {
                status_msg_ = (language_ == UILanguage::Vietnamese
                    ? "Khoảng hở phải từ 0 đến " : "Gap must be between 0 and ") +
                    format_mm_display_utf8(geometry::kMaxCanvasDimensionMm, display_length_unit_, 5);
                if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
            distribute_spacing_mm_ = spacing_value;
        } catch (...) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Nhập khoảng hở hợp lệ theo đơn vị trang hoặc ghi hậu tố đơn vị"
                : "Enter a valid gap in the page unit or add a unit suffix";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        active_num_field_ = NumericFieldId::None;
        num_input_buffer_.clear();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã cập nhật khoảng hở cố định: " + format_mm_display_utf8(spacing_value, display_length_unit_, 5)
            : "Updated fixed gap: " + format_mm_display_utf8(spacing_value, display_length_unit_, 5);
#ifdef _WIN32
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
        return;
    }

    if (active_num_field_ == NumericFieldId::BlurSigmaX ||
        active_num_field_ == NumericFieldId::BlurSigmaY ||
        active_num_field_ == NumericFieldId::ShadowFeatherX ||
        active_num_field_ == NumericFieldId::ShadowFeatherY ||
        active_num_field_ == NumericFieldId::ShadowOffsetX ||
        active_num_field_ == NumericFieldId::ShadowOffsetY ||
        active_num_field_ == NumericFieldId::ShadowOpacity) {
        const auto field = active_num_field_;
        try {
            std::string normalized = num_input_buffer_;
            std::replace(normalized.begin(), normalized.end(), ',', '.');
            size_t parsed = 0;
            const bool length_field = numeric_field_is_length(field);
            const double value = length_field ? parse_length_input_mm(normalized, display_length_unit_)
                                              : std::stod(normalized, &parsed);
            if ((!length_field && parsed != normalized.size()) || !std::isfinite(value))
                throw std::out_of_range("appearance parameter");

            const bool opacity = field == NumericFieldId::ShadowOpacity;
            const bool offset = field == NumericFieldId::ShadowOffsetX ||
                                field == NumericFieldId::ShadowOffsetY;
            const bool in_range = opacity ? value >= 0.0 && value <= 100.0
                : offset ? std::abs(value) <= geometry::limits::kCanvasMaxMm
                         : value >= 0.0 && value <= geometry::limits::kCanvasMaxMm;
            if (!in_range)
                throw std::out_of_range("appearance parameter range");

            const auto &current_targets = root().vector_controller().selection().selected_ids();
            const auto current_snapshot = root().document().current_snapshot();
            const auto current_summary = current_snapshot
                ? selection_appearance(*current_snapshot, current_targets) : SelectionAppearance{};
            bool correct_type = false;
            if (current_summary.can_edit_effect(numeric_effect_index_)) {
                const auto &params = current_summary.effects[numeric_effect_index_].parameters;
                if (field == NumericFieldId::BlurSigmaX) {
                    correct_type = std::holds_alternative<document::GaussianBlurEffect>(params) ||
                                   std::holds_alternative<document::FeatherEffect>(params) ||
                                   std::holds_alternative<document::OuterGlowEffect>(params) ||
                                   std::holds_alternative<document::InnerGlowEffect>(params);
                } else if (field == NumericFieldId::BlurSigmaY) {
                    correct_type = std::holds_alternative<document::GaussianBlurEffect>(params);
                } else if (field == NumericFieldId::ShadowFeatherX || field == NumericFieldId::ShadowFeatherY ||
                           field == NumericFieldId::ShadowOffsetX || field == NumericFieldId::ShadowOffsetY) {
                    correct_type = std::holds_alternative<document::DropShadowEffect>(params) ||
                                   std::holds_alternative<document::InnerShadowEffect>(params);
                } else if (field == NumericFieldId::ShadowOpacity) {
                    correct_type = std::holds_alternative<document::DropShadowEffect>(params) ||
                                   std::holds_alternative<document::InnerShadowEffect>(params) ||
                                   std::holds_alternative<document::OuterGlowEffect>(params) ||
                                   std::holds_alternative<document::InnerGlowEffect>(params) ||
                                   std::holds_alternative<document::ColorOverlayEffect>(params);
                }
            }
            bool applied = current_targets == numeric_effect_targets_ && correct_type &&
                workflow().can_edit_selected_effect(numeric_effect_index_);
            if (applied) {
                if (field == NumericFieldId::BlurSigmaX || field == NumericFieldId::ShadowFeatherX)
                    applied = workflow().set_selected_effect_sigma(numeric_effect_index_, true, value);
                else if (field == NumericFieldId::BlurSigmaY || field == NumericFieldId::ShadowFeatherY)
                    applied = workflow().set_selected_effect_sigma(numeric_effect_index_, false, value);
                else if (field == NumericFieldId::ShadowOffsetX)
                    applied = workflow().set_selected_effect_offset(numeric_effect_index_, true, value);
                else if (field == NumericFieldId::ShadowOffsetY)
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
            const auto limit = format_mm_display_utf8(
                geometry::limits::kCanvasMaxMm, display_length_unit_, 5);
            status_msg_ = (language_ == UILanguage::Vietnamese
                ? "Giá trị không hợp lệ · Sigma/Feather: 0–" : "Invalid value · Sigma/Feather: 0–") +
                limit + (language_ == UILanguage::Vietnamese ? "; lệch: ±" : "; offset: ±") +
                limit + "; opacity: 0–100%";
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
            const auto field = active_num_field_;
            const bool length_field = numeric_field_is_length(field);
            const auto pixel_dpi = [&]() -> std::optional<double> {
                if (field != NumericFieldId::ObjectWidth && field != NumericFieldId::ObjectHeight)
                    return std::nullopt;
                const auto& ids = root().vector_controller().selection().selected_ids();
                if (ids.size() != 1) return std::nullopt;
                const auto snapshot = root().document().current_snapshot();
                if (!snapshot) return std::nullopt;
                const auto object = snapshot->objects().get(ids.front());
                if (!object) return std::nullopt;
                const auto* image = std::get_if<document::ImageObject>(&*object);
                if (!image) return std::nullopt;
                return image_source_axis_dpi(image->source.dpi_x, image->source.dpi_y,
                    image->source.dpi_from_source, image->source.exif_orientation,
                    image->source.orientation_applied, field == NumericFieldId::ObjectWidth);
            }();
            size_t parsed = 0;
            const double val = length_field ? parse_length_input_mm(
                                                  num_input_buffer_, display_length_unit_, pixel_dpi)
                                            : std::stod(num_input_buffer_, &parsed);
            if (!length_field && parsed != num_input_buffer_.size())
                throw std::invalid_argument("Invalid scalar input");

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
                        const auto bounds_opt = workflow().get_object_bounds(ids.front());
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
                                sync_rect_corner_controls_from_selection();
                            } else if (field == NumericFieldId::ObjectHeight) {
                                double new_h = std::clamp(val, 1.0, geometry::kMaxObjectDimensionMm);
                                double new_w = bounds_opt->width;
                                if (aspect_ratio_locked_ && bounds_opt->height > 0.001) {
                                    new_w = std::clamp(bounds_opt->width * (new_h / bounds_opt->height), 1.0, geometry::kMaxObjectDimensionMm);
                                }
                                workflow().resize_object(id, new_w, new_h);
                                sync_rect_corner_controls_from_selection();
                            } else if (field == NumericFieldId::ObjectRotation) {
                                const auto transform = snapshot->hierarchy().find(ids.front())->world;
                                double cur_deg = std::atan2(transform.b, transform.a) * (180.0 / 3.14159265358979323846);
                                if (cur_deg < 0.0) cur_deg += 360.0;
                                double target_deg = std::fmod(val, 360.0);
                                if (target_deg < 0.0) target_deg += 360.0;
                                double delta_deg = target_deg - cur_deg;
                                workflow().rotate_selected_degrees(delta_deg, custom_pivot_world_);
                            }
                        }
                    }
                }
            }
        } catch (...) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Giá trị hoặc hậu tố không hợp lệ · kiểm tra ngữ cảnh DPI, sửa hoặc Esc để hủy"
                : "Invalid value or unit suffix · check the DPI context, edit or press Esc to cancel";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
    }

    active_num_field_ = NumericFieldId::None;
    num_input_buffer_.clear();

#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}


} // namespace pikdraw::desktop
