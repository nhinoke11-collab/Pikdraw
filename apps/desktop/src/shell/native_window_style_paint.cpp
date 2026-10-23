#include "pikdraw/color/preview/color_transform.hpp"
#include "pikdraw/color/preview/icc_preview.hpp"
#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_length_input.hpp"
#include "native_window_gradient_properties_paint.hpp"
#include "native_window_gradient_ramp.hpp"
#include "../tools/gradient_tool_controller.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace pikdraw::desktop {
#ifdef _WIN32
namespace {
std::wstring parameter_text(double value, bool percent = false) {
    std::wostringstream text;
    text << std::setprecision(7) << value;
    auto result = text.str();
    if (percent) result += L"%";
    return result;
}

int effect_row_height(const document::EffectParameters &parameters) {
    return std::holds_alternative<document::DropShadowEffect>(parameters) ? 246 : 118;
}
} // namespace

void paint_gradient_properties_panel(
    HDC dc, int inspector_x, int width, int top, int bottom, ThemeMode theme,
    UILanguage language, tools::GradientToolController& controller,
    NativeWindow::NumericFieldId active_field, const std::string& numeric_buffer,
    const color::IccPreview* color_preview, geometry::units::LengthUnit display_length_unit) {
    using Field = NativeWindow::NumericFieldId;
    namespace Layout = GradientPropertiesLayout;
    const bool vi = language == UILanguage::Vietnamese;
    const bool dark = theme == ThemeMode::CyberDark;
    const COLORREF text = dark ? RGB(232, 239, 248) : RGB(27, 39, 54);
    const COLORREF muted = dark ? RGB(147, 164, 184) : RGB(98, 113, 131);
    const COLORREF accent = dark ? RGB(0, 242, 195) : RGB(0, 126, 99);
    const COLORREF surface = dark ? RGB(18, 29, 43) : RGB(241, 245, 249);
    const COLORREF active_surface = dark ? RGB(24, 43, 59) : RGB(225, 240, 238);
    const COLORREF border = dark ? RGB(48, 64, 82) : RGB(194, 205, 217);
    const COLORREF disabled = dark ? RGB(72, 84, 99) : RGB(165, 176, 189);
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, inspector_x, top, inspector_x + width, bottom);
    const auto& presenter = controller.presenter();
    const auto& summary = presenter.summary();
    const auto* paint = presenter.draft_paint();
    const bool stops_mixed = summary.mixed_stops && summary.supported_count > 1;
    const bool geometry_mixed = summary.supported_count > 1 &&
        (summary.mixed_angle || summary.mixed_length || summary.mixed_center ||
         summary.mixed_aspect_ratio);
    const int content_left = inspector_x + Layout::content_left;
    const int content_right = std::min(inspector_x + width - 14,
                                       content_left + Layout::content_width);
    const int content_width = std::max(0, content_right - content_left);
    const int visible_bottom = bottom - top;
    auto y = [&](int offset) { return top + offset; };
    auto label = [&](const wchar_t* value, int offset, COLORREF color, int height = 20) {
        RECT rect{content_left, y(offset), content_right, y(offset) + height};
        SetTextColor(dc, color);
        DrawTextW(dc, value, -1, &rect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    };
    auto button = [&](int x, int offset, int button_width, const wchar_t* caption,
                      bool selected, bool enabled, int height = 24) {
        const COLORREF fill = !enabled ? (dark ? RGB(15, 23, 34) : RGB(247, 249, 251))
                                      : selected ? active_surface : surface;
        const COLORREF edge = selected ? accent : enabled ? border : disabled;
        HBRUSH brush = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, selected ? 2 : 1, edge);
        const auto old_pen = SelectObject(dc, pen);
        const auto old_brush = SelectObject(dc, brush);
        RoundRect(dc, x, y(offset), x + button_width, y(offset) + height, 5, 5);
        SelectObject(dc, old_brush);
        SelectObject(dc, old_pen);
        DeleteObject(brush);
        DeleteObject(pen);
        RECT rect{x + 3, y(offset), x + button_width - 3, y(offset) + height};
        SetTextColor(dc, !enabled ? muted : selected ? accent : text);
        DrawTextW(dc, caption, -1, &rect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    };
    auto field = [&](int offset, const wchar_t* name, Field id, const std::wstring& value,
                     bool enabled, int label_width = 108) {
        RECT name_rect{content_left, y(offset), content_left + label_width, y(offset) + 27};
        SetTextColor(dc, enabled ? text : muted);
        DrawTextW(dc, name, -1, &name_rect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        const int field_left = content_left + label_width + 4;
        RECT input{field_left, y(offset) + 1, content_right, y(offset) + 27};
        const bool focused = active_field == id;
        HBRUSH brush = CreateSolidBrush(!enabled ? (dark ? RGB(15, 23, 34) : RGB(247, 249, 251))
                                                : focused ? active_surface : surface);
        HPEN pen = CreatePen(PS_SOLID, focused ? 2 : 1,
                             focused ? accent : enabled ? border : disabled);
        const auto old_pen = SelectObject(dc, pen);
        const auto old_brush = SelectObject(dc, brush);
        RoundRect(dc, input.left, input.top, input.right, input.bottom, 5, 5);
        SelectObject(dc, old_brush);
        SelectObject(dc, old_pen);
        DeleteObject(brush);
        DeleteObject(pen);
        RECT value_rect{input.left + 7, input.top, input.right - 5, input.bottom};
        const std::wstring shown = focused ? NativeWindow::utf8_to_wide(numeric_buffer) + L"|" : value;
        SetTextColor(dc, !enabled ? muted : focused ? accent : text);
        DrawTextW(dc, shown.c_str(), -1, &value_rect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    };

    SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    RECT title{content_left, y(Layout::title_top), content_right, y(Layout::title_top) + 22};
    SetTextColor(dc, accent);
    DrawTextW(dc, vi ? L"THUỘC TÍNH · CHUYỂN SẮC" : L"PROPERTIES · GRADIENT", -1,
              &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    std::wstring summary_text = std::to_wstring(summary.supported_count) +
        (vi ? L" Path hỗ trợ" : L" supported Paths");
    if (summary.mixed_kind || summary.mixed_spread || stops_mixed || geometry_mixed)
        summary_text += vi ? L" · có giá trị hỗn hợp" : L" · mixed values";
    if (presenter.is_dirty()) summary_text += vi ? L" · bản nháp" : L" · draft";
    RECT summary_rect{content_left, y(22), content_right, y(42)};
    SetTextColor(dc, muted);
    DrawTextW(dc, summary_text.c_str(), -1, &summary_rect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    const int half_width = (content_width - 6) / 2;
    const bool can_edit = summary.can_edit_gradient() && paint != nullptr;
    const bool kind_mixed = summary.mixed_kind && summary.supported_count > 1;
    button(content_left, Layout::kind_top, half_width,
           kind_mixed ? (vi ? L"Tuyến tính · hỗn hợp" : L"Linear · mixed")
                      : vi ? L"Tuyến tính" : L"Linear",
           paint && paint->kind == document::GradientKind::Linear, can_edit);
    button(content_left + half_width + 6, Layout::kind_top, half_width,
           kind_mixed ? (vi ? L"Tỏa tròn · hỗn hợp" : L"Radial · mixed")
                      : vi ? L"Tỏa tròn" : L"Radial",
           paint && paint->kind == document::GradientKind::Radial, can_edit);
    const int spread_width = (content_width - 12) / 3;
    const bool spread_mixed = summary.mixed_spread && summary.supported_count > 1;
    constexpr document::GradientSpread spreads[] = {
        document::GradientSpread::Pad, document::GradientSpread::Repeat,
        document::GradientSpread::Reflect};
    const wchar_t* spread_names_vi[] = {L"Chặn", L"Lặp", L"Đối xứng"};
    const wchar_t* spread_names_en[] = {L"Pad", L"Repeat", L"Reflect"};
    for (int i = 0; i < 3; ++i) {
        std::wstring name = vi ? spread_names_vi[i] : spread_names_en[i];
        if (spread_mixed) name += L" · *";
        button(content_left + i * (spread_width + 6), Layout::spread_top, spread_width,
               name.c_str(), paint && paint->spread == spreads[i], can_edit);
    }

    const auto& stops = paint ? paint->stops : summary.stops;
    const auto preview_color = [&](const color::ColorValue& value) {
        return (color_preview ? color_preview->convert(value) : color::to_preview_rgba(value)).to_device();
    };
    size_t selected_index = 0;
    if (paint && !stops.empty()) {
        const auto selected = std::find_if(stops.begin(), stops.end(), [&](const auto& stop) {
            return stop.id == presenter.selected_stop_id();
        });
        if (selected != stops.end()) selected_index = static_cast<size_t>(selected - stops.begin());
    }
    label(vi ? L"DẢI MÀU CHUYỂN SẮC" : L"GRADIENT RAMP", Layout::ramp_header_top, accent);
    std::wstring page = stops.empty() ? L"0/0"
        : std::to_wstring(selected_index + 1) + L"/" + std::to_wstring(stops.size());
    RECT page_rect{content_right - 64, y(Layout::ramp_header_top), content_right, y(Layout::ramp_header_top) + 20};
    DrawTextW(dc, page.c_str(), -1, &page_rect,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    if (paint) {
        const RECT ramp_rect{content_left, y(Layout::ramp_top), content_right, y(Layout::ramp_top + Layout::ramp_height)};
        paint_gradient_ramp(dc, ramp_rect, *paint, presenter.selected_stop_id(), theme, color_preview);
    }

    const int action_width = (content_width - 18) / 4;
    const wchar_t* nav_prev = L"‹";
    const wchar_t* nav_next = L"›";
    button(content_left, Layout::stop_actions_top, action_width, nav_prev, false,
           can_edit && !stops_mixed && selected_index > 0);
    button(content_left + action_width + 6, Layout::stop_actions_top, action_width,
           vi ? L"+ Thêm" : L"+ Add", false,
           can_edit && !stops_mixed && stops.size() < document::kMaxGradientStops);
    button(content_left + 2 * (action_width + 6), Layout::stop_actions_top, action_width,
           vi ? L"Xóa" : L"Delete", false,
           can_edit && !stops_mixed && stops.size() > 2);
    button(content_left + 3 * (action_width + 6), Layout::stop_actions_top, action_width,
           nav_next, false, can_edit && !stops_mixed && selected_index + 1 < stops.size());

    color::ColorValue selected_color = stops.empty()
        ? color::ColorValue{color::RGBColor{0.0, 0.0, 0.0, 1.0}}
        : stops[selected_index].color;
    const auto color_rgba = preview_color(selected_color);
    RECT color_swatch{content_left, y(Layout::color_top) + 3,
                      content_left + 22, y(Layout::color_top) + 24};
    HBRUSH color_brush = CreateSolidBrush(RGB(color_rgba.r, color_rgba.g, color_rgba.b));
    FillRect(dc, &color_swatch, color_brush);
    DeleteObject(color_brush);
    button(content_left + 28, Layout::color_top, content_width - 28,
           vi ? L"Màu điểm dừng…" : L"Stop color…", false,
           can_edit && !stops_mixed && !stops.empty());

    const bool stop_enabled = can_edit && !stops_mixed && !stops.empty();
    auto percent_text = [&](double value) {
        std::wostringstream formatted;
        formatted << std::fixed << std::setprecision(1) << value << L"%";
        return formatted.str();
    };
    const auto* selected_stop = stops.empty() ? nullptr : &stops[selected_index];
    field(Layout::offset_top, vi ? L"Vị trí" : L"Offset", Field::GradientStopOffset,
          selected_stop ? percent_text(selected_stop->offset * 100.0) : L"—", stop_enabled);
    field(Layout::opacity_top, vi ? L"Độ mờ" : L"Opacity", Field::GradientStopOpacity,
          selected_stop ? percent_text(selected_stop->opacity * 100.0) : L"—", stop_enabled);
    const bool midpoint_enabled = stop_enabled && selected_stop && selected_index + 1 < stops.size();
    field(Layout::midpoint_top, vi ? L"Trung điểm" : L"Midpoint", Field::GradientStopMidpoint,
          midpoint_enabled ? percent_text(selected_stop->midpoint_to_next * 100.0)
                           : stops_mixed ? (vi ? L"Hỗn hợp" : L"Mixed") : L"—",
          midpoint_enabled);

    // Preset library bar
    label(vi ? L"THƯ VIỆN MẪU CÓ SẴN" : L"PRESET LIBRARY", Layout::preset_header_top, accent);
    const auto& presets = get_gradient_presets();
    const int preset_count = static_cast<int>(presets.size());
    const int p_gap = 4;
    const int p_width = (content_width - (preset_count - 1) * p_gap) / std::max(1, preset_count);
    for (int p_idx = 0; p_idx < preset_count; ++p_idx) {
        const int px = content_left + p_idx * (p_width + p_gap);
        button(px, Layout::presets_top, p_width,
               vi ? presets[p_idx].name_vi : presets[p_idx].name_en,
               false, can_edit, 24);
    }

    // Alignment and distribution actions
    const int half = (content_width - 6) / 2;
    button(content_left, Layout::align_top, half,
           vi ? L"⇄ Đảo dải màu" : L"⇄ Reverse Stops", false,
           can_edit && !stops_mixed, 24);
    button(content_left + half + 6, Layout::align_top, half,
           vi ? L"⇋ Phân bố đều" : L"⇋ Distribute", false,
           can_edit && !stops_mixed && stops.size() > 2, 24);

    label(vi ? L"HÌNH HỌC" : L"GEOMETRY", Layout::geometry_heading_top, accent);
    const auto projection = presenter.geometry_projection();
    auto measure_angle = [&](double value) {
        std::wostringstream formatted;
        formatted << std::fixed << std::setprecision(2) << value << L"°";
        return formatted.str();
    };
    const std::wstring mixed_geometry = vi ? L"Hỗn hợp · chọn một Path" : L"Mixed · select one Path";
    field(Layout::angle_top, vi ? L"Góc" : L"Angle", Field::GradientAngle,
          geometry_mixed ? mixed_geometry : measure_angle(projection.angle_degrees),
          can_edit && !geometry_mixed);
    field(Layout::length_top, vi ? L"Chiều dài" : L"Length", Field::GradientLength,
          geometry_mixed ? mixed_geometry : format_mm_display(projection.length_mm, display_length_unit),
          can_edit && !geometry_mixed);
    field(Layout::center_x_top, vi ? L"Tâm X" : L"Center X", Field::GradientCenterX,
          geometry_mixed ? mixed_geometry : format_mm_display(projection.center_mm.x, display_length_unit),
          can_edit && !geometry_mixed);
    field(Layout::center_y_top, vi ? L"Tâm Y" : L"Center Y", Field::GradientCenterY,
          geometry_mixed ? mixed_geometry : format_mm_display(projection.center_mm.y, display_length_unit),
          can_edit && !geometry_mixed);

    button(content_left, Layout::hint_top, content_width,
           vi ? L"⧈ Khớp dải màu theo khung đối tượng" : L"⧈ Fit Gradient to Bounds", false,
           can_edit, 26);

    const int buttons_y = Layout::actions_top;
    button(content_left, buttons_y, half, vi ? L"Áp dụng" : L"Apply", false,
           can_edit && presenter.is_dirty(), 28);
    button(content_left + half + 6, buttons_y, half, vi ? L"Hủy" : L"Cancel", false,
           can_edit && presenter.is_dirty(), 28);
    if (visible_bottom < buttons_y + 32)
        label(vi ? L"Cuộn xuống để xem đầy đủ nút bấm" : L"Scroll down to see all buttons",
              buttons_y + 30, muted);
    (void)inspector_x;
    RestoreDC(dc, saved);
}

void NativeWindow::paint_style_inspector(HDC dc, int left, int top, int bottom) {
    const int saved = SaveDC(dc);
    const int viewport_height = std::max(0, bottom - top);
    const int max_scroll = std::max(0, style_inspector_content_height() - viewport_height);
    style_inspector_scroll_ = std::clamp(style_inspector_scroll_, 0, max_scroll);
    IntersectClipRect(dc, left, top, left + 280, bottom);
    SetViewportOrgEx(dc, 0, -style_inspector_scroll_, nullptr);
    const bool vi = language_ == UILanguage::Vietnamese;
    const bool dark = theme_ == ThemeMode::CyberDark;
    const auto unit_suffix = display_length_unit_suffix_wide(display_length_unit_);
    const auto sigma_x_label = std::wstring(L"σX ") + unit_suffix;
    const auto sigma_y_label = std::wstring(L"σY ") + unit_suffix;
    const auto offset_x_label = std::wstring(L"X ") + unit_suffix;
    const auto offset_y_label = std::wstring(L"Y ") + unit_suffix;
    const COLORREF text = dark ? RGB(220, 230, 242) : RGB(30, 41, 59);
    const COLORREF muted = dark ? RGB(142, 159, 179) : RGB(100, 116, 139);
    const COLORREF accent = dark ? RGB(0, 242, 195) : RGB(0, 112, 88);
    const COLORREF panel = dark ? RGB(19, 29, 43) : RGB(235, 241, 247);
    const auto snapshot = root().document().current_snapshot();
    const auto &selection = root().vector_controller().selection();
    const auto style = selection_style(*snapshot, selection.selected_ids(), default_style_);
    const auto appearance = selection_appearance(*snapshot, selection.selected_ids());
    const auto &source_color = palette_stroke_ ? style.values.stroke : style.values.fill;
    const bool mixed = palette_stroke_ ? style.mixed_stroke : style.mixed_fill;
    const bool enabled = palette_stroke_ ? style.values.has_stroke : style.values.has_fill;
    const bool mixed_enabled = palette_stroke_ ? style.mixed_stroke_enabled : style.mixed_fill_enabled;
    SetBkMode(dc, TRANSPARENT);
    const auto label = [&](const std::wstring &value, int x, int y, int width, COLORREF colorref) {
        SelectObject(dc, font_be_vietnam_);
        SetTextColor(dc, colorref);
        RECT r{left + x, top + y, left + x + width, top + y + 24};
        DrawTextW(dc, value.c_str(), -1, &r,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    };
    const auto button = [&](RECT r, const std::wstring &value, bool active, bool available = true) {
        OffsetRect(&r, left, top);
        const auto brush = CreateSolidBrush(available ? panel : (dark ? RGB(24, 34, 47) : RGB(242, 245, 249)));
        const auto pen = CreatePen(PS_SOLID, 1,
            active ? accent : (available ? (dark ? RGB(49, 65, 85) : RGB(187, 200, 216))
                                         : (dark ? RGB(43, 51, 62) : RGB(210, 218, 227))));
        const auto old_brush = SelectObject(dc, brush);
        const auto old_pen = SelectObject(dc, pen);
        RoundRect(dc, r.left, r.top, r.right, r.bottom, 5, 5);
        SelectObject(dc, old_brush);
        SelectObject(dc, old_pen);
        DeleteObject(brush);
        DeleteObject(pen);
        SelectObject(dc, font_be_vietnam_);
        SetTextColor(dc, !available ? muted : active ? accent : text);
        DrawTextW(dc, value.c_str(), -1, &r,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    };
    const auto title = style.count == 0
                           ? (vi ? L"KIỂU CHO ĐỐI TƯỢNG MỚI" : L"NEW OBJECT STYLE")
                           : (vi ? L"ĐANG CHỌN: " : L"SELECTED: ") + std::to_wstring(style.count);
    label(title, 12, 0, 256, accent);
    if (const auto bounds = selection.bounds(*snapshot)) {
        std::wostringstream xy, wh;
        xy << L"X " << format_mm_display(bounds->x, display_length_unit_, 5)
           << L"  Y " << format_mm_display(bounds->y, display_length_unit_, 5);
        wh << L"W " << format_mm_display(bounds->width, display_length_unit_, 5)
           << L"  H " << format_mm_display(bounds->height, display_length_unit_, 5);
        label(xy.str(), 12, 27, 256, text);
        label(wh.str(), 12, 49, 256, text);
    } else {
        label(vi ? L"Click hoặc kéo khung để chọn đối tượng" : L"Click or drag a window to select", 12, 32,
              256, muted);
    }
    button({12, 82, 134, 110}, vi ? L"Tô màu · Fill" : L"Fill", !palette_stroke_);
    button({142, 82, 268, 110}, vi ? L"Đường viền · Stroke" : L"Stroke", palette_stroke_, style.all_paths);
    button({12, 120, 268, 164}, L"", false);
    const auto convert = [&](const color::ColorValue &value) {
        const auto* preview = effective_color_preview();
        return (preview ? preview->convert(value) : color::to_preview_rgba(value)).to_device();
    };
    const auto device = convert(source_color);
    RECT swatch{left + 20, top + 128, left + 46, top + 156};
    const auto brush = CreateSolidBrush(RGB(device.r, device.g, device.b));
    FillRect(dc, &swatch, brush);
    DeleteObject(brush);
    if (!enabled) {
        const auto pen = CreatePen(PS_SOLID, 2, RGB(235, 65, 80));
        const auto previous = SelectObject(dc, pen);
        MoveToEx(dc, swatch.left, swatch.bottom, nullptr);
        LineTo(dc, swatch.right, swatch.top);
        SelectObject(dc, previous);
        DeleteObject(pen);
    }
    label(mixed ? (vi ? L"Nhiều giá trị" : L"Mixed values") : utf8_to_wide(color_description(source_color)),
          55, 123, 207, text);
    label(vi ? L"Nhấp để nhập kênh màu…" : L"Click to edit source channels…", 55, 143, 207, muted);
    const auto enable_text = mixed_enabled ? (vi ? L"Nhiều trạng thái · bật tất cả" : L"Mixed · enable all")
                             : enabled ? (vi ? L"✓ Đang bật · nhấp để tắt" : L"✓ Enabled · click to disable")
                                       : (vi ? L"Không màu · nhấp để bật" : L"None · click to enable");
    button({12, 174, 268, 202}, enable_text, enabled && !mixed_enabled, style.all_paths);
    label(vi ? L"ĐỘ DÀY VIỀN NGUỒN" : L"SOURCE STROKE WIDTH", 12, 210, 256, muted);
    std::wostringstream width;
    if (style.mixed_width)
        width << (vi ? L"Nhiều giá trị" : L"Mixed values");
    else if (!style.values.has_stroke)
        width << (vi ? L"Không viền (None)" : L"None");
    else if (style.values.width_mm <= 0.0001)
        width << (vi ? L"Hairline (Nét mảnh)" : L"Hairline");
    else
        width << format_mm_display(style.values.width_mm, display_length_unit_, 5);
    button({12, 236, 268, 266}, width.str() + L"   ▾", false, style.all_paths);

    const auto paint_opacity_text = [&](bool stroke) {
        const auto field = stroke ? NumericFieldId::StrokeOpacity : NumericFieldId::FillOpacity;
        if (active_num_field_ == field)
            return (stroke ? (vi ? L"Viền: " : L"Stroke: ")
                           : (vi ? L"Tô: " : L"Fill: ")) +
                   utf8_to_wide(num_input_buffer_) + L"|";
        if (appearance.selected_count == 0)
            return stroke ? (vi ? std::wstring(L"Viền —") : std::wstring(L"Stroke —"))
                          : (vi ? std::wstring(L"Tô —") : std::wstring(L"Fill —"));
        if (appearance.contains_locked)
            return stroke ? (vi ? std::wstring(L"Viền · khóa") : std::wstring(L"Stroke · locked"))
                          : (vi ? std::wstring(L"Tô · khóa") : std::wstring(L"Fill · locked"));
        if (stroke && appearance.all_path_text && !appearance.all_paths)
            return vi ? std::wstring(L"Viền · chỉ Path") : std::wstring(L"Stroke · Path only");
        if (stroke ? !appearance.can_edit_stroke_opacity()
                   : !appearance.can_edit_fill_opacity())
            return stroke ? (vi ? std::wstring(L"Viền · không hỗ trợ")
                                : std::wstring(L"Stroke · unsupported"))
                          : (vi ? std::wstring(L"Tô · không hỗ trợ")
                                : std::wstring(L"Fill · unsupported"));
        const bool mixed_value = stroke ? appearance.mixed_stroke_opacity
                                        : appearance.mixed_fill_opacity;
        if (mixed_value)
            return stroke ? (vi ? std::wstring(L"Viền · nhiều giá trị")
                                : std::wstring(L"Stroke · Mixed"))
                          : (vi ? std::wstring(L"Tô · nhiều giá trị")
                                : std::wstring(L"Fill · Mixed"));
        std::wostringstream value;
        value << (stroke ? (vi ? L"Viền: " : L"Stroke: ")
                         : (vi ? L"Tô: " : L"Fill: "))
              << std::fixed << std::setprecision(1)
              << (stroke ? appearance.stroke_opacity : appearance.fill_opacity) * 100.0 << L"%";
        return value.str();
    };
    button({12, 270, 134, 296}, paint_opacity_text(false),
           active_num_field_ == NumericFieldId::FillOpacity,
           appearance.can_edit_fill_opacity());
    button({142, 270, 268, 296}, paint_opacity_text(true),
           active_num_field_ == NumericFieldId::StrokeOpacity,
           appearance.can_edit_stroke_opacity());

    std::wstring opacity_value;
    if (active_num_field_ == NumericFieldId::ObjectOpacity) {
        opacity_value = utf8_to_wide(num_input_buffer_) + L"|";
    } else if (appearance.supported_count == 0) {
        opacity_value = appearance.selected_count == 0
            ? L"—" : (vi ? L"Không hỗ trợ" : L"Unsupported");
    } else if (appearance.mixed_object_opacity) {
        opacity_value = vi ? L"Nhiều giá trị" : L"Mixed";
    } else {
        std::wostringstream value;
        value << std::fixed << std::setprecision(1) << appearance.object_opacity * 100.0 << L"%";
        opacity_value = value.str();
    }
    const std::wstring opacity_label = (vi ? L"Độ mờ đối tượng: " : L"Object opacity: ") + opacity_value;
    button({12, 298, 268, 328}, opacity_label,
           active_num_field_ == NumericFieldId::ObjectOpacity,
           appearance.can_edit_object_opacity());
    const wchar_t *opacity_hint = appearance.selected_count == 0
        ? (vi ? L"Chọn Path/Text · Stroke chỉ áp cho Path"
              : L"Select Path/Text · Stroke is Path-only")
        : appearance.contains_locked
            ? (vi ? L"Có đối tượng khóa · không sửa một phần"
                  : L"Locked target · no partial edits")
            : !appearance.all_path_text
                ? (vi ? L"Chỉ Path/Text · không sửa một phần"
                      : L"Path/Text only · no partial edits")
                : (vi ? L"Enter áp dụng · Esc hủy"
                      : L"Enter apply · Esc cancel");
    label(opacity_hint, 12, 332, 256, muted);
    const bool show_cmyk = palette_show_cmyk_.value_or(snapshot->color_mode() == color::DocumentColorMode::Cmyk);
    button({12, 356, 134, 380}, vi ? L"Bảng CMYK" : L"CMYK Palette", show_cmyk);
    button({142, 356, 268, 380}, vi ? L"Bảng RGB" : L"RGB Palette", !show_cmyk);
    label(snapshot->page().color_mode_explicit ? L"ICC: " + utf8_to_wide(snapshot->color_profile_name())
        : (vi ? L"File cũ · giữ màu nguồn" : L"Legacy file · source colors retained"), 12, 386, 256, muted);
    label(vi ? L"Đổi hệ màu / DPI: menu Edit" : L"Color mode / DPI: Edit menu", 12, 404, 256, muted);
    const auto &palette = editor_palette(show_cmyk);
    for (size_t i = 0; i < palette.size(); ++i) {
        const auto rgba = convert(palette[i]);
        const LONG x = left + 12 + static_cast<int>(i % 9) * 28;
        const LONG y = top + 424 + static_cast<int>(i / 9) * 28;
        RECT rect{x, y, x + 24, y + 24};
        const auto color_brush = CreateSolidBrush(RGB(rgba.r, rgba.g, rgba.b));
        FillRect(dc, &rect, color_brush);
        DeleteObject(color_brush);
        FrameRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
    }
    label(vi ? L"Trái: mục đang chọn · phải: Viền" : L"Left: active target · right: Stroke", 12, 480, 256,
          muted);
    button({12, 504, 268, 532}, vi ? L"Bảng màu Spot…" : L"Spot palette…", false);
    const auto* preview = effective_color_preview();
    label(preview && preview->has_cmyk_transform()
              ? L"CMYK preview: ICC → sRGB"
              : (vi ? L"CMYK preview: gần đúng · chưa có ICC" : L"CMYK preview: approximate · no ICC"),
          12, 534, 256, muted);
    button({12, 560, 268, 588}, vi ? L"Profile màu và ICC…" : L"Color profiles and ICC…", false);
    label(vi ? L"Cuộn bảng để xem Blend và Effects" : L"Scroll for Blend and Effects",
          12, 590, 256, muted);

    label(vi ? L"CHẾ ĐỘ HÒA TRỘN" : L"BLEND MODE",
          12, kAppearanceBlendTop, 156, accent);
    if (appearance.mixed_blend_mode)
        label(vi ? L"Nhiều giá trị" : L"Mixed", 176, kAppearanceBlendTop, 92, muted);
    constexpr document::BlendMode blend_modes[] = {
        document::BlendMode::Normal, document::BlendMode::Multiply,
        document::BlendMode::Screen, document::BlendMode::Overlay,
        document::BlendMode::Darken, document::BlendMode::Lighten};
    const wchar_t *blend_labels_en[] = {L"Normal", L"Multiply", L"Screen", L"Overlay", L"Darken", L"Lighten"};
    const wchar_t *blend_labels_vi[] = {L"Bình thường", L"Nhân", L"Màn hình", L"Phủ", L"Tối", L"Sáng"};
    const int blend_width = 80;
    for (int i = 0; i < 6; ++i) {
        const int x = 12 + (i % 3) * 88;
        const int y = kAppearanceBlendTop + 26 + (i / 3) * 28;
        button({x, y, x + blend_width, y + 24}, vi ? blend_labels_vi[i] : blend_labels_en[i],
               !appearance.mixed_blend_mode && appearance.blend_mode == blend_modes[i],
               appearance.can_edit_appearance());
    }
    if (workflow().cancel_stale_effect_draft())
        status_msg_ = vi ? "Bản nháp hiệu ứng đã hủy vì tài liệu hoặc vùng chọn thay đổi"
                         : "Effect draft cancelled because the document or selection changed";
    const auto effect_draft = workflow().effect_draft();
    label(vi ? L"HIỆU ỨNG · THỨ TỰ TỪ TRÊN XUỐNG" : L"EFFECTS · TOP-TO-BOTTOM ORDER",
          12, kAppearanceEffectsTop, 256, accent);
    label(vi ? L"Xem trước Path; Text cần outline thật." :
               L"Preview Path; Text needs real outlines.",
          12, kAppearanceEffectsTop + 24, 256, muted);
    const bool has_blur = workflow().selected_effect_index(
        document::GaussianBlurEffect{1.0, 1.0}).has_value();
    const bool has_shadow = workflow().selected_effect_index(
        document::DropShadowEffect{}).has_value();
    const bool can_activate_blur = !effect_draft && workflow().can_activate_selected_effect(
        document::GaussianBlurEffect{1.0, 1.0});
    const bool can_activate_shadow = !effect_draft && workflow().can_activate_selected_effect(
        document::DropShadowEffect{});
    const bool can_add_effect = !effect_draft && workflow().can_add_selected_effect();
    button({12, kAppearanceEffectsTop + 50, 134, kAppearanceEffectsTop + 78},
           has_blur ? (vi ? L"Sửa Blur" : L"Edit Blur") : L"+ Gaussian Blur",
           false, can_activate_blur);
    button({142, kAppearanceEffectsTop + 50, 268, kAppearanceEffectsTop + 78},
           has_shadow ? (vi ? L"Sửa bóng" : L"Edit Shadow") : L"+ Drop Shadow",
           false, can_activate_shadow);
    if (effect_draft) {
        const int draft_top = kAppearanceEffectListTop;
        const auto *blur = std::get_if<document::GaussianBlurEffect>(&effect_draft->parameters);
        const auto *shadow = std::get_if<document::DropShadowEffect>(&effect_draft->parameters);
        const wchar_t* effect_name = blur ? L"Gaussian Blur" : L"Drop Shadow";
        const std::wstring draft_title = effect_draft->editing_existing
            ? (vi ? std::wstring(L"Đang chỉnh · ") + effect_name
                  : std::wstring(L"Editing · ") + effect_name)
            : (vi ? std::wstring(L"Bản nháp · ") + effect_name
                  : std::wstring(L"Draft · ") + effect_name);
        label(draft_title,
              12, draft_top, 256, text);
        label(effect_draft->editing_existing
                  ? (vi ? L"Kéo xem trước · thả chuột lưu một bước Undo"
                        : L"Drag to preview · release commits one Undo step")
                  : (vi ? L"Chưa đổi tài liệu · Preview để xem · Apply để lưu"
                        : L"Document unchanged · Preview to inspect · Apply to keep"),
              12, draft_top + 18, 256, muted);
        const auto draw_draft_numeric = [&](int x, int y, const wchar_t *name,
                                            DocumentWorkflow::EffectDraftField field,
                                            double value, bool percent) {
            RECT name_rect{left + x, top + y, left + x + 42, top + y + 26};
            SelectObject(dc, font_be_vietnam_);
            SetTextColor(dc, muted);
            DrawTextW(dc, name, -1, &name_rect,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            RECT input{left + x + 42, top + y + 1, left + x + 122, top + y + 25};
            const bool focused = effect_draft->active_field == field;
            const auto field_brush = CreateSolidBrush(focused ? (dark ? RGB(34, 56, 70) : RGB(218, 239, 232)) : panel);
            const auto field_pen = CreatePen(PS_SOLID, focused ? 2 : 1,
                focused ? accent : (dark ? RGB(49, 65, 85) : RGB(187, 200, 216)));
            const auto old_pen = SelectObject(dc, field_pen);
            const auto old_brush = SelectObject(dc, field_brush);
            RoundRect(dc, input.left, input.top, input.right, input.bottom, 4, 4);
            SelectObject(dc, old_pen);
            SelectObject(dc, old_brush);
            DeleteObject(field_pen);
            DeleteObject(field_brush);
            const std::wstring shown = focused
                ? utf8_to_wide(effect_draft->numeric_buffer) + L"|"
                : percent ? parameter_text(value, true)
                          : format_mm_display(value, display_length_unit_, 5);
            RECT value_rect{input.left + 5, input.top, input.right - 5, input.bottom};
            SetTextColor(dc, focused ? accent : text);
            SelectObject(dc, font_mono_);
            DrawTextW(dc, shown.c_str(), -1, &value_rect,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        };
        if (blur) {
            draw_draft_numeric(12, draft_top + 40, sigma_x_label.c_str(),
                               DocumentWorkflow::EffectDraftField::SigmaX, blur->sigma_x_mm, false);
            draw_draft_numeric(142, draft_top + 40, sigma_y_label.c_str(),
                               DocumentWorkflow::EffectDraftField::SigmaY, blur->sigma_y_mm, false);
        } else if (shadow) {
            draw_draft_numeric(12, draft_top + 40, offset_x_label.c_str(),
                               DocumentWorkflow::EffectDraftField::OffsetX, shadow->dx_mm, false);
            draw_draft_numeric(142, draft_top + 40, offset_y_label.c_str(),
                               DocumentWorkflow::EffectDraftField::OffsetY, shadow->dy_mm, false);
            draw_draft_numeric(12, draft_top + 68, sigma_x_label.c_str(),
                               DocumentWorkflow::EffectDraftField::SigmaX, shadow->sigma_x_mm, false);
            draw_draft_numeric(142, draft_top + 68, sigma_y_label.c_str(),
                               DocumentWorkflow::EffectDraftField::SigmaY, shadow->sigma_y_mm, false);
            draw_draft_numeric(12, draft_top + 96, L"Opacity %",
                               DocumentWorkflow::EffectDraftField::Opacity, shadow->opacity * 100.0, true);
            const auto rgba = convert(shadow->color);
            RECT swatch{left + 156, top + draft_top + 101, left + 174, top + draft_top + 117};
            const auto brush = CreateSolidBrush(RGB(rgba.r, rgba.g, rgba.b));
            FillRect(dc, &swatch, brush);
            DeleteObject(brush);
            FrameRect(dc, &swatch, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
            button({184, draft_top + 96, 268, draft_top + 122},
                   vi ? L"Màu bóng…" : L"Shadow color…", false, true);
        }
        const int buttons_y = draft_top + (blur ? 72 : 126);
        const wchar_t *preview_label = effect_draft->preview_enabled
            ? (vi ? L"Xem trước ✓" : L"Preview ✓")
            : (vi ? L"Xem trước" : L"Preview");
        button({12, buttons_y, 96, buttons_y + 26}, preview_label,
               effect_draft->preview_enabled, true);
        button({112, buttons_y, 190, buttons_y + 26},
               effect_draft->editing_existing
                   ? (vi ? L"Cập nhật" : L"Update")
                   : (vi ? L"Áp dụng" : L"Apply"), false, true);
        button({194, buttons_y, 268, buttons_y + 26}, vi ? L"Hủy" : L"Cancel", false, true);
        label(effect_draft->editing_existing
                  ? (vi ? L"Enter lưu · Esc hủy chỉnh sửa"
                        : L"Enter commits · Esc cancels the edit")
                  : (vi ? L"Enter xác nhận ô · Tab chuyển · Esc hủy bản nháp"
                        : L"Enter confirms a field · Tab moves · Esc cancels draft"),
              12, buttons_y + 28, 256, muted);
    } else if (!appearance.can_edit_appearance()) {
        const std::wstring reason = appearance.selected_count == 0
            ? (vi ? L"Chọn Path/Text để sửa hiệu ứng." : L"Select Path/Text to edit effects.")
            : appearance.contains_locked
                ? (vi ? L"Vùng chọn có đối tượng khóa; cập nhật bị vô hiệu." :
                        L"Selection contains a locked object; edits are disabled.")
                : (vi ? L"Chỉ Path/Text được hỗ trợ; không cập nhật một phần." :
                        L"Path/Text only; partial updates are disabled.");
        label(reason, 12, kAppearanceEffectsTop + 80, 256, muted);
    } else if (appearance.effects.empty()) {
        label(vi ? L"Chưa có node. Thêm hiệu ứng sẽ nối vào cuối stack." :
                   L"No nodes yet. New effects append to the stack.",
              12, kAppearanceEffectListTop, 256, muted);
    } else if (!appearance.effect_stack_compatible) {
        label(vi ? L"Stack đang khác nhau giữa các đối tượng. Chọn một đối tượng hoặc đồng bộ loại/thứ tự node." :
                   L"Effect stacks differ. Select one object or align node types/order before editing.",
              12, kAppearanceEffectListTop, 256, muted);
    } else {
        const auto draw_numeric = [&](int x, int y, const wchar_t *name, NumericFieldId field,
                                      size_t effect_index, bool field_mixed, double value,
                                      bool percent, bool available) {
            RECT name_rect{left + x, top + y, left + x + 42, top + y + 26};
            SelectObject(dc, font_be_vietnam_);
            SetTextColor(dc, muted);
            DrawTextW(dc, name, -1, &name_rect,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            RECT input{left + x + 42, top + y + 1, left + x + 122, top + y + 25};
            const bool focused = active_num_field_ == field && numeric_effect_index_ == effect_index;
            const auto field_brush = CreateSolidBrush(focused ? (dark ? RGB(34, 56, 70) : RGB(218, 239, 232)) : panel);
            const auto field_pen = CreatePen(PS_SOLID, focused ? 2 : 1,
                                              focused ? accent : (available ?
                                                  (dark ? RGB(49, 65, 85) : RGB(187, 200, 216)) :
                                                  (dark ? RGB(43, 51, 62) : RGB(210, 218, 227))));
            const auto old_pen = SelectObject(dc, field_pen);
            const auto old_brush = SelectObject(dc, field_brush);
            RoundRect(dc, input.left, input.top, input.right, input.bottom, 4, 4);
            SelectObject(dc, old_pen);
            SelectObject(dc, old_brush);
            DeleteObject(field_pen);
            DeleteObject(field_brush);
            std::wstring shown;
            if (focused)
                shown = utf8_to_wide(num_input_buffer_) + L"|";
            else if (field_mixed)
                shown = vi ? L"Khác nhau" : L"Mixed";
            else
                shown = percent ? parameter_text(value, true)
                                : format_mm_display(value, display_length_unit_, 5);
            RECT value_rect{input.left + 5, input.top, input.right - 5, input.bottom};
            SetTextColor(dc, !available ? muted : focused ? accent : text);
            SelectObject(dc, font_mono_);
            DrawTextW(dc, shown.c_str(), -1, &value_rect,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        };
        int node_top = kAppearanceEffectListTop;
        for (size_t index = 0; index < appearance.effects.size(); ++index) {
            const auto &effect = appearance.effects[index];
            const bool blur = std::holds_alternative<document::GaussianBlurEffect>(effect.parameters);
            const auto title = std::to_wstring(index + 1) + L" · " +
                (blur ? (vi ? L"Gaussian Blur" : L"Gaussian Blur")
                      : (vi ? L"Drop Shadow" : L"Drop Shadow"));
            label(title, 12, node_top, 256, text);
            const int controls_y = node_top + 22;
            button({12, controls_y, 96, controls_y + 23},
                   effect.mixed_enabled ? (vi ? L"Mixed" : L"Mixed")
                       : effect.enabled ? (vi ? L"Đang bật" : L"Enabled")
                                        : (vi ? L"Đã tắt" : L"Disabled"),
                   effect.enabled && !effect.mixed_enabled,
                   appearance.can_edit_effect(index));
            button({100, controls_y, 146, controls_y + 23}, vi ? L"Lên" : L"Up", false,
                   appearance.can_edit_effect(index) && index > 0 &&
                       workflow().can_move_selected_effect(index, -1));
            button({150, controls_y, 206, controls_y + 23}, vi ? L"Xuống" : L"Down", false,
                   appearance.can_edit_effect(index) && index + 1 < appearance.effects.size() &&
                       workflow().can_move_selected_effect(index, 1));
            button({210, controls_y, 268, controls_y + 23}, vi ? L"Xóa" : L"Remove", false,
                   appearance.can_edit_effect(index));
            // The presenter has already evaluated the whole selection's compatibility.
            // Re-run the workflow guard when an action is invoked, not once per node while
            // painting a potentially large effect stack.
            const bool can_edit_node = appearance.can_edit_effect(index);
            const int first_row = node_top + 48;
            if (blur) {
                const auto &blur_value = std::get<document::GaussianBlurEffect>(effect.parameters);
                draw_numeric(12, first_row, sigma_x_label.c_str(), NumericFieldId::BlurSigmaX, index,
                             effect.mixed_sigma_x, blur_value.sigma_x_mm, false, can_edit_node);
                draw_numeric(142, first_row, sigma_y_label.c_str(), NumericFieldId::BlurSigmaY, index,
                             effect.mixed_sigma_y, blur_value.sigma_y_mm, false, can_edit_node);
            } else {
                const auto &shadow = std::get<document::DropShadowEffect>(effect.parameters);
                draw_numeric(12, first_row, offset_x_label.c_str(), NumericFieldId::ShadowOffsetX,
                             index, effect.mixed_offset_x, shadow.dx_mm, false, can_edit_node);
                draw_numeric(142, first_row, offset_y_label.c_str(), NumericFieldId::ShadowOffsetY,
                             index, effect.mixed_offset_y, shadow.dy_mm, false, can_edit_node);
                draw_numeric(12, first_row + 30, sigma_x_label.c_str(), NumericFieldId::ShadowFeatherX,
                             index, effect.mixed_sigma_x, shadow.sigma_x_mm, false, can_edit_node);
                draw_numeric(142, first_row + 30, sigma_y_label.c_str(), NumericFieldId::ShadowFeatherY,
                             index, effect.mixed_sigma_y, shadow.sigma_y_mm, false, can_edit_node);
                label(vi ? L"Màu bóng" : L"Shadow color", 12, first_row + 62, 82, muted);
                button({96, first_row + 62, 268, first_row + 88},
                       effect.mixed_color ? (vi ? L"Nhiều màu" : L"Mixed colors")
                           : utf8_to_wide(color_description(shadow.color)),
                       false, can_edit_node && !effect.mixed_color);
                const auto shadow_rgba = convert(shadow.color);
                RECT color_swatch{left + 101, top + first_row + 67,
                                  left + 119, top + first_row + 83};
                const auto shadow_brush = CreateSolidBrush(RGB(shadow_rgba.r, shadow_rgba.g, shadow_rgba.b));
                FillRect(dc, &color_swatch, shadow_brush);
                DeleteObject(shadow_brush);
                FrameRect(dc, &color_swatch, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
                draw_numeric(12, first_row + 92, L"Opacity %", NumericFieldId::ShadowOpacity,
                             index, effect.mixed_opacity, shadow.opacity * 100.0, true, can_edit_node);
                label(vi ? L"Hòa bóng lên nền" : L"Shadow blend with backdrop",
                      12, first_row + 122, 210, muted);
                if (effect.mixed_shadow_blend_mode)
                    label(vi ? L"Nhiều giá trị" : L"Mixed", 218,
                          first_row + 122, 50, muted);
                constexpr document::BlendMode shadow_modes[] = {
                    document::BlendMode::Normal, document::BlendMode::Multiply,
                    document::BlendMode::Screen, document::BlendMode::Overlay,
                    document::BlendMode::Darken, document::BlendMode::Lighten};
                const wchar_t *shadow_labels_en[] = {
                    L"Normal", L"Multiply", L"Screen", L"Overlay", L"Darken", L"Lighten"};
                const wchar_t *shadow_labels_vi[] = {
                    L"Bình thường", L"Nhân", L"Màn hình", L"Phủ", L"Tối", L"Sáng"};
                for (int mode_index = 0; mode_index < 6; ++mode_index) {
                    const auto mode = shadow_modes[mode_index];
                    const int x = 12 + (mode_index % 3) * 88;
                    const int y = first_row + 144 + (mode_index / 3) * 28;
                    button({x, y, x + 80, y + 24},
                           vi ? shadow_labels_vi[mode_index] : shadow_labels_en[mode_index],
                           !effect.mixed_shadow_blend_mode &&
                               shadow.blend_mode == mode,
                           can_edit_node && workflow().can_set_selected_effect_blend_mode(
                               index, mode));
                }
            }
            node_top += effect_row_height(effect.parameters) + 10;
        }
    }
    if (!effect_draft && can_add_effect) {
        int draft_add_top = kAppearanceEffectListTop;
        if (appearance.effects.empty()) draft_add_top += 24;
        else if (!appearance.effect_stack_compatible) draft_add_top += 56;
        else
            for (const auto &effect : appearance.effects)
                draft_add_top += effect_row_height(effect.parameters) + 10;
        button({12, draft_add_top, 134, draft_add_top + 26},
               vi ? L"Bản nháp Blur" : L"Draft Blur", false,
               workflow().can_add_selected_effect(document::GaussianBlurEffect{1.0, 1.0}));
        button({142, draft_add_top, 268, draft_add_top + 26},
               vi ? L"Bản nháp Shadow" : L"Draft Shadow", false,
               workflow().can_add_selected_effect(document::DropShadowEffect{}));
    }
    RestoreDC(dc, saved);
}
#endif
} // namespace pikdraw::desktop
