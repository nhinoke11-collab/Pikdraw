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
#include "../tools/annotation/annotation_tool_controller.hpp"
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
#include <string_view>

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

void NativeWindow::start_numeric_input(NumericFieldId field) {
    if (active_num_field_ == field)
        return;
    const std::string page_unit(display_length_unit_suffix(display_length_unit_));
    const auto image_pixel_dpi = [&]() -> std::optional<double> {
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
    const bool physical_length_field = numeric_field_is_length(field);
    const ScopeExit format_seed_for_display_unit{[this, field, physical_length_field, page_unit] {
        if (!physical_length_field || active_num_field_ != field || num_input_buffer_.empty()) return;
        const auto ends_with = [this](std::string_view suffix) {
            return num_input_buffer_.size() >= suffix.size() &&
                num_input_buffer_.compare(num_input_buffer_.size() - suffix.size(), suffix.size(), suffix) == 0;
        };
        const bool has_explicit_unit = ends_with("mm") || ends_with("cm") || ends_with("in") ||
            ends_with("ft") || ends_with("yd") || ends_with("pt") || ends_with("pc") || ends_with("px") ||
            ends_with("m");
        if (has_explicit_unit) return;
        try {
            size_t parsed = 0;
            const double seeded_mm = std::stod(num_input_buffer_, &parsed);
            if (parsed == num_input_buffer_.size() && std::isfinite(seeded_mm)) {
                num_input_buffer_ = format_mm_input(seeded_mm, display_length_unit_);
                return;
            }
        } catch (...) {}
        num_input_buffer_ += page_unit;
    }};
    const ScopeExit capture_numeric_context{[this, field] {
        if (active_num_field_ != field) return;
        auto& doc = root().document();
        const auto snapshot = doc.current_snapshot();
        numeric_input_context_ = {};
        numeric_input_context_.root_identity = static_cast<const void*>(&root());
        numeric_input_context_.page_id = snapshot ? snapshot->active_page_id() : 0;
        numeric_input_context_.document_revision = doc.revision();
        numeric_input_context_.view_generation = view_generation_;
        numeric_input_context_.active_tool = active_tool_;
        numeric_input_context_.selection_ids = root().vector_controller().selection().selected_ids();
        if (field == NumericFieldId::ContourSpacing || field == NumericFieldId::ContourSteps ||
            field == NumericFieldId::ContourTolerance || field == NumericFieldId::ContourMiterLimit) {
            numeric_input_context_.contour_target = contour_tool_
                ? contour_tool_->target_object() : document::kInvalidObjectId;
        }
        if (field == NumericFieldId::GradientStopOffset || field == NumericFieldId::GradientStopOpacity ||
            field == NumericFieldId::GradientStopMidpoint || field == NumericFieldId::GradientAngle ||
            field == NumericFieldId::GradientLength || field == NumericFieldId::GradientCenterX ||
            field == NumericFieldId::GradientCenterY) {
            numeric_input_context_.gradient_stop_id = gradient_tool_ && gradient_tool_->active()
                ? gradient_tool_->presenter().selected_stop_id() : 0;
        }
        if (field == NumericFieldId::ArtboardX || field == NumericFieldId::ArtboardY ||
            field == NumericFieldId::ArtboardWidth || field == NumericFieldId::ArtboardHeight)
            numeric_input_context_.artboard_id = active_tab().active_artboard_id;
        numeric_input_context_.valid = true;
    }};
    if (active_num_field_ != NumericFieldId::None && active_num_field_ != field) {
        commit_numeric_field();
        if (active_num_field_ != NumericFieldId::None)
            return;
    }
    if (field == NumericFieldId::KnifeGap ||
        field == NumericFieldId::VectorEraserDiameter) {
        const bool knife_field = field == NumericFieldId::KnifeGap;
        if ((knife_field && active_tool_ != 5) || (!knife_field && active_tool_ != 16))
            return;
        active_num_field_ = field;
        num_input_buffer_ = format_mm_input(
            knife_field ? knife_gap_mm_ : vector_eraser_diameter_mm_, display_length_unit_);
        status_msg_ = knife_field
            ? (language_ == UILanguage::Vietnamese
                ? "Khoảng hở Knife: 0–1 mm · Enter áp dụng · Esc hủy"
                : "Knife gap: 0–1 mm · Enter applies · Esc cancels")
            : (language_ == UILanguage::Vietnamese
                ? "Đường kính Tẩy: 0,01–1000 mm · Enter áp dụng · Esc hủy"
                : "Eraser diameter: 0.01–1000 mm · Enter applies · Esc cancels");
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if ((field == text_numeric_field(TextPropertyField::FontSize) ||
         field == text_numeric_field(TextPropertyField::FrameWidth) ||
         field == text_numeric_field(TextPropertyField::LineSpacing)) &&
        active_dock_tab_ == 0 && has_text_properties_target(*this)) {
        const auto view = text_properties_view(*this);
        if (!view.editable) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Văn bản bị khóa hoặc ẩn · không thể sửa thuộc tính"
                : "Text is locked or hidden · properties cannot be edited";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        active_num_field_ = field;
        numeric_effect_targets_ = root().vector_controller().selection().selected_ids();
        num_input_buffer_.clear();
        if (field == text_numeric_field(TextPropertyField::FontSize))
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Cỡ chữ 1–2880 pt · Enter áp dụng · Esc hủy"
                : "Font size 1–2880 pt · Enter applies · Esc cancels";
        else if (field == text_numeric_field(TextPropertyField::FrameWidth))
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Khung chữ: số trần theo " + page_unit + "; hậu tố rõ ghi đè · 0 = chữ tự do"
                : "Text frame: bare value uses " + page_unit + "; explicit suffix overrides · 0 = point text";
        else
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Giãn dòng 25–1000% · Enter áp dụng · Esc hủy"
                : "Line spacing 25–1000% · Enter applies · Esc cancels";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (field == NumericFieldId::RasterExposureEv && !can_edit_selected_bitmap(true)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Exposure EV cần một ảnh sRGB không khóa; xử lý profile nhúng trước"
            : "Exposure EV needs one unlocked sRGB bitmap; resolve its embedded profile first";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const bool color_balance_field =
        field == NumericFieldId::RasterColorBalanceRedCyan ||
        field == NumericFieldId::RasterColorBalanceGreenMagenta ||
        field == NumericFieldId::RasterColorBalanceBlueYellow;
    const bool raster_light_field =
        field == NumericFieldId::RasterLightContrast ||
        field == NumericFieldId::RasterLightHighlights ||
        field == NumericFieldId::RasterLightShadows ||
        field == NumericFieldId::RasterLightWhites ||
        field == NumericFieldId::RasterLightBlacks;
    const bool raster_color_adjustment_field =
        field == NumericFieldId::RasterHslHue ||
        field == NumericFieldId::RasterHslSaturation ||
        field == NumericFieldId::RasterHslLightness ||
        field == NumericFieldId::RasterVibranceAmount;
    const bool raster_levels_field =
        field == NumericFieldId::RasterLevelsBlackIn ||
        field == NumericFieldId::RasterLevelsWhiteIn ||
        field == NumericFieldId::RasterLevelsGamma ||
        field == NumericFieldId::RasterLevelsBlackOut ||
        field == NumericFieldId::RasterLevelsWhiteOut;
    const bool raster_white_balance_field =
        field == NumericFieldId::RasterWhiteBalanceTemperature ||
        field == NumericFieldId::RasterWhiteBalanceTint;
    const bool raster_curves_field =
        field == NumericFieldId::RasterCurvePoint0 ||
        field == NumericFieldId::RasterCurvePoint1 ||
        field == NumericFieldId::RasterCurvePoint2 ||
        field == NumericFieldId::RasterCurvePoint3 ||
        field == NumericFieldId::RasterCurvePoint4;
    if (color_balance_field && !can_edit_selected_bitmap(true)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Color Balance cần một ảnh RGB sRGB có nguồn được giữ lại"
            : "Color Balance needs one RGB sRGB image with a preserved source";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (raster_light_field && !can_edit_selected_bitmap(true)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Điều chỉnh Ánh sáng cần một ảnh sRGB không khóa"
            : "Light adjustments require one unlocked sRGB bitmap";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (raster_color_adjustment_field && !can_edit_selected_bitmap(true)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "HSL/Vibrance cần một ảnh sRGB không khóa và có nguồn được giữ lại"
            : "HSL/Vibrance needs one unlocked sRGB bitmap with a preserved source";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (raster_levels_field && !can_edit_selected_bitmap(true)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Levels cần một ảnh RGB sRGB không khóa"
            : "Levels requires one unlocked RGB sRGB bitmap";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (raster_white_balance_field && !can_edit_selected_bitmap(true)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Cân bằng trắng cần một ảnh RGB sRGB không khóa"
            : "White Balance requires one unlocked RGB sRGB bitmap";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (raster_curves_field && !can_edit_selected_bitmap(true)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Curves cần một ảnh RGB sRGB không khóa"
            : "Curves requires one unlocked RGB sRGB bitmap";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (active_tool_ == 12 &&
        (field == NumericFieldId::ContourSpacing ||
         field == NumericFieldId::ContourSteps ||
         field == NumericFieldId::ContourMiterLimit ||
         field == NumericFieldId::ContourTolerance)) {
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
        value << std::setprecision(std::numeric_limits<double>::max_digits10);
        if (field == NumericFieldId::ContourSpacing) value << contour_tool_->spacing_mm();
        else if (field == NumericFieldId::ContourSteps) value << contour_tool_->steps();
        else if (field == NumericFieldId::ContourTolerance) value << contour_tool_->arc_tolerance_mm();
        else value << contour_tool_->miter_limit();
        num_input_buffer_ = value.str();
        if (field == NumericFieldId::ContourSpacing || field == NumericFieldId::ContourTolerance)
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Contour · số trần theo " + page_unit + "; hậu tố rõ ghi đè · Enter xem trước · Tab trường kế"
                : "Contour · bare lengths use " + page_unit + "; explicit suffix overrides · Enter previews · Tab next field";
        else if (field == NumericFieldId::ContourSteps)
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Contour · số bước nguyên từ 1 đến 99 · Enter xem trước"
                : "Contour · whole step count from 1 to 99 · Enter previews";
        else
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Contour · giới hạn góc miter 1–100 · Enter xem trước"
                : "Contour · miter limit 1–100 · Enter previews";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (active_tool_ == 13 &&
        (field == NumericFieldId::GradientStopOffset || field == NumericFieldId::GradientStopOpacity ||
         field == NumericFieldId::GradientStopMidpoint || field == NumericFieldId::GradientAngle ||
         field == NumericFieldId::GradientLength || field == NumericFieldId::GradientCenterX ||
         field == NumericFieldId::GradientCenterY)) {
        if (!gradient_tool_ || !gradient_tool_->active())
            return;
        auto& presenter = gradient_tool_->presenter();
        const auto& summary = presenter.summary();
        const auto* paint = presenter.draft_paint();
        const bool stop_field = field == NumericFieldId::GradientStopOffset ||
            field == NumericFieldId::GradientStopOpacity ||
            field == NumericFieldId::GradientStopMidpoint;
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
            (field == NumericFieldId::GradientStopMidpoint && stop_index + 1 >= paint->stops.size()))) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Chọn một stop có đoạn kế tiếp để sửa trung điểm"
                : "Select a stop with a following segment to edit its midpoint";
#ifdef _WIN32
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
            return;
        }
        const bool geometry_field = field == NumericFieldId::GradientAngle ||
            field == NumericFieldId::GradientLength || field == NumericFieldId::GradientCenterX ||
            field == NumericFieldId::GradientCenterY;
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
        if (field == NumericFieldId::GradientStopOffset)
            value << paint->stops[stop_index].offset * 100.0;
        else if (field == NumericFieldId::GradientStopOpacity)
            value << paint->stops[stop_index].opacity * 100.0;
        else if (field == NumericFieldId::GradientStopMidpoint)
            value << paint->stops[stop_index].midpoint_to_next * 100.0;
        else {
            value << std::defaultfloat << std::setprecision(std::numeric_limits<double>::max_digits10);
            const auto projection = presenter.geometry_projection();
            const bool mixed = (field == NumericFieldId::GradientAngle && summary.mixed_angle) ||
                               (field == NumericFieldId::GradientLength && summary.mixed_length);
            if (!mixed) {
                if (field == NumericFieldId::GradientAngle) value << projection.angle_degrees;
                else if (field == NumericFieldId::GradientLength) value << projection.length_mm;
                else if (field == NumericFieldId::GradientCenterX) value << projection.center_mm.x;
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
    const bool effect_numeric = field == NumericFieldId::BlurSigmaX ||
        field == NumericFieldId::BlurSigmaY || field == NumericFieldId::ShadowFeatherX ||
        field == NumericFieldId::ShadowFeatherY || field == NumericFieldId::ShadowOffsetX ||
        field == NumericFieldId::ShadowOffsetY || field == NumericFieldId::ShadowOpacity;
    if (effect_numeric) {
        const auto snapshot = root().document().current_snapshot();
        const auto selection = root().vector_controller().selection().selected_ids();
        const auto appearance = snapshot
            ? selection_appearance(*snapshot, selection) : SelectionAppearance{};
        bool field_supported = false;
        if (appearance.can_edit_effect(property_effect_index_)) {
            const auto &params = appearance.effects[property_effect_index_].parameters;
            if (field == NumericFieldId::BlurSigmaX) {
                field_supported = std::holds_alternative<document::GaussianBlurEffect>(params) ||
                                  std::holds_alternative<document::FeatherEffect>(params) ||
                                  std::holds_alternative<document::OuterGlowEffect>(params) ||
                                  std::holds_alternative<document::InnerGlowEffect>(params);
            } else if (field == NumericFieldId::BlurSigmaY) {
                field_supported = std::holds_alternative<document::GaussianBlurEffect>(params);
            } else if (field == NumericFieldId::ShadowFeatherX || field == NumericFieldId::ShadowFeatherY ||
                       field == NumericFieldId::ShadowOffsetX || field == NumericFieldId::ShadowOffsetY) {
                field_supported = std::holds_alternative<document::DropShadowEffect>(params) ||
                                  std::holds_alternative<document::InnerShadowEffect>(params);
            } else if (field == NumericFieldId::ShadowOpacity) {
                field_supported = std::holds_alternative<document::DropShadowEffect>(params) ||
                                  std::holds_alternative<document::InnerShadowEffect>(params) ||
                                  std::holds_alternative<document::OuterGlowEffect>(params) ||
                                  std::holds_alternative<document::InnerGlowEffect>(params) ||
                                  std::holds_alternative<document::ColorOverlayEffect>(params);
            }
        }
        if (!workflow().can_edit_selected_effect(property_effect_index_) || !field_supported) {
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
    ss << std::setprecision(std::numeric_limits<double>::max_digits10);

    if (field == NumericFieldId::PageWidth) {
        ss << active_tab().page_width_mm;
        num_input_buffer_ = ss.str();
    } else if (field == NumericFieldId::PageHeight) {
        ss << active_tab().page_height_mm;
        num_input_buffer_ = ss.str();
    } else if (field == NumericFieldId::RasterExposureEv) {
        const auto value = selected_bitmap_exposure_ev();
        if (!value) {
            active_num_field_ = NumericFieldId::None;
            return;
        }
        ss << *value;
        num_input_buffer_ = ss.str();
    } else if (field == NumericFieldId::RasterColorBalanceRedCyan ||
               field == NumericFieldId::RasterColorBalanceGreenMagenta ||
               field == NumericFieldId::RasterColorBalanceBlueYellow) {
        const auto parameters = selected_bitmap_color_balance();
        if (field == NumericFieldId::RasterColorBalanceRedCyan)
            ss << parameters.red_cyan_percent;
        else if (field == NumericFieldId::RasterColorBalanceGreenMagenta)
            ss << parameters.green_magenta_percent;
        else
            ss << parameters.blue_yellow_percent;
        num_input_buffer_ = ss.str();
    } else if (raster_light_field) {
        const auto parameters = selected_bitmap_light();
        if (field == NumericFieldId::RasterLightContrast)
            ss << parameters.contrast_percent;
        else if (field == NumericFieldId::RasterLightHighlights)
            ss << parameters.highlights_percent;
        else if (field == NumericFieldId::RasterLightShadows)
            ss << parameters.shadows_percent;
        else if (field == NumericFieldId::RasterLightWhites)
            ss << parameters.whites_percent;
        else
            ss << parameters.blacks_percent;
        num_input_buffer_ = ss.str();
    } else if (raster_color_adjustment_field) {
        if (field == NumericFieldId::RasterVibranceAmount) {
            ss << selected_bitmap_vibrance_amount();
        } else if (raster_hsl_active_channel_ > 0) {
            const auto selective = selected_bitmap_selective_hsl(
                static_cast<document::RasterHueRange>(
                    raster_hsl_active_channel_ - 1));
            if (field == NumericFieldId::RasterHslHue)
                ss << selective.hue_degrees;
            else if (field == NumericFieldId::RasterHslSaturation)
                ss << selective.saturation_percent;
            else
                ss << selective.lightness_percent;
        } else {
            const auto parameters = selected_bitmap_hsl();
            if (field == NumericFieldId::RasterHslHue)
                ss << parameters.hue_degrees;
            else if (field == NumericFieldId::RasterHslSaturation)
                ss << parameters.saturation_percent;
            else
                ss << parameters.lightness_percent;
        }
        num_input_buffer_ = ss.str();
    } else if (raster_levels_field) {
        const auto parameters = selected_bitmap_levels();
        if (field == NumericFieldId::RasterLevelsBlackIn)
            ss << parameters.black_in * 100.0;
        else if (field == NumericFieldId::RasterLevelsWhiteIn)
            ss << parameters.white_in * 100.0;
        else if (field == NumericFieldId::RasterLevelsGamma)
            ss << parameters.gamma;
        else if (field == NumericFieldId::RasterLevelsBlackOut)
            ss << parameters.black_out * 100.0;
        else
            ss << parameters.white_out * 100.0;
        num_input_buffer_ = ss.str();
    } else if (raster_white_balance_field) {
        const auto parameters = selected_bitmap_white_balance();
        ss << (field == NumericFieldId::RasterWhiteBalanceTemperature
            ? parameters.temperature_shift_percent : parameters.tint_percent);
        num_input_buffer_ = ss.str();
    } else if (raster_curves_field) {
        const auto parameters = selected_bitmap_curves();
        size_t point = 0;
        switch (field) {
        case NumericFieldId::RasterCurvePoint0: point = 0; break;
        case NumericFieldId::RasterCurvePoint1: point = 1; break;
        case NumericFieldId::RasterCurvePoint2: point = 2; break;
        case NumericFieldId::RasterCurvePoint3: point = 3; break;
        case NumericFieldId::RasterCurvePoint4: point = 4; break;
        default: return;
        }
        if (point >= parameters.output_points.size()) return;
        ss << parameters.output_points[point] * 100.0;
        num_input_buffer_ = ss.str();
    } else if (field == NumericFieldId::PolygonSides) {
        num_input_buffer_ = std::to_string(polygon_sides_);
    } else if (field == NumericFieldId::PolygonRotation) {
        ss << polygon_rotation_degrees_;
        num_input_buffer_ = ss.str();
    } else if (field == NumericFieldId::TacLimitPercent) {
        num_input_buffer_.clear();
    } else if (field == NumericFieldId::ModalWidth) {
        ss << new_doc_w_;
        num_input_buffer_ = ss.str();
    } else if (field == NumericFieldId::ModalHeight) {
        ss << new_doc_h_;
        num_input_buffer_ = ss.str();
    } else if (field == NumericFieldId::DistributeSpacing) {
        ss << distribute_spacing_mm_;
        num_input_buffer_ = ss.str();
    } else if (field == NumericFieldId::AnnotationSourceX ||
               field == NumericFieldId::AnnotationSourceY ||
               field == NumericFieldId::AnnotationSourceWidth ||
               field == NumericFieldId::AnnotationSourceHeight ||
               field == NumericFieldId::AnnotationFrameX ||
               field == NumericFieldId::AnnotationFrameY) {
        std::optional<document::DetailViewObject> detail;
        if (annotation_tool_ && annotation_tool_->active() && active_tool_ == 17 &&
            annotation_tool_->mode() == tools::AnnotationToolMode::DetailView)
            detail = annotation_tool_->draft_detail_view();
        else {
            const auto& ids = root().vector_controller().selection().selected_ids();
            if (ids.size() == 1) detail = document::get_detail_view(root().document(), ids.front());
        }
        if (!detail) { active_num_field_ = NumericFieldId::None; return; }
        const auto& source = detail->source_region;
        double value = 0.0;
        if (field == NumericFieldId::AnnotationSourceX)
            value = detail->shape == document::DetailViewShape::Circle ? source.center().x : source.x;
        else if (field == NumericFieldId::AnnotationSourceY)
            value = detail->shape == document::DetailViewShape::Circle ? source.center().y : source.y;
        else if (field == NumericFieldId::AnnotationSourceWidth)
            value = detail->shape == document::DetailViewShape::Circle ? source.width * 0.5 : source.width;
        else if (field == NumericFieldId::AnnotationSourceHeight)
            value = source.height;
        else if (field == NumericFieldId::AnnotationFrameX)
            value = detail->frame_rect.x;
        else value = detail->frame_rect.y + detail->frame_rect.height;
        num_input_buffer_ = format_mm_input(value, display_length_unit_);
    } else if (field == NumericFieldId::AnnotationScale) {
        if (annotation_tool_ && annotation_tool_->active() && active_tool_ == 17)
            ss << annotation_tool_->scale_ratio();
        else {
            const auto& ids = root().vector_controller().selection().selected_ids();
            const auto value = ids.size() == 1 ? document::get_detail_view(root().document(), ids.front()) : std::nullopt;
            if (!value) { active_num_field_ = NumericFieldId::None; return; }
            ss << value->scale_ratio;
        }
        num_input_buffer_ = ss.str();
    } else if (field == NumericFieldId::AnnotationTextSize) {
        if (annotation_tool_ && annotation_tool_->active() && active_tool_ == 17 &&
            annotation_tool_->mode() != tools::AnnotationToolMode::DetailView)
            ss << annotation_tool_->note_text_size_mm();
        else {
            const auto& ids = root().vector_controller().selection().selected_ids();
            const auto value = ids.size() == 1 ? document::get_annotation(root().document(), ids.front()) : std::nullopt;
            if (!value) { active_num_field_ = NumericFieldId::None; return; }
            ss << value->style.text_size_mm;
        }
        num_input_buffer_ = ss.str();
    } else if (field == NumericFieldId::GridSpacing) {
        const auto snap = drawing_tool_ ? drawing_tool_->grid_snap_status()
                                        : tools::DrawingToolGridSnapStatus{};
        ss << snap.spacing_mm;
        num_input_buffer_ = ss.str();
    } else if (field == NumericFieldId::ArtboardX || field == NumericFieldId::ArtboardY ||
               field == NumericFieldId::ArtboardWidth || field == NumericFieldId::ArtboardHeight) {
        const auto snapshot = root().document().current_snapshot();
        if (snapshot) {
            const auto& boards = snapshot->active_page().artboards;
            const auto board = std::find_if(boards.begin(), boards.end(), [&](const auto& item) {
                return item.id == active_tab().active_artboard_id;
            });
            if (board != boards.end()) {
                if (field == NumericFieldId::ArtboardX) ss << board->x_mm;
                else if (field == NumericFieldId::ArtboardY) ss << board->y_mm;
                else if (field == NumericFieldId::ArtboardWidth) ss << board->width_mm;
                else ss << board->height_mm;
                num_input_buffer_ = ss.str();
            }
        }
    } else if (field == NumericFieldId::SnapAcquireRadius ||
               field == NumericFieldId::SnapReleaseRadius) {
        const auto config = drawing_tool_ ? drawing_tool_->snap_config_snapshot()
            : vector::precision::snap::SnapConfigSnapshot{};
        ss << (field == NumericFieldId::SnapAcquireRadius
            ? config.acquire_radius_dip : config.release_radius_dip);
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
        bool mixed = false;
        double value = 0.0;
        if (field == NumericFieldId::BlurSigmaX) {
            mixed = effect.mixed_sigma_x;
            if (const auto *b = std::get_if<document::GaussianBlurEffect>(&effect.parameters))
                value = b->sigma_x_mm;
            else if (const auto *f = std::get_if<document::FeatherEffect>(&effect.parameters))
                value = f->radius_mm;
            else if (const auto *og = std::get_if<document::OuterGlowEffect>(&effect.parameters))
                value = og->radius_mm;
            else if (const auto *ig = std::get_if<document::InnerGlowEffect>(&effect.parameters))
                value = ig->radius_mm;
        } else if (field == NumericFieldId::BlurSigmaY) {
            mixed = effect.mixed_sigma_y;
            if (const auto *b = std::get_if<document::GaussianBlurEffect>(&effect.parameters))
                value = b->sigma_y_mm;
        } else if (field == NumericFieldId::ShadowFeatherX) {
            mixed = effect.mixed_sigma_x;
            if (const auto *s = std::get_if<document::DropShadowEffect>(&effect.parameters))
                value = s->sigma_x_mm;
            else if (const auto *is = std::get_if<document::InnerShadowEffect>(&effect.parameters))
                value = is->sigma_x_mm;
        } else if (field == NumericFieldId::ShadowFeatherY) {
            mixed = effect.mixed_sigma_y;
            if (const auto *s = std::get_if<document::DropShadowEffect>(&effect.parameters))
                value = s->sigma_y_mm;
            else if (const auto *is = std::get_if<document::InnerShadowEffect>(&effect.parameters))
                value = is->sigma_y_mm;
        } else if (field == NumericFieldId::ShadowOffsetX) {
            mixed = effect.mixed_offset_x;
            if (const auto *s = std::get_if<document::DropShadowEffect>(&effect.parameters))
                value = s->dx_mm;
            else if (const auto *is = std::get_if<document::InnerShadowEffect>(&effect.parameters))
                value = is->dx_mm;
        } else if (field == NumericFieldId::ShadowOffsetY) {
            mixed = effect.mixed_offset_y;
            if (const auto *s = std::get_if<document::DropShadowEffect>(&effect.parameters))
                value = s->dy_mm;
            else if (const auto *is = std::get_if<document::InnerShadowEffect>(&effect.parameters))
                value = is->dy_mm;
        } else {
            mixed = effect.mixed_opacity;
            if (const auto *s = std::get_if<document::DropShadowEffect>(&effect.parameters))
                value = s->opacity * 100.0;
            else if (const auto *is = std::get_if<document::InnerShadowEffect>(&effect.parameters))
                value = is->opacity * 100.0;
            else if (const auto *og = std::get_if<document::OuterGlowEffect>(&effect.parameters))
                value = og->opacity * 100.0;
            else if (const auto *ig = std::get_if<document::InnerGlowEffect>(&effect.parameters))
                value = ig->opacity * 100.0;
            else if (const auto *co = std::get_if<document::ColorOverlayEffect>(&effect.parameters))
                value = co->opacity * 100.0;
        }
        if (!mixed) {
            std::ostringstream effect_value;
            effect_value << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
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
        ss << std::setprecision(std::numeric_limits<double>::max_digits10) << r;
        num_input_buffer_ = ss.str();
    } else {
        const auto ids = root().vector_controller().selection().selected_ids();
        if (!ids.empty()) {
            const auto snapshot = root().document().current_snapshot();
            const auto obj = snapshot->objects().get(ids.front());
            if (obj) {
                const auto bounds_opt = workflow().get_object_bounds(ids.front());
                if (bounds_opt) {
                    if (field == NumericFieldId::ObjectX) ss << bounds_opt->x;
                    else if (field == NumericFieldId::ObjectY) ss << bounds_opt->y;
                    else if (field == NumericFieldId::ObjectWidth) ss << bounds_opt->width;
                    else if (field == NumericFieldId::ObjectHeight) ss << bounds_opt->height;
                    else if (field == NumericFieldId::ObjectRotation) {
                        const auto transform = snapshot->hierarchy().find(ids.front())->world;
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
            ? "Tọa độ node: số trần theo " + page_unit + "; hậu tố rõ ghi đè · Enter áp dụng · Tab X/Y · Esc hủy"
            : "Node coordinates: bare value uses " + page_unit + "; suffix overrides · Enter applies · Tab X/Y · Esc cancels";
    } else if (field == NumericFieldId::ObjectOpacity) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Nhập Opacity 0–100% (dấu phẩy hoặc chấm) · Enter áp dụng · Esc hủy · Ctrl+Z hoàn tác"
            : "Enter Opacity from 0–100% · comma or dot decimals · Enter applies · Esc cancels · Ctrl+Z undoes";
    } else if (field == NumericFieldId::RasterExposureEv) {
        const double range = selected_bitmap_has_light_node() ? 5.0 : 10.0;
        std::ostringstream range_text;
        range_text << std::fixed << std::setprecision(0) << range;
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Exposure ±" + range_text.str() + " EV · Enter áp dụng · Esc hủy · Ctrl+Z hoàn tác"
            : "Exposure ±" + range_text.str() + " EV · Enter applies · Esc cancels · Ctrl+Z undoes";
    } else if (field == NumericFieldId::RasterColorBalanceRedCyan ||
               field == NumericFieldId::RasterColorBalanceGreenMagenta ||
               field == NumericFieldId::RasterColorBalanceBlueYellow) {
        const char* axis_vi = field == NumericFieldId::RasterColorBalanceRedCyan
            ? "Lục lam ↔ Đỏ (C↔R)"
            : field == NumericFieldId::RasterColorBalanceGreenMagenta
                ? "Xanh lá ↔ Đỏ tía (M↔G)"
                : "Xanh dương ↔ Vàng (Y↔B)";
        const char* axis_en = field == NumericFieldId::RasterColorBalanceRedCyan
            ? "Cyan ↔ Red (C↔R)"
            : field == NumericFieldId::RasterColorBalanceGreenMagenta
                ? "Green ↔ Magenta (M↔G)"
                : "Blue ↔ Yellow (Y↔B)";
        status_msg_ = language_ == UILanguage::Vietnamese
            ? std::string(axis_vi) + " −100…+100% · Enter áp dụng · Esc hủy · Ctrl+Z hoàn tác"
            : std::string(axis_en) + " −100…+100% · Enter applies · Esc cancels · Ctrl+Z undoes";
    } else if (raster_light_field) {
        const char* parameter_vi = field == NumericFieldId::RasterLightContrast
            ? "Tương phản" : field == NumericFieldId::RasterLightHighlights
                ? "Vùng sáng" : field == NumericFieldId::RasterLightShadows
                    ? "Vùng tối" : field == NumericFieldId::RasterLightWhites
                        ? "Trắng" : "Đen";
        const char* parameter_en = field == NumericFieldId::RasterLightContrast
            ? "Contrast" : field == NumericFieldId::RasterLightHighlights
                ? "Highlights" : field == NumericFieldId::RasterLightShadows
                    ? "Shadows" : field == NumericFieldId::RasterLightWhites
                        ? "Whites" : "Blacks";
        status_msg_ = language_ == UILanguage::Vietnamese
            ? std::string(parameter_vi) + " −100…+100% · Enter áp dụng · Esc hủy · Ctrl+Z hoàn tác"
            : std::string(parameter_en) + " −100…+100% · Enter applies · Esc cancels · Ctrl+Z undoes";
    } else if (field == NumericFieldId::RasterHslHue) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Hue −180…+180° · sRGB đã mã hóa · Enter áp dụng · Esc hủy · Ctrl+Z hoàn tác"
            : "Hue −180…+180° · encoded sRGB · Enter applies · Esc cancels · Ctrl+Z undoes";
    } else if (raster_color_adjustment_field) {
        const char* label_vi = field == NumericFieldId::RasterHslSaturation
            ? "Bão hòa" : field == NumericFieldId::RasterHslLightness
                ? "Độ sáng HSL" : "Độ rực";
        const char* label_en = field == NumericFieldId::RasterHslSaturation
            ? "Saturation" : field == NumericFieldId::RasterHslLightness
                ? "Lightness" : "Vibrance";
        status_msg_ = language_ == UILanguage::Vietnamese
            ? std::string(label_vi) + " −100…+100% · Enter áp dụng · Esc hủy · Ctrl+Z hoàn tác"
            : std::string(label_en) + " −100…+100% · Enter applies · Esc cancels · Ctrl+Z undoes";
    } else if (raster_levels_field) {
        const bool gamma = field == NumericFieldId::RasterLevelsGamma;
        const char* label_vi = field == NumericFieldId::RasterLevelsBlackIn
            ? "Đen vào" : field == NumericFieldId::RasterLevelsWhiteIn
                ? "Trắng vào" : field == NumericFieldId::RasterLevelsBlackOut
                    ? "Đen ra" : field == NumericFieldId::RasterLevelsWhiteOut
                        ? "Trắng ra" : "Gamma";
        const char* label_en = field == NumericFieldId::RasterLevelsBlackIn
            ? "Input black" : field == NumericFieldId::RasterLevelsWhiteIn
                ? "Input white" : field == NumericFieldId::RasterLevelsBlackOut
                    ? "Output black" : field == NumericFieldId::RasterLevelsWhiteOut
                        ? "Output white" : "Gamma";
        status_msg_ = gamma
            ? (language_ == UILanguage::Vietnamese
                ? "Gamma 0,1…10× · encoded sRGB · Enter áp dụng · Esc hủy · Ctrl+Z hoàn tác"
                : "Gamma 0.1…10× · encoded sRGB · Enter applies · Esc cancels · Ctrl+Z undoes")
            : (language_ == UILanguage::Vietnamese
                ? std::string(label_vi) + " 0…100% · encoded sRGB · Enter áp dụng · Esc hủy · Ctrl+Z hoàn tác"
                : std::string(label_en) + " 0…100% · encoded sRGB · Enter applies · Esc cancels · Ctrl+Z undoes");
    } else if (raster_white_balance_field) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Cân bằng trắng tương đối trên sRGB tuyến tính −100…+100%; không phải Kelvin · Enter áp dụng · Esc hủy · Ctrl+Z hoàn tác"
            : "Relative white balance on linear-light sRGB, −100…+100%; not Kelvin · Enter applies · Esc cancels · Ctrl+Z undoes";
    } else if (raster_curves_field) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Curves · giá trị đầu ra 0…100% tại điểm cố định · Enter áp dụng · Esc hủy · Ctrl+Z hoàn tác"
            : "Curves · 0–100% output at fixed input points · Enter applies · Esc cancels · Ctrl+Z undoes";
    } else if (field == NumericFieldId::ObjectWidth || field == NumericFieldId::ObjectHeight) {
        const auto dpi_label = [&] {
            if (!image_pixel_dpi) return std::string{};
            std::ostringstream formatted;
            formatted << std::setprecision(5) << *image_pixel_dpi;
            return formatted.str();
        }();
        status_msg_ = image_pixel_dpi
            ? (language_ == UILanguage::Vietnamese
                ? "Kích thước: số trần theo " + page_unit + "; px dùng DPI nguồn " + dpi_label + " · Enter áp dụng · Esc hủy"
                : "Size: bare value uses " + page_unit + "; px uses source DPI " + dpi_label + " · Enter applies · Esc cancels")
            : (language_ == UILanguage::Vietnamese
                ? "Kích thước: số trần theo " + page_unit + "; px cần chọn một ảnh có DPI nguồn · Enter áp dụng · Esc hủy"
                : "Size: bare value uses " + page_unit + "; px requires one image with source DPI · Enter applies · Esc cancels");
    } else if (field == NumericFieldId::PenLength) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chiều dài mặc định " + page_unit + "; hậu tố rõ ghi đè · Enter tạo đoạn · Tab nhập góc (°)"
            : "Length defaults to " + page_unit + "; suffix overrides · Enter creates line · Tab sets angle (°)";
    } else if (field == NumericFieldId::PenExpression) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Pen: @L<góc, @ΔX;ΔY hoặc #X;Y · số trần theo " + page_unit + " · hậu tố rõ ghi đè"
            : "Pen: @L<angle, @dX;dY or #X;Y · bare lengths use " + page_unit + " · suffix overrides";
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
                ? "Bán kính mặc định " + page_unit + " · Khóa 4 góc · Enter áp dụng · Tab đổi ô · Esc hủy"
                : "Bán kính mặc định " + page_unit + " · Khóa đang mở · Enter áp dụng · Tab đổi ô · Esc hủy")
            : (rect_corners_locked_
                ? "Corner radius defaults to " + page_unit + " · 4 corners locked · Enter applies · Tab cycles · Esc cancels"
                : "Corner radius defaults to " + page_unit + " · independent corner · Enter applies · Tab cycles · Esc cancels");
    } else if (effect_numeric) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chiều dài mặc định " + page_unit + "; hậu tố rõ ghi đè · Opacity 0–100% · Enter áp dụng · Tab ô kế"
            : "Lengths default to " + page_unit + "; suffix overrides · Opacity 0–100% · Enter applies · Tab next field";
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
    numeric_input_context_ = {};
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

    const auto field = active_num_field_;
    const bool length_field = numeric_field_is_length(field);
    const bool text_line_spacing = field == text_numeric_field(TextPropertyField::LineSpacing);
    const bool accepts_length_units = length_field && !text_line_spacing;
    const bool unit_letter = (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z');

    if (active_num_field_ == NumericFieldId::PenLength || active_num_field_ == NumericFieldId::PenAngle) {
        const bool pen_length = active_num_field_ == NumericFieldId::PenLength;
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
                    ? ("Nhập chiều dài đoạn thẳng (" + std::string(display_length_unit_suffix(display_length_unit_)) + ") · Enter tạo đoạn · Tab nhập góc (°)")
                    : ("Enter line length (" + std::string(display_length_unit_suffix(display_length_unit_)) + ") · Enter creates line · Tab sets angle (°)");
            }
            update_pen_dimension_candidate();
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        } else if ((ch >= L'0' && ch <= L'9') || ch == L'.' || ch == L',' ||
                   (pen_length && (unit_letter || ch == L'+' || ch == L'/' || ch == L'*' ||
                                     ch == L'(' || ch == L')' || ch == L'-' || ch == L' ')) ||
                   (active_num_field_ == NumericFieldId::PenAngle && ch == L'-')) {
            if (!pen_length && (ch == L'.' || ch == L',') &&
                (num_input_buffer_.find('.') != std::string::npos || num_input_buffer_.find(',') != std::string::npos)) {
                return;
            }
            if (!pen_length && ch == L'-' && !num_input_buffer_.empty()) {
                return;
            }
            if (num_input_buffer_.size() < (pen_length ? 64u : 12u)) {
                if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch - L'A' + L'a');
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
            const auto result = pen_tool_->update_numeric_candidate(pen_numeric_buffer_,
                vector::precision::numeric::ParseOptions{'.', display_length_unit_});
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
        if (active_num_field_ == NumericFieldId::TacLimitPercent) {
            commit_numeric_field();
            if (active_num_field_ == NumericFieldId::None) {
                settings_tac_focus_index_ = 4;
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Đã lưu ngưỡng TAC · Tab tới Kiểm tra"
                    : "TAC limit applied · Tab moves to Check";
            }
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        NumericFieldId next = NumericFieldId::None;
        if (active_num_field_ == text_numeric_field(TextPropertyField::FontSize) ||
             active_num_field_ == text_numeric_field(TextPropertyField::FrameWidth) ||
             active_num_field_ == text_numeric_field(TextPropertyField::LineSpacing)) {
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
                case NumericFieldId::GradientStopOffset: next = NumericFieldId::GradientStopOpacity; break;
                case NumericFieldId::GradientStopOpacity: next = NumericFieldId::GradientStopMidpoint; break;
                case NumericFieldId::GradientStopMidpoint: next = NumericFieldId::GradientAngle; break;
                case NumericFieldId::GradientAngle: next = NumericFieldId::GradientLength; break;
                case NumericFieldId::GradientLength: next = NumericFieldId::GradientCenterX; break;
                case NumericFieldId::GradientCenterX: next = NumericFieldId::GradientCenterY; break;
                case NumericFieldId::GradientCenterY: next = NumericFieldId::GradientStopOffset; break;
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
                case NumericFieldId::ContourSpacing: next = NumericFieldId::ContourSteps; break;
                case NumericFieldId::ContourSteps: next = NumericFieldId::ContourTolerance; break;
                case NumericFieldId::ContourTolerance: next = NumericFieldId::ContourMiterLimit; break;
                case NumericFieldId::ContourMiterLimit: next = NumericFieldId::ContourSpacing; break;
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
            case NumericFieldId::ShadowOffsetX: next = NumericFieldId::ShadowOffsetY; break;
            case NumericFieldId::ShadowOffsetY: next = NumericFieldId::ShadowFeatherX; break;
            case NumericFieldId::BlurSigmaX: next = NumericFieldId::BlurSigmaY; break;
            case NumericFieldId::BlurSigmaY: next = NumericFieldId::BlurSigmaX; break;
            case NumericFieldId::ShadowFeatherX: next = NumericFieldId::ShadowFeatherY; break;
            case NumericFieldId::ShadowFeatherY: next = NumericFieldId::ShadowOpacity; break;
            case NumericFieldId::ShadowOpacity: next = NumericFieldId::ShadowOffsetX; break;
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
               (accepts_length_units && (unit_letter || ch == L'+' || ch == L'/' || ch == L'*' ||
                                 ch == L'(' || ch == L')' || ch == L' ')) ||
               ((active_num_field_ != NumericFieldId::PolygonSides || active_tool_ == 13) &&
                active_num_field_ != NumericFieldId::ContourSteps && ch == L'.') ||
               (ch == L',' && accepts_length_units) ||
               (ch == L',' && active_num_field_ == NumericFieldId::DistributeSpacing) ||
               (ch == L',' && (active_num_field_ == text_numeric_field(TextPropertyField::FontSize) ||
                               active_num_field_ == text_numeric_field(TextPropertyField::FrameWidth) ||
                               active_num_field_ == text_numeric_field(TextPropertyField::LineSpacing))) ||
               (ch == L',' && active_num_field_ == NumericFieldId::TacLimitPercent) ||
               (ch == L',' && active_tool_ == 11 &&
                (active_num_field_ == NumericFieldId::ObjectX ||
                 active_num_field_ == NumericFieldId::ObjectY)) ||
               (ch == L',' && active_num_field_ == NumericFieldId::ObjectOpacity) ||
               (ch == L',' && active_num_field_ == NumericFieldId::RasterExposureEv) ||
               (ch == L',' && (active_num_field_ == NumericFieldId::RasterColorBalanceRedCyan ||
                               active_num_field_ == NumericFieldId::RasterColorBalanceGreenMagenta ||
                               active_num_field_ == NumericFieldId::RasterColorBalanceBlueYellow)) ||
               (ch == L',' && (active_num_field_ == NumericFieldId::RasterLightContrast ||
                               active_num_field_ == NumericFieldId::RasterLightHighlights ||
                               active_num_field_ == NumericFieldId::RasterLightShadows ||
                               active_num_field_ == NumericFieldId::RasterLightWhites ||
                               active_num_field_ == NumericFieldId::RasterLightBlacks)) ||
               (ch == L',' && (active_num_field_ == NumericFieldId::RasterHslHue ||
                               active_num_field_ == NumericFieldId::RasterHslSaturation ||
                               active_num_field_ == NumericFieldId::RasterHslLightness ||
                               active_num_field_ == NumericFieldId::RasterVibranceAmount)) ||
               (ch == L',' && (active_num_field_ == NumericFieldId::RasterLevelsBlackIn ||
                               active_num_field_ == NumericFieldId::RasterLevelsWhiteIn ||
                               active_num_field_ == NumericFieldId::RasterLevelsGamma ||
                               active_num_field_ == NumericFieldId::RasterLevelsBlackOut ||
                               active_num_field_ == NumericFieldId::RasterLevelsWhiteOut)) ||
               (ch == L',' && (active_num_field_ == NumericFieldId::RasterWhiteBalanceTemperature ||
                               active_num_field_ == NumericFieldId::RasterWhiteBalanceTint)) ||
                (ch == L',' && (active_num_field_ == NumericFieldId::RasterCurvePoint0 ||
                                active_num_field_ == NumericFieldId::RasterCurvePoint1 ||
                                active_num_field_ == NumericFieldId::RasterCurvePoint2 ||
                                active_num_field_ == NumericFieldId::RasterCurvePoint3 ||
                                active_num_field_ == NumericFieldId::RasterCurvePoint4)) ||
               (ch == L',' && (active_num_field_ == NumericFieldId::FillOpacity ||
                               active_num_field_ == NumericFieldId::StrokeOpacity)) ||
               (ch == L',' && (active_num_field_ == NumericFieldId::BlurSigmaX ||
                               active_num_field_ == NumericFieldId::BlurSigmaY ||
                               active_num_field_ == NumericFieldId::ShadowFeatherX ||
                               active_num_field_ == NumericFieldId::ShadowFeatherY ||
                               active_num_field_ == NumericFieldId::ShadowOffsetX ||
                               active_num_field_ == NumericFieldId::ShadowOffsetY ||
                               active_num_field_ == NumericFieldId::ShadowOpacity)) ||
               (ch == L',' && active_tool_ == 13 &&
                (active_num_field_ == NumericFieldId::GradientStopOffset ||
                 active_num_field_ == NumericFieldId::GradientStopOpacity ||
                 active_num_field_ == NumericFieldId::GradientStopMidpoint ||
                 active_num_field_ == NumericFieldId::GradientAngle ||
                 active_num_field_ == NumericFieldId::GradientLength ||
                 active_num_field_ == NumericFieldId::GradientCenterX ||
                 active_num_field_ == NumericFieldId::GradientCenterY)) ||
               (ch == L',' && active_tool_ == 12 &&
                (active_num_field_ == NumericFieldId::ContourSpacing ||
                 active_num_field_ == NumericFieldId::ContourTolerance ||
                 active_num_field_ == NumericFieldId::ContourMiterLimit)) ||
               (accepts_length_units && ch == L'-') ||
               ((active_num_field_ != NumericFieldId::PolygonSides || active_tool_ == 13) &&
                active_num_field_ != NumericFieldId::ContourSteps &&
                (active_tool_ != 12 || active_num_field_ != NumericFieldId::ContourMiterLimit) && ch == L'-')) {
        if (!accepts_length_units && (ch == L'.' || ch == L',') &&
            (num_input_buffer_.find('.') != std::string::npos || num_input_buffer_.find(',') != std::string::npos)) {
            return;
        }
        if (!accepts_length_units && ch == L'-' && !num_input_buffer_.empty()) {
            return;
        }
        if (num_input_buffer_.size() < (accepts_length_units ? 64u : 12u)) {
            if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch - L'A' + L'a');
            num_input_buffer_.push_back(static_cast<char>(ch));
        }
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}


} // namespace pikdraw::desktop
