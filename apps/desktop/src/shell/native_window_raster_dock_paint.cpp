#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_length_input.hpp"
#include "native_window_raster_dock_metrics.hpp"
#include "pikdraw/document/commands/raster_edit_commands.hpp"
#include "pikdraw/raster/analysis/histogram_service.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

namespace pikdraw::desktop {

namespace {

#ifdef _WIN32
COLORREF hsv_to_colorref(double h_deg, double s, double v) {
    double h = std::fmod(h_deg, 360.0);
    if (h < 0.0) h += 360.0;
    s = std::clamp(s, 0.0, 1.0);
    v = std::clamp(v, 0.0, 1.0);
    const double c = v * s;
    const double x = c * (1.0 - std::abs(std::fmod(h / 60.0, 2.0) - 1.0));
    const double m = v - c;
    double r1 = 0.0, g1 = 0.0, b1 = 0.0;
    if (h < 60.0) { r1 = c; g1 = x; }
    else if (h < 120.0) { r1 = x; g1 = c; }
    else if (h < 180.0) { g1 = c; b1 = x; }
    else if (h < 240.0) { g1 = x; b1 = c; }
    else if (h < 300.0) { r1 = x; b1 = c; }
    else { r1 = c; b1 = x; }
    const int r = std::clamp(static_cast<int>(std::lround((r1 + m) * 255.0)), 0, 255);
    const int g = std::clamp(static_cast<int>(std::lround((g1 + m) * 255.0)), 0, 255);
    const int b = std::clamp(static_cast<int>(std::lround((b1 + m) * 255.0)), 0, 255);
    return RGB(r, g, b);
}

COLORREF lerp_colorref(COLORREF a, COLORREF b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    const int r = static_cast<int>(std::lround(GetRValue(a) * (1.0 - t) + GetRValue(b) * t));
    const int g = static_cast<int>(std::lround(GetGValue(a) * (1.0 - t) + GetGValue(b) * t));
    const int bl = static_cast<int>(std::lround(GetBValue(a) * (1.0 - t) + GetBValue(b) * t));
    return RGB(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(bl, 0, 255));
}

void draw_color_reference_strip(
    HDC dc, const RECT& rc,
    const std::function<COLORREF(double)>& color_at_t,
    COLORREF border_col) {
    const int width = std::max(1L, rc.right - rc.left);
    const int slices = std::clamp(width / 3, 16, 72);
    for (int i = 0; i < slices; ++i) {
        const double t = (slices > 1) ? static_cast<double>(i) / (slices - 1) : 0.0;
        const int x0 = rc.left + (i * width) / slices;
        const int x1 = (i + 1 == slices) ? rc.right : (rc.left + ((i + 1) * width) / slices);
        RECT seg{x0, rc.top, x1, rc.bottom};
        HBRUSH br = CreateSolidBrush(color_at_t(t));
        FillRect(dc, &seg, br);
        DeleteObject(br);
    }
    HPEN border_pen = CreatePen(PS_SOLID, 1, border_col);
    HGDIOBJ old_pen = SelectObject(dc, border_pen);
    HGDIOBJ old_br = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(dc, old_br);
    SelectObject(dc, old_pen);
    DeleteObject(border_pen);
}

void draw_slider_thumb(HDC dc, int cx, int top, int bottom, COLORREF accent, bool is_dark) {
    const int mid_y = (top + bottom) / 2;
    HPEN pen = CreatePen(PS_SOLID, 2, accent);
    HBRUSH br = CreateSolidBrush(is_dark ? RGB(15, 23, 42) : RGB(255, 255, 255));
    HGDIOBJ old_p = SelectObject(dc, pen);
    HGDIOBJ old_b = SelectObject(dc, br);
    Ellipse(dc, cx - 5, mid_y - 5, cx + 6, mid_y + 6);
    SelectObject(dc, old_b);
    SelectObject(dc, old_p);
    DeleteObject(br);
    DeleteObject(pen);
}

void draw_level_triangle(HDC dc, int cx, int top_y, COLORREF fill_col, COLORREF border_col) {
    POINT pts[3] = {
        {cx, top_y},
        {cx - 5, top_y + 9},
        {cx + 5, top_y + 9}
    };
    HBRUSH br = CreateSolidBrush(fill_col);
    HPEN pen = CreatePen(PS_SOLID, 1, border_col);
    HGDIOBJ old_b = SelectObject(dc, br);
    HGDIOBJ old_p = SelectObject(dc, pen);
    Polygon(dc, pts, 3);
    SelectObject(dc, old_b);
    SelectObject(dc, old_p);
    DeleteObject(br);
    DeleteObject(pen);
}

double levels_gamma_position(double gamma) {
    return std::clamp(0.5 - 0.4 * std::log10(std::clamp(gamma, 0.1, 10.0)),
                      0.1, 0.9);
}

double levels_gamma_from_position(double position) {
    return std::clamp(std::pow(10.0,
        (0.5 - std::clamp(position, 0.1, 0.9)) * 2.5), 0.1, 10.0);
}
#endif

} // namespace

document::RasterSelectiveHslParams NativeWindow::selected_bitmap_selective_hsl(
    document::RasterHueRange range) const {
    document::RasterSelectiveHslParams params{};
    params.range = range;
    const auto& selected = root().vector_controller().selection().selected_ids();
    const auto snapshot = root().document().current_snapshot();
    if (selected.size() != 1 || !snapshot) return params;
    const auto* object = snapshot->objects().lookup_ref(selected.front());
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return params;
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        if (const auto* sel = std::get_if<document::RasterSelectiveHslParams>(&node->parameters);
            sel && sel->range == range) {
            return *sel;
        }
    }
    return params;
}

bool NativeWindow::set_selected_bitmap_selective_hsl_value(
    document::RasterHueRange range, int component, double value) {
    if (!std::isfinite(value) || !can_edit_selected_bitmap(true)) return false;
    auto params = selected_bitmap_selective_hsl(range);
    if (component == 0) {
        params.hue_degrees = std::clamp(value, -30.0, 30.0);
    } else if (component == 1) {
        params.saturation_percent = std::clamp(value, -100.0, 100.0);
    } else if (component == 2) {
        params.lightness_percent = std::clamp(value, -100.0, 100.0);
    } else if (component == 3) {
        params.feather_percent = std::clamp(value, 0.0, 100.0);
    } else {
        return false;
    }

    const auto& selected = root().vector_controller().selection().selected_ids();
    const auto snapshot = root().document().current_snapshot();
    if (selected.size() != 1 || !snapshot) return false;
    const auto* object = snapshot->objects().lookup_ref(selected.front());
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return false;

    document::RasterEditNode updated;
    bool found = false;
    uint64_t max_id = 0;
    for (const auto& node : image->edit_graph.nodes) {
        max_id = (std::max)(max_id, node.id.value);
    }
    for (auto node = image->edit_graph.nodes.rbegin();
         node != image->edit_graph.nodes.rend(); ++node) {
        if (const auto* sel = std::get_if<document::RasterSelectiveHslParams>(&node->parameters);
            sel && sel->range == range) {
            updated = *node;
            updated.parameters = params;
            found = true;
            break;
        }
    }
    if (!found) {
        if (max_id == std::numeric_limits<uint64_t>::max()) return false;
        updated = document::RasterEditNode{{max_id + 1}, true, params, 1u, std::nullopt};
    }
    return root().document().execute_command(
        std::make_unique<document::SetRasterEditNodeCommand>(
            selected.front(), std::move(updated))).success;
}

void NativeWindow::focus_raster_adjustment_in_dock(int tab, int hsl_channel) {
    active_dock_tab_ = 0;
    show_style_inspector_ = false;
    show_align_panel_ = false;
    show_snap_properties_ = false;
    if (tab >= 1 && tab <= 6) {
        raster_adjustment_inspector_tab_ = static_cast<uint8_t>(tab);
    }
    if (hsl_channel >= 0 && hsl_channel <= 6) {
        raster_hsl_active_channel_ = static_cast<uint8_t>(hsl_channel);
    }
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

bool NativeWindow::auto_levels_selected_bitmap() {
    if (!can_edit_selected_bitmap(true)) return false;
#ifdef _WIN32
    const auto& selected = root().vector_controller().selection().selected_ids();
    const auto snapshot = root().document().current_snapshot();
    if (selected.size() != 1 || !snapshot ||
        !raster_source_histogram_service_) return false;
    const auto* object = snapshot->objects().lookup_ref(selected.front());
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return false;

    const auto hist = raster_source_histogram_service_->find(
        *snapshot, selected.front(), raster::RasterHistogramMode::Source);
    if (!hist || !hist->succeeded() ||
        hist->channels[0].coverage_units == 0) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đang chờ histogram nguồn; chưa áp dụng Auto Levels"
            : "Waiting for source histogram; Auto Levels was not applied";
        return false;
    }
    std::array<uint64_t, 256> auto_bins{};
    uint64_t coverage_units = 0;
    if (raster_levels_active_channel_ ==
        document::RasterToneChannel::Composite) {
        // Composite auto range pools the three encoded RGB channel samples.
        for (size_t channel = 0; channel < 3; ++channel) {
            coverage_units += hist->channels[channel].coverage_units;
            for (size_t bin = 0; bin < auto_bins.size(); ++bin)
                auto_bins[bin] += hist->channels[channel].display_bins[bin];
        }
    } else {
        const size_t index = raster_levels_active_channel_ ==
                document::RasterToneChannel::Red ? 0u :
            raster_levels_active_channel_ ==
                document::RasterToneChannel::Green ? 1u : 2u;
        const auto& channel = hist->channels[index];
        coverage_units = channel.coverage_units;
        auto_bins = channel.display_bins;
    }
    if (coverage_units == 0) return false;
    const uint64_t low_target = std::max<uint64_t>(1, coverage_units / 100);
    const uint64_t high_target = coverage_units - low_target;
    if (high_target == 0) return false;
    double black_in = 0.0;
    double white_in = 1.0;
    {
        uint64_t accum = 0;
        int low_bin = 0;
        int high_bin = 255;
        for (int b = 0; b < 256; ++b) {
            accum += auto_bins[static_cast<size_t>(b)];
            if (accum >= low_target) {
                low_bin = b;
                break;
            }
        }
        accum = 0;
        for (int b = 0; b < 256; ++b) {
            accum += auto_bins[static_cast<size_t>(b)];
            if (accum >= high_target) {
                high_bin = b;
                break;
            }
        }
        if (high_bin <= low_bin + 8) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Histogram không đủ dải; chưa áp dụng Auto Levels"
                : "Histogram range is too narrow; Auto Levels was not applied";
            return false;
        }
        black_in = std::clamp(static_cast<double>(low_bin) / 255.0, 0.0, 0.85);
        white_in = std::clamp(static_cast<double>(high_bin) / 255.0, black_in + 0.05, 1.0);
    }
    auto parameters = selected_bitmap_levels();
    parameters.black_in = black_in;
    parameters.white_in = white_in;
    parameters.gamma = 1.0;
    const auto found = std::find_if(image->edit_graph.nodes.rbegin(),
        image->edit_graph.nodes.rend(), [this](const auto& node) {
            const auto* levels = std::get_if<document::RasterLevelsParams>(
                &node.parameters);
            return levels && levels->channel == raster_levels_active_channel_;
        });
    document::RasterEditNode updated;
    if (found != image->edit_graph.nodes.rend()) {
        updated = *found;
        updated.parameters = parameters;
    } else {
        uint64_t maximum_id = 0;
        for (const auto& node : image->edit_graph.nodes)
            maximum_id = (std::max)(maximum_id, node.id.value);
        if (maximum_id == std::numeric_limits<uint64_t>::max()) return false;
        updated = document::RasterEditNode{{maximum_id + 1}, true, parameters};
    }
    if (!root().document().execute_command(
            std::make_unique<document::SetRasterEditNodeCommand>(
                selected.front(), std::move(updated))).success)
        return false;
    status_msg_ = (language_ == UILanguage::Vietnamese)
        ? "Đã tự động tối ưu điểm Đen/Trắng (Auto Levels) · Ctrl+Z hoàn tác"
        : "Applied Auto Levels optimization · Ctrl+Z to undo";
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
#else
    return false;
#endif
}

bool NativeWindow::reset_selected_bitmap_tab_adjustments(int tab) {
    if (!can_edit_selected_bitmap(true)) return false;
    const auto& selected = root().vector_controller().selection().selected_ids();
    const auto snapshot = root().document().current_snapshot();
    if (selected.size() != 1 || !snapshot) return false;
    const auto* object = snapshot->objects().lookup_ref(selected.front());
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return false;
    auto graph = image->edit_graph;
    const auto should_reset = [this, tab](const document::RasterEditNode& node) {
        switch (tab) {
        case 1:
            return std::holds_alternative<document::RasterExposureParams>(node.parameters) ||
                std::holds_alternative<document::RasterLightParams>(node.parameters);
        case 2:
            if (std::holds_alternative<document::RasterWhiteBalanceParams>(node.parameters))
                return true;
            if (raster_hsl_active_channel_ == 0)
                return std::holds_alternative<document::RasterHslParams>(node.parameters) ||
                    std::holds_alternative<document::RasterVibranceParams>(node.parameters);
            if (const auto* selective = std::get_if<document::RasterSelectiveHslParams>(
                    &node.parameters))
                return static_cast<uint8_t>(selective->range) ==
                    raster_hsl_active_channel_ - 1;
            return false;
        case 3:
            if (const auto* levels = std::get_if<document::RasterLevelsParams>(
                    &node.parameters))
                return levels->channel == raster_levels_active_channel_;
            return false;
        case 4:
            if (const auto* balance = std::get_if<document::RasterColorBalanceParams>(
                    &node.parameters))
                return balance->range == raster_color_balance_active_range_;
            return false;
        case 5:
            if (const auto* curves = std::get_if<document::RasterCurvesParams>(
                    &node.parameters))
                return curves->channel == raster_curves_active_channel_;
            return false;
        default:
            return false;
        }
    };
    std::erase_if(graph.nodes, should_reset);
    const auto result = root().document().execute_command(
        std::make_unique<document::SetRasterEditGraphCommand>(
            selected.front(), std::move(graph), snapshot->epoch(),
            snapshot->revision()));
    if (!result.success) return false;
    status_msg_ = (language_ == UILanguage::Vietnamese)
        ? "Đã đặt lại mục chỉnh ảnh đang chọn bằng một bước · Ctrl+Z hoàn tác"
        : "Reset the selected adjustment scope in one step · Ctrl+Z undoes";
#ifdef _WIN32
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    return true;
}

#ifdef _WIN32
void NativeWindow::paint_raster_inspector_dock(
    HDC mem_dc, int panel_left, int panel_width, int content_y) {
    const auto snapshot = root().document().current_snapshot();
    const auto& selection = root().vector_controller().selection().selected_ids();
    if (selection.size() != 1 || !snapshot) return;
    const auto* selected_object = snapshot->objects().lookup_ref(selection.front());
    const auto* selected_image = selected_object
        ? std::get_if<document::ImageObject>(selected_object) : nullptr;
    if (!selected_image) return;

    ensure_ui_fonts();
    const bool is_dark = (theme_ == ThemeMode::CyberDark);
    const bool is_vi = (language_ == UILanguage::Vietnamese);
    const COLORREF col_border = is_dark ? RGB(29, 38, 54) : RGB(203, 213, 225);
    const COLORREF col_teal = is_dark ? RGB(0, 242, 195) : RGB(0, 135, 106);
    const COLORREF col_text_white = is_dark ? RGB(241, 245, 249) : RGB(15, 23, 42);
    const COLORREF col_text_muted = is_dark ? RGB(148, 163, 184) : RGB(100, 116, 139);
    const COLORREF col_active_tab = is_dark ? RGB(22, 37, 54) : RGB(236, 242, 248);
    const COLORREF col_input_bg = is_dark ? RGB(9, 13, 22) : RGB(241, 245, 249);
    const COLORREF col_card_bg = is_dark ? RGB(14, 21, 33) : RGB(248, 250, 252);

    auto draw_dock_num_box = [&](RECT r, const std::string& val_str, NumericFieldId fid) {
        const bool is_editing = (active_num_field_ == fid);
        HBRUSH b_in = CreateSolidBrush(is_editing
            ? (is_dark ? RGB(20, 36, 52) : RGB(255, 255, 255)) : col_input_bg);
        HPEN p_in = CreatePen(PS_SOLID, is_editing ? 2 : 1, is_editing ? col_teal : col_border);
        HGDIOBJ ob = SelectObject(mem_dc, b_in);
        HGDIOBJ op = SelectObject(mem_dc, p_in);
        RoundRect(mem_dc, r.left, r.top, r.right, r.bottom, 4, 4);
        SelectObject(mem_dc, ob);
        SelectObject(mem_dc, op);
        DeleteObject(b_in);
        DeleteObject(p_in);

        SelectObject(mem_dc, font_mono_ ? font_mono_ : font_secondary_);
        SetTextColor(mem_dc, is_editing ? col_teal : col_text_white);
        std::string disp = is_editing ? (num_input_buffer_ + "|") : val_str;
        RECT tr = {r.left + 6, r.top, r.right - 4, r.bottom};
        DrawTextA(mem_dc, disp.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    };

    const int histogram_top = content_y + raster_dock_metrics::kHistogramTopOffset;
    SelectObject(mem_dc, font_be_vietnam_bold_);
    SetTextColor(mem_dc, col_teal);
    RECT title_rc = {panel_left, histogram_top, panel_left + 130, histogram_top + 18};
    DrawTextW(mem_dc, is_vi ? L"HISTOGRAM ẢNH" : L"IMAGE HISTOGRAM", -1,
              &title_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Quick [Auto] and [Reset] buttons on the right of Histogram header
    auto draw_mini_action = [&](RECT rc, const wchar_t* label) {
        HBRUSH br = CreateSolidBrush(col_active_tab);
        HPEN pen = CreatePen(PS_SOLID, 1, col_border);
        HGDIOBJ ob = SelectObject(mem_dc, br);
        HGDIOBJ op = SelectObject(mem_dc, pen);
        RoundRect(mem_dc, rc.left, rc.top, rc.right, rc.bottom, 4, 4);
        SelectObject(mem_dc, ob);
        SelectObject(mem_dc, op);
        DeleteObject(br);
        DeleteObject(pen);
        SelectObject(mem_dc, font_secondary_);
        SetTextColor(mem_dc, col_teal);
        DrawTextW(mem_dc, label, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    };
    draw_mini_action({panel_left + panel_width - 96, histogram_top, panel_left + panel_width - 50, histogram_top + 18},
                     is_vi ? L"Tự động" : L"Auto");
    draw_mini_action({panel_left + panel_width - 46, histogram_top, panel_left + panel_width, histogram_top + 18},
                     is_vi ? L"Đặt lại" : L"Reset");

    const std::array<const wchar_t*, 4> labels = {
        is_vi ? L" Sáng " : L" Luma ", L" R ", L" G ", L" B "};
    const std::array<COLORREF, 4> colors = {
        col_text_white, RGB(239, 68, 68), RGB(34, 197, 94), RGB(59, 130, 246)};
    const int gap = 4;
    const int button_w = (panel_width - gap * 3) / 4;
    SelectObject(mem_dc, font_secondary_);
    for (size_t ch = 0; ch < labels.size(); ++ch) {
        const int left = panel_left + static_cast<int>(ch) * (button_w + gap);
        RECT button_rc = {left, histogram_top + 20, left + button_w, histogram_top + 38};
        const bool enabled = raster_histogram_channels_visible_[ch];
        HBRUSH fill = CreateSolidBrush(enabled ? col_active_tab : col_input_bg);
        HPEN pen = CreatePen(PS_SOLID, 1, enabled ? colors[ch] : col_border);
        HGDIOBJ old_brush = SelectObject(mem_dc, fill);
        HGDIOBJ old_pen = SelectObject(mem_dc, pen);
        RoundRect(mem_dc, button_rc.left, button_rc.top, button_rc.right, button_rc.bottom, 4, 4);
        SelectObject(mem_dc, old_brush);
        SelectObject(mem_dc, old_pen);
        DeleteObject(fill);
        DeleteObject(pen);
        SetTextColor(mem_dc, enabled ? colors[ch] : col_text_muted);
        DrawTextW(mem_dc, labels[ch], -1, &button_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    RECT graph_rc = {panel_left, histogram_top + 42, panel_left + panel_width, histogram_top + 90};
    HBRUSH graph_fill = CreateSolidBrush(col_input_bg);
    HPEN graph_pen = CreatePen(PS_SOLID, 1, col_border);
    HGDIOBJ old_brush = SelectObject(mem_dc, graph_fill);
    HGDIOBJ old_pen = SelectObject(mem_dc, graph_pen);
    RoundRect(mem_dc, graph_rc.left, graph_rc.top, graph_rc.right, graph_rc.bottom, 4, 4);
    SelectObject(mem_dc, old_brush);
    SelectObject(mem_dc, old_pen);
    DeleteObject(graph_fill);
    DeleteObject(graph_pen);

    const auto histogram = raster_histogram_service_
        ? raster_histogram_service_->find(
              *snapshot, selection.front(), raster::RasterHistogramMode::Edited)
        : nullptr;
    if (histogram && histogram->succeeded() &&
        histogram->channels[0].coverage_units > 0) {
        uint64_t peak = 1;
        const std::array<const std::array<uint64_t, 256>*, 4> bins = {
            &histogram->channels[0].display_bins, &histogram->channels[1].display_bins,
            &histogram->channels[2].display_bins, &histogram->channels[3].display_bins};
        for (size_t ch = 0; ch < bins.size(); ++ch) {
            if (!raster_histogram_channels_visible_[ch]) continue;
            for (const uint64_t count : *bins[ch]) peak = (std::max)(peak, count);
        }
        const int plot_left = graph_rc.left + 4;
        const int plot_right = graph_rc.right - 4;
        const int plot_top = graph_rc.top + 4;
        const int plot_bottom = graph_rc.bottom - 4;
        const int plot_w = (std::max)(1, plot_right - plot_left);
        const int plot_h = (std::max)(1, plot_bottom - plot_top);
        const double denom = std::log1p(static_cast<double>(peak));
        for (size_t ch = 0; ch < bins.size(); ++ch) {
            if (!raster_histogram_channels_visible_[ch]) continue;
            HPEN curve_pen = CreatePen(PS_SOLID, 1, colors[ch]);
            HGDIOBJ prev_pen = SelectObject(mem_dc, curve_pen);
            for (int x = 0; x < plot_w; ++x) {
                const size_t bin = static_cast<size_t>((x * 255) / (std::max)(1, plot_w - 1));
                const double norm = denom > 0.0
                    ? std::log1p(static_cast<double>((*bins[ch])[bin])) / denom
                    : 0.0;
                const int y = plot_bottom - static_cast<int>(std::lround(norm * plot_h));
                if (x == 0) MoveToEx(mem_dc, plot_left + x, y, nullptr);
                else LineTo(mem_dc, plot_left + x, y);
            }
            SelectObject(mem_dc, prev_pen);
            DeleteObject(curve_pen);
        }
    } else {
        SetTextColor(mem_dc, col_text_muted);
        DrawTextW(mem_dc, is_vi ? L"Đang tính histogram…" : L"Computing histogram…",
                  -1, &graph_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // Draw interactive Levels Black / Gamma / White markers directly on bottom edge of Histogram
    {
        const auto lv = selected_bitmap_levels();
        const int plot_l = graph_rc.left + 4;
        const int plot_w = std::max(1L, (graph_rc.right - 4) - plot_l);
        const int bx = plot_l + static_cast<int>(std::lround(lv.black_in * plot_w));
        const int wx = plot_l + static_cast<int>(std::lround(lv.white_in * plot_w));
        const double gamma_norm = levels_gamma_position(lv.gamma);
        const int gx = bx + static_cast<int>(std::lround((wx - bx) * gamma_norm));
        draw_level_triangle(mem_dc, bx, graph_rc.bottom - 6, RGB(15, 23, 42), col_teal);
        draw_level_triangle(mem_dc, gx, graph_rc.bottom - 6, RGB(148, 163, 184), col_border);
        draw_level_triangle(mem_dc, wx, graph_rc.bottom - 6, RGB(248, 250, 252), col_teal);
    }

    RECT clip_rc = {panel_left, histogram_top + 94, panel_left + panel_width, histogram_top + 114};
    std::wstring clip_text = is_vi ? L"Độ phơi sáng (EV)" : L"Exposure (EV)";
    const auto luma_covered = (histogram && histogram->succeeded())
        ? histogram->channels[0].covered_pixels : 0ULL;
    if (luma_covered > 0) {
        const auto pct = [&](uint64_t count) {
        return (100.0 * static_cast<double>(count)) /
            static_cast<double>(histogram->channels[0].coverage_units);
        };
        std::wostringstream stream;
        stream << std::fixed << std::setprecision(1)
               << (is_vi ? L"Tối " : L"Blk ") << pct(histogram->channels[0].clipped_low_units)
               << (is_vi ? L"% · Sáng " : L"% · Wht ") << pct(histogram->channels[0].clipped_high_units) << L"%";
        clip_text = stream.str();
    }
    SetTextColor(mem_dc, col_text_muted);
    RECT clip_label_rc = {clip_rc.left, clip_rc.top, clip_rc.left + 148, clip_rc.bottom};
    DrawTextW(mem_dc, clip_text.c_str(), -1, &clip_label_rc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    std::ostringstream exposure_text;
    exposure_text << std::fixed << std::setprecision(2)
                  << selected_bitmap_exposure_ev().value_or(0.0) << " EV";
    draw_dock_num_box({panel_left + panel_width - 96, histogram_top + 92,
                       panel_left + panel_width, histogram_top + 116},
                      exposure_text.str(), NumericFieldId::RasterExposureEv);

    // 6-tab Adjustment selector (exact raster_dock_metrics coordinates preserved)
    const int tabs_top = histogram_top + raster_dock_metrics::kRasterAdjustmentTabsTopOffset;
    constexpr int tab_gap = raster_dock_metrics::kRasterAdjustmentTabGap;
    const int tab_w = (panel_width - tab_gap *
                       (raster_dock_metrics::kRasterAdjustmentTabCount - 1)) /
                      raster_dock_metrics::kRasterAdjustmentTabCount;
    const std::array<const wchar_t*, raster_dock_metrics::kRasterAdjustmentTabCount> tab_labels = {
        is_vi ? L"Sáng" : L"Light",
        is_vi ? L"Màu" : L"Color",
        L"Levels",
        is_vi ? L"Cân" : L"Bal",
        L"Curves",
        is_vi ? L"Khác" : L"More"};
    SelectObject(mem_dc, font_secondary_);
    for (int t = 0; t < raster_dock_metrics::kRasterAdjustmentTabCount; ++t) {
        const int left = panel_left + t * (tab_w + tab_gap);
        const int right = (t == raster_dock_metrics::kRasterAdjustmentTabCount - 1)
            ? (panel_left + panel_width) : (left + tab_w);
        RECT tab_rc = {left, tabs_top, right,
                       tabs_top + raster_dock_metrics::kRasterAdjustmentTabsHeight};
        const bool active = (raster_adjustment_inspector_tab_ == static_cast<uint8_t>(t + 1));
        HBRUSH fill = CreateSolidBrush(active ? col_active_tab : col_input_bg);
        HPEN pen = CreatePen(PS_SOLID, 1, active ? col_teal : col_border);
        HGDIOBJ ob = SelectObject(mem_dc, fill);
        HGDIOBJ op = SelectObject(mem_dc, pen);
        RoundRect(mem_dc, tab_rc.left, tab_rc.top, tab_rc.right, tab_rc.bottom, 4, 4);
        SelectObject(mem_dc, ob);
        SelectObject(mem_dc, op);
        DeleteObject(fill);
        DeleteObject(pen);
        SetTextColor(mem_dc, active ? col_teal : col_text_muted);
        DrawTextW(mem_dc, tab_labels[static_cast<size_t>(t)], -1, &tab_rc,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    const int col_gap = 8;
    const int col_w = (panel_width - col_gap) / 2;
    const int label_w = 48;
    auto draw_compact_metric = [&](int col, int row_y, int row_h, const wchar_t* label,
                                   double value, NumericFieldId field,
                                   const char* suffix = "%", int precision = 0) {
        const int left = panel_left + col * (col_w + col_gap);
        SelectObject(mem_dc, font_secondary_);
        SetTextColor(mem_dc, col_text_muted);
        RECT label_rc = {left, row_y, left + label_w, row_y + row_h};
        DrawTextW(mem_dc, label, -1, &label_rc,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        std::ostringstream text;
        text << std::fixed << std::setprecision(precision) << value << suffix;
        draw_dock_num_box({left + label_w, row_y, left + col_w, row_y + row_h},
                          text.str(), field);
    };

    // Helper to draw a CorelDRAW / Illustrator visual slider with a Color Reference Strip
    auto draw_reference_slider_row = [&](int y_top, const wchar_t* label, const std::string& val_text,
                                         double norm_01, bool bipolar,
                                         const std::function<COLORREF(double)>& strip_color_fn) {
        SelectObject(mem_dc, font_secondary_);
        SetTextColor(mem_dc, col_text_white);
        RECT lbl_rc = {panel_left + 6, y_top, panel_left + panel_width - 56, y_top + 15};
        DrawTextW(mem_dc, label, -1, &lbl_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SelectObject(mem_dc, font_mono_ ? font_mono_ : font_secondary_);
        SetTextColor(mem_dc, col_teal);
        RECT val_rc = {panel_left + panel_width - 56, y_top, panel_left + panel_width - 6, y_top + 15};
        DrawTextA(mem_dc, val_text.c_str(), -1, &val_rc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        RECT bar_rc = {panel_left + 8, y_top + 16, panel_left + panel_width - 8, y_top + 26};
        draw_color_reference_strip(mem_dc, bar_rc, strip_color_fn, col_border);

        if (bipolar) {
            const int mid_x = (bar_rc.left + bar_rc.right) / 2;
            HPEN tick_pen = CreatePen(PS_SOLID, 1, col_text_white);
            HGDIOBJ old_p = SelectObject(mem_dc, tick_pen);
            MoveToEx(mem_dc, mid_x, bar_rc.top - 2, nullptr);
            LineTo(mem_dc, mid_x, bar_rc.bottom + 2);
            SelectObject(mem_dc, old_p);
            DeleteObject(tick_pen);
        }
        const int thumb_x = bar_rc.left + static_cast<int>(
            std::lround(std::clamp(norm_01, 0.0, 1.0) * (bar_rc.right - bar_rc.left)));
        draw_slider_thumb(mem_dc, thumb_x, bar_rc.top, bar_rc.bottom, col_teal, is_dark);
    };

    auto draw_studio_card_frame = [&](int top_y, int bottom_y, const wchar_t* header_title) {
        HBRUSH card_br = CreateSolidBrush(col_card_bg);
        HPEN card_pen = CreatePen(PS_SOLID, 1, col_border);
        HGDIOBJ ob = SelectObject(mem_dc, card_br);
        HGDIOBJ op = SelectObject(mem_dc, card_pen);
        RoundRect(mem_dc, panel_left, top_y, panel_left + panel_width, bottom_y, 6, 6);
        SelectObject(mem_dc, ob);
        SelectObject(mem_dc, op);
        DeleteObject(card_br);
        DeleteObject(card_pen);

        SelectObject(mem_dc, font_be_vietnam_bold_);
        SetTextColor(mem_dc, col_teal);
        RECT hdr_rc = {panel_left + 8, top_y + 4, panel_left + panel_width - 8, top_y + 20};
        DrawTextW(mem_dc, header_title, -1, &hdr_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    };

    if (raster_adjustment_inspector_tab_ == 1) {
        const auto light = selected_bitmap_light();
        const int light_top = histogram_top + raster_dock_metrics::kLightFieldsTopOffset;
        const int row_h = raster_dock_metrics::kLightFieldHeight;
        const int row_step = raster_dock_metrics::kLightFieldRowSpacing;
        draw_compact_metric(0, light_top, row_h, is_vi ? L"Tương" : L"Cont",
                            light.contrast_percent, NumericFieldId::RasterLightContrast);
        draw_compact_metric(1, light_top, row_h, is_vi ? L"Sáng" : L"High",
                            light.highlights_percent, NumericFieldId::RasterLightHighlights);
        draw_compact_metric(0, light_top + row_step, row_h, is_vi ? L"Tối" : L"Shad",
                            light.shadows_percent, NumericFieldId::RasterLightShadows);
        draw_compact_metric(1, light_top + row_step, row_h, is_vi ? L"Trắng" : L"White",
                            light.whites_percent, NumericFieldId::RasterLightWhites);
        draw_compact_metric(0, light_top + row_step * 2, row_h, is_vi ? L"Đen" : L"Black",
                            light.blacks_percent, NumericFieldId::RasterLightBlacks);

        // CorelDRAW / Illustrator Interactive Tone & Light Sliders with Reference Strips
        const int studio_top = histogram_top + 244;
        draw_studio_card_frame(studio_top, studio_top + 216,
                               is_vi ? L"THANH KÉO ÁNH SÁNG & DẢI THAM CHIẾU" : L"INTERACTIVE TONE & REFERENCE STRIPS");
        const double ev = selected_bitmap_exposure_ev().value_or(0.0);
        const std::array<std::pair<const wchar_t*, double>, 6> rows = {{
            {is_vi ? L"Phơi sáng (EV)" : L"Exposure (EV)", ev},
            {is_vi ? L"Độ tương phản" : L"Contrast", light.contrast_percent},
            {is_vi ? L"Vùng sáng (Highlights)" : L"Highlights", light.highlights_percent},
            {is_vi ? L"Vùng tối (Shadows)" : L"Shadows", light.shadows_percent},
            {is_vi ? L"Điểm trắng (Whites)" : L"Whites", light.whites_percent},
            {is_vi ? L"Điểm đen (Blacks)" : L"Blacks", light.blacks_percent},
        }};
        for (int i = 0; i < 6; ++i) {
            const int ry = studio_top + 22 + i * 31;
            const double val = rows[static_cast<size_t>(i)].second;
            const double norm = (i == 0)
                ? std::clamp((val + 5.0) / 10.0, 0.0, 1.0)
                : std::clamp((val + 100.0) / 200.0, 0.0, 1.0);
            std::ostringstream vs;
            vs << std::fixed << std::setprecision(i == 0 ? 2 : 0) << val << (i == 0 ? " EV" : "%");
            draw_reference_slider_row(
                ry, rows[static_cast<size_t>(i)].first, vs.str(), norm, true,
                [i](double t) -> COLORREF {
                    if (i == 0) return lerp_colorref(RGB(12, 16, 24), RGB(255, 250, 235), t);
                    if (i == 1) return lerp_colorref(RGB(115, 122, 134), RGB(248, 250, 252), t);
                    if (i == 2) return lerp_colorref(RGB(100, 116, 139), RGB(255, 255, 255), t);
                    if (i == 3) return lerp_colorref(RGB(10, 14, 22), RGB(148, 163, 184), t);
                    if (i == 4) return lerp_colorref(RGB(160, 174, 192), RGB(255, 255, 255), t);
                    return lerp_colorref(RGB(5, 8, 14), RGB(100, 116, 139), t);
                });
        }
    } else if (raster_adjustment_inspector_tab_ == 2) {
        const auto hsl = selected_bitmap_hsl();
        auto field_hsl = hsl;
        if (raster_hsl_active_channel_ > 0) {
            const auto range = static_cast<document::RasterHueRange>(
                raster_hsl_active_channel_ - 1);
            const auto selective = selected_bitmap_selective_hsl(range);
            field_hsl.hue_degrees = selective.hue_degrees;
            field_hsl.saturation_percent = selective.saturation_percent;
            field_hsl.lightness_percent = selective.lightness_percent;
        }
        const auto wb = selected_bitmap_white_balance();
        const int color_top = histogram_top + raster_dock_metrics::kColorFieldsTopOffset;
        const int row_h = raster_dock_metrics::kColorFieldHeight;
        const int row_step = raster_dock_metrics::kColorFieldRowSpacing;
        draw_compact_metric(0, color_top, row_h, L"Hue",
                            field_hsl.hue_degrees, NumericFieldId::RasterHslHue, "°");
        draw_compact_metric(1, color_top, row_h, L"Sat",
                            field_hsl.saturation_percent, NumericFieldId::RasterHslSaturation);
        draw_compact_metric(0, color_top + row_step, row_h, is_vi ? L"Sáng" : L"Light",
                            field_hsl.lightness_percent, NumericFieldId::RasterHslLightness);
        draw_compact_metric(1, color_top + row_step, row_h, L"Vib",
                            selected_bitmap_vibrance_amount(), NumericFieldId::RasterVibranceAmount);

        const int wb_top = histogram_top + raster_dock_metrics::kWhiteBalanceFieldsTopOffset;
        const int wb_h = raster_dock_metrics::kWhiteBalanceFieldHeight;
        draw_compact_metric(0, wb_top, wb_h, is_vi ? L"Nhiệt" : L"Temp",
                            wb.temperature_shift_percent, NumericFieldId::RasterWhiteBalanceTemperature);
        draw_compact_metric(1, wb_top, wb_h, L"Tint",
                            wb.tint_percent, NumericFieldId::RasterWhiteBalanceTint);

        // CorelDRAW-style 7-Channel HSL Swatch Selector + Spectrum Reference Strips (between 210..256 and 292..474)
        const int ch_bar_top = histogram_top + 212;
        SelectObject(mem_dc, font_secondary_);
        SetTextColor(mem_dc, col_text_muted);
        RECT ch_lbl = {panel_left, ch_bar_top, panel_left + panel_width, ch_bar_top + 14};
        DrawTextW(mem_dc, is_vi ? L"Kênh màu HSL (Toàn ảnh / 6 dải màu chuyên biệt):" : L"HSL Channel (Master / 6 Selective Color Ranges):",
                  -1, &ch_lbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        const std::array<const wchar_t*, 7> ch_names = {L"All", L"R", L"Y", L"G", L"C", L"B", L"M"};
        const std::array<COLORREF, 7> ch_cols = {
            col_teal,
            RGB(239, 68, 68),
            RGB(234, 179, 8),
            RGB(34, 197, 94),
            RGB(6, 182, 212),
            RGB(59, 130, 246),
            RGB(217, 70, 239)};
        const int cgap = 3;
        const int cw = (panel_width - cgap * 6) / 7;
        for (int c = 0; c < 7; ++c) {
            const int cl = panel_left + c * (cw + cgap);
            const int cr = (c == 6) ? (panel_left + panel_width) : (cl + cw);
            RECT crc = {cl, ch_bar_top + 16, cr, ch_bar_top + 38};
            const bool active = (raster_hsl_active_channel_ == static_cast<uint8_t>(c));
            HBRUSH br = CreateSolidBrush(active ? col_active_tab : col_input_bg);
            HPEN pen = CreatePen(PS_SOLID, active ? 2 : 1, active ? ch_cols[static_cast<size_t>(c)] : col_border);
            HGDIOBJ ob = SelectObject(mem_dc, br);
            HGDIOBJ op = SelectObject(mem_dc, pen);
            RoundRect(mem_dc, crc.left, crc.top, crc.right, crc.bottom, 4, 4);
            SelectObject(mem_dc, ob);
            SelectObject(mem_dc, op);
            DeleteObject(br);
            DeleteObject(pen);
            SetTextColor(mem_dc, active ? ch_cols[static_cast<size_t>(c)] : col_text_white);
            DrawTextW(mem_dc, ch_names[static_cast<size_t>(c)], -1, &crc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        const int studio_top = histogram_top + 292;
        draw_studio_card_frame(studio_top, studio_top + 184,
                               is_vi ? L"DẢI MÀU THAM CHIẾU TRỰC QUAN (HSL & WB)" : L"VISUAL COLOR REFERENCE STRIPS (HSL & WB)");

        double cur_hue = hsl.hue_degrees;
        double cur_sat = hsl.saturation_percent;
        double cur_light = hsl.lightness_percent;
        double cur_fourth = selected_bitmap_vibrance_amount();
        double hue_max = 180.0;
        double base_hue_deg = 200.0;
        if (raster_hsl_active_channel_ > 0) {
            const auto range = static_cast<document::RasterHueRange>(raster_hsl_active_channel_ - 1);
            const auto sel = selected_bitmap_selective_hsl(range);
            cur_hue = sel.hue_degrees;
            cur_sat = sel.saturation_percent;
            cur_light = sel.lightness_percent;
            cur_fourth = sel.feather_percent;
            hue_max = 30.0;
            constexpr double kRangeCenterHues[6] = {0.0, 60.0, 120.0, 180.0, 240.0, 300.0};
            base_hue_deg = kRangeCenterHues[(raster_hsl_active_channel_ - 1) % 6];
        } else {
            base_hue_deg = std::fmod(180.0 + cur_hue + 360.0, 360.0);
        }

        // 1. Hue Spectrum Reference Strip (-180..+180 or -30..+30)
        {
            std::ostringstream hs;
            hs << std::fixed << std::setprecision(0) << cur_hue << "°";
            draw_reference_slider_row(
                studio_top + 22,
                is_vi ? L"Đổi sắc độ (Hue Spectrum)" : L"Hue Shift Spectrum",
                hs.str(),
                (cur_hue + hue_max) / (2.0 * hue_max),
                true,
                [&](double t) -> COLORREF {
                    if (raster_hsl_active_channel_ == 0) {
                        return hsv_to_colorref(t * 360.0, 0.85, 0.92);
                    }
                    const double h = base_hue_deg + (t - 0.5) * 90.0;
                    return hsv_to_colorref(h, 0.85, 0.92);
                });
        }
        // 2. Saturation Reference Strip (-100..+100%)
        {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(0) << cur_sat << "%";
            draw_reference_slider_row(
                studio_top + 53,
                is_vi ? L"Độ bão hòa (Saturation)" : L"Saturation",
                ss.str(),
                (cur_sat + 100.0) / 200.0,
                true,
                [&](double t) -> COLORREF {
                    return hsv_to_colorref(base_hue_deg, t, 0.88);
                });
        }
        // 3. Lightness Reference Strip (-100..+100%)
        {
            std::ostringstream ls;
            ls << std::fixed << std::setprecision(0) << cur_light << "%";
            draw_reference_slider_row(
                studio_top + 84,
                is_vi ? L"Độ sáng màu (Lightness)" : L"Lightness",
                ls.str(),
                (cur_light + 100.0) / 200.0,
                true,
                [&](double t) -> COLORREF {
                    if (t < 0.5) {
                        return lerp_colorref(RGB(10, 12, 18), hsv_to_colorref(base_hue_deg, 0.85, 0.90), t * 2.0);
                    }
                    return lerp_colorref(hsv_to_colorref(base_hue_deg, 0.85, 0.90), RGB(255, 255, 255), (t - 0.5) * 2.0);
                });
        }
        // 4. Vibrance / Selective Feather Reference Strip
        {
            std::ostringstream fs;
            fs << std::fixed << std::setprecision(0) << cur_fourth << "%";
            const bool is_master = (raster_hsl_active_channel_ == 0);
            draw_reference_slider_row(
                studio_top + 115,
                is_master
                    ? (is_vi ? L"Tăng màu thông minh (Vibrance)" : L"Vibrance")
                    : (is_vi ? L"Độ mềm biên dải màu (Feather)" : L"Range Feather"),
                fs.str(),
                is_master ? ((cur_fourth + 100.0) / 200.0) : (cur_fourth / 100.0),
                is_master,
                [&](double t) -> COLORREF {
                    return lerp_colorref(RGB(100, 116, 139), hsv_to_colorref(base_hue_deg, 0.95, 0.95), t);
                });
        }
        // 5. White Balance Temperature Reference Strip (Blue <-> Amber)
        {
            std::ostringstream ts;
            ts << std::fixed << std::setprecision(0) << wb.temperature_shift_percent << "%";
            draw_reference_slider_row(
                studio_top + 146,
                is_vi ? L"Nhiệt độ màu (Lạnh ↔ Ấm)" : L"WB Temperature (Cool ↔ Warm)",
                ts.str(),
                (wb.temperature_shift_percent + 100.0) / 200.0,
                true,
                [](double t) -> COLORREF {
                    if (t < 0.5) return lerp_colorref(RGB(37, 99, 235), RGB(226, 232, 240), t * 2.0);
                    return lerp_colorref(RGB(226, 232, 240), RGB(245, 158, 11), (t - 0.5) * 2.0);
                });
        }
    } else if (raster_adjustment_inspector_tab_ == 3) {
        const auto levels = selected_bitmap_levels();
        const int ch_top = histogram_top + raster_dock_metrics::kLevelsChannelButtonsTopOffset;
        const int ch_h = raster_dock_metrics::kLevelsChannelButtonsHeight;
        const std::array<const wchar_t*, 4> ch_labels = {L"RGB", L"R", L"G", L"B"};
        const std::array<COLORREF, 4> ch_colors = {
            col_teal, RGB(239, 68, 68), RGB(34, 197, 94), RGB(59, 130, 246)};
        for (int ch = 0; ch < 4; ++ch) {
            const int left = panel_left + ch * (button_w + gap);
            RECT button_rc = {left, ch_top, left + button_w, ch_top + ch_h};
            const bool active = static_cast<int>(levels.channel) == ch;
            HBRUSH fill = CreateSolidBrush(active ? col_active_tab : col_input_bg);
            HPEN pen = CreatePen(PS_SOLID, 1, active ? ch_colors[static_cast<size_t>(ch)] : col_border);
            HGDIOBJ ob = SelectObject(mem_dc, fill);
            HGDIOBJ op = SelectObject(mem_dc, pen);
            RoundRect(mem_dc, button_rc.left, button_rc.top, button_rc.right, button_rc.bottom, 4, 4);
            SelectObject(mem_dc, ob);
            SelectObject(mem_dc, op);
            DeleteObject(fill);
            DeleteObject(pen);
            SetTextColor(mem_dc, active ? ch_colors[static_cast<size_t>(ch)] : col_text_muted);
            DrawTextW(mem_dc, ch_labels[static_cast<size_t>(ch)], -1, &button_rc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        const int fields_top = histogram_top + raster_dock_metrics::kLevelsFieldsTopOffset;
        const int row_h = raster_dock_metrics::kLevelsFieldHeight;
        const int row_step = raster_dock_metrics::kLevelsFieldRowSpacing;
        draw_compact_metric(0, fields_top, row_h, is_vi ? L"Đen" : L"InB",
                            levels.black_in * 100.0, NumericFieldId::RasterLevelsBlackIn);
        draw_compact_metric(1, fields_top, row_h, is_vi ? L"Trắng" : L"InW",
                            levels.white_in * 100.0, NumericFieldId::RasterLevelsWhiteIn);
        draw_compact_metric(0, fields_top + row_step, row_h, L"Gamma",
                            levels.gamma, NumericFieldId::RasterLevelsGamma, "", 2);
        draw_compact_metric(1, fields_top + row_step, row_h, is_vi ? L"RaĐ" : L"OutB",
                            levels.black_out * 100.0, NumericFieldId::RasterLevelsBlackOut);
        draw_compact_metric(0, fields_top + row_step * 2, row_h, is_vi ? L"RaT" : L"OutW",
                            levels.white_out * 100.0, NumericFieldId::RasterLevelsWhiteOut);

        // CorelDRAW / Illustrator Interactive Input & Output Level Ramps with Draggable Handles
        const int studio_top = histogram_top + 266;
        draw_studio_card_frame(studio_top, studio_top + 164,
                               is_vi ? L"DẢI SẮC ĐỘ ĐẦU VÀO / ĐẦU RA (KÉO CHUỘT)" : L"INTERACTIVE INPUT / OUTPUT LEVEL RAMPS");

        COLORREF peak_col = RGB(255, 255, 255);
        if (levels.channel == document::RasterToneChannel::Red) peak_col = RGB(239, 68, 68);
        else if (levels.channel == document::RasterToneChannel::Green) peak_col = RGB(34, 197, 94);
        else if (levels.channel == document::RasterToneChannel::Blue) peak_col = RGB(59, 130, 246);

        // Input Ramp
        SelectObject(mem_dc, font_secondary_);
        SetTextColor(mem_dc, col_text_white);
        RECT in_lbl = {panel_left + 8, studio_top + 24, panel_left + panel_width - 8, studio_top + 40};
        DrawTextW(mem_dc, is_vi ? L"Dải đầu vào (Đen · Gamma · Trắng):" : L"Input Levels (Black · Gamma · White):",
                  -1, &in_lbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT in_bar = {panel_left + 10, studio_top + 42, panel_left + panel_width - 10, studio_top + 56};
        draw_color_reference_strip(mem_dc, in_bar,
                                   [peak_col](double t) { return lerp_colorref(RGB(8, 10, 16), peak_col, t); },
                                   col_border);
        const int bar_w = std::max(1L, in_bar.right - in_bar.left);
        const int in_bx = in_bar.left + static_cast<int>(std::lround(levels.black_in * bar_w));
        const int in_wx = in_bar.left + static_cast<int>(std::lround(levels.white_in * bar_w));
        const double gamma_norm = levels_gamma_position(levels.gamma);
        const int in_gx = in_bx + static_cast<int>(std::lround((in_wx - in_bx) * gamma_norm));
        draw_level_triangle(mem_dc, in_bx, in_bar.bottom + 1, RGB(15, 23, 42), col_teal);
        draw_level_triangle(mem_dc, in_gx, in_bar.bottom + 1, RGB(148, 163, 184), col_text_white);
        draw_level_triangle(mem_dc, in_wx, in_bar.bottom + 1, RGB(255, 255, 255), col_teal);

        // Output Ramp
        SetTextColor(mem_dc, col_text_white);
        RECT out_lbl = {panel_left + 8, studio_top + 74, panel_left + panel_width - 8, studio_top + 90};
        DrawTextW(mem_dc, is_vi ? L"Dải đầu ra (Giới hạn Đen · Trắng):" : L"Output Levels (Black · White Limits):",
                  -1, &out_lbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT out_bar = {panel_left + 10, studio_top + 92, panel_left + panel_width - 10, studio_top + 106};
        draw_color_reference_strip(mem_dc, out_bar,
                                   [peak_col](double t) { return lerp_colorref(RGB(8, 10, 16), peak_col, t); },
                                   col_border);
        const int out_bx = out_bar.left + static_cast<int>(std::lround(levels.black_out * bar_w));
        const int out_wx = out_bar.left + static_cast<int>(std::lround(levels.white_out * bar_w));
        draw_level_triangle(mem_dc, out_bx, out_bar.bottom + 1, RGB(15, 23, 42), col_teal);
        draw_level_triangle(mem_dc, out_wx, out_bar.bottom + 1, RGB(255, 255, 255), col_teal);

        // Auto Levels & Reset buttons inside card
        draw_mini_action({panel_left + 10, studio_top + 128, panel_left + panel_width / 2 - 4, studio_top + 154},
                         is_vi ? L"Tự động tối ưu (Auto)" : L"Auto Optimize");
        draw_mini_action({panel_left + panel_width / 2 + 4, studio_top + 128, panel_left + panel_width - 10, studio_top + 154},
                         is_vi ? L"Đặt lại Levels" : L"Reset Levels");
    } else if (raster_adjustment_inspector_tab_ == 4) {
        const auto balance = selected_bitmap_color_balance();
        const int range_top = histogram_top + raster_dock_metrics::kColorBalanceRangesTopOffset;
        const int range_h = raster_dock_metrics::kColorBalanceRangeHeight;
        const int range_w = (panel_width - gap * 2) / 3;
        const std::array<const wchar_t*, 3> range_labels = {
            is_vi ? L"Tối" : L"Shad",
            is_vi ? L"Trung" : L"Mid",
            is_vi ? L"Sáng" : L"High"};
        for (int r = 0; r < 3; ++r) {
            const int left = panel_left + r * (range_w + gap);
            const int right = (r == 2) ? (panel_left + panel_width) : (left + range_w);
            RECT range_rc = {left, range_top, right, range_top + range_h};
            const bool active = static_cast<int>(raster_color_balance_active_range_) == r;
            HBRUSH fill = CreateSolidBrush(active ? col_active_tab : col_input_bg);
            HPEN pen = CreatePen(PS_SOLID, 1, active ? col_teal : col_border);
            HGDIOBJ ob = SelectObject(mem_dc, fill);
            HGDIOBJ op = SelectObject(mem_dc, pen);
            RoundRect(mem_dc, range_rc.left, range_rc.top, range_rc.right, range_rc.bottom, 4, 4);
            SelectObject(mem_dc, ob);
            SelectObject(mem_dc, op);
            DeleteObject(fill);
            DeleteObject(pen);
            SetTextColor(mem_dc, active ? col_teal : col_text_muted);
            DrawTextW(mem_dc, range_labels[static_cast<size_t>(r)], -1, &range_rc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        const int fields_top = histogram_top + raster_dock_metrics::kColorBalanceFieldsTopOffset;
        const int row_h = raster_dock_metrics::kColorBalanceFieldHeight;
        const int row_step = raster_dock_metrics::kColorBalanceFieldRowSpacing;
        const std::array<const wchar_t*, 3> axis_labels = {
            is_vi ? L"Cyan ↔ Đỏ" : L"Cyan ↔ Red",
            is_vi ? L"Mag ↔ Lục" : L"Magenta ↔ Green",
            is_vi ? L"Vàng ↔ Lam" : L"Yellow ↔ Blue"};
        const std::array<double, 3> axis_values = {
            balance.red_cyan_percent,
            balance.green_magenta_percent,
            balance.blue_yellow_percent};
        const std::array<NumericFieldId, 3> axis_fields = {
            NumericFieldId::RasterColorBalanceRedCyan,
            NumericFieldId::RasterColorBalanceGreenMagenta,
            NumericFieldId::RasterColorBalanceBlueYellow};
        for (int axis = 0; axis < 3; ++axis) {
            const int row_y = fields_top + axis * row_step;
            SelectObject(mem_dc, font_secondary_);
            SetTextColor(mem_dc, col_text_muted);
            RECT label_rc = {panel_left, row_y, panel_left + 128, row_y + row_h};
            DrawTextW(mem_dc, axis_labels[static_cast<size_t>(axis)], -1,
                      &label_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            std::ostringstream text;
            text << std::fixed << std::setprecision(0) << axis_values[static_cast<size_t>(axis)] << "%";
            draw_dock_num_box({panel_left + panel_width - 96, row_y,
                               panel_left + panel_width, row_y + row_h},
                              text.str(), axis_fields[static_cast<size_t>(axis)]);
        }

        const int preserve_top = histogram_top + raster_dock_metrics::kColorBalancePreserveTopOffset;
        RECT preserve_rc = {panel_left, preserve_top, panel_left + panel_width,
                            preserve_top + raster_dock_metrics::kColorBalancePreserveHeight};
        HBRUSH fill = CreateSolidBrush(balance.preserve_luminosity ? col_active_tab : col_input_bg);
        HPEN pen = CreatePen(PS_SOLID, 1, balance.preserve_luminosity ? col_teal : col_border);
        HGDIOBJ ob = SelectObject(mem_dc, fill);
        HGDIOBJ op = SelectObject(mem_dc, pen);
        RoundRect(mem_dc, preserve_rc.left, preserve_rc.top, preserve_rc.right, preserve_rc.bottom, 4, 4);
        SelectObject(mem_dc, ob);
        SelectObject(mem_dc, op);
        DeleteObject(fill);
        DeleteObject(pen);
        SetTextColor(mem_dc, balance.preserve_luminosity ? col_teal : col_text_white);
        DrawTextW(mem_dc, is_vi
            ? (balance.preserve_luminosity ? L"✓ Giữ độ sáng (Luminosity)" : L"Giữ độ sáng (Luminosity)")
            : (balance.preserve_luminosity ? L"✓ Preserve Luminosity" : L"Preserve Luminosity"),
            -1, &preserve_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // CorelDRAW Opponent-Color Reference Sliders
        const int studio_top = histogram_top + 292;
        draw_studio_card_frame(studio_top, studio_top + 124,
                               is_vi ? L"THANH KÉO ĐỐI KHÁNG MÀU (CMY ↔ RGB)" : L"OPPONENT COLOR REFERENCE SLIDERS");
        const std::array<std::pair<COLORREF, COLORREF>, 3> opp_cols = {{
            {RGB(6, 182, 212), RGB(239, 68, 68)},
            {RGB(236, 72, 153), RGB(16, 185, 129)},
            {RGB(234, 179, 8), RGB(59, 130, 246)},
        }};
        for (int axis = 0; axis < 3; ++axis) {
            const int ry = studio_top + 22 + axis * 32;
            const double val = axis_values[static_cast<size_t>(axis)];
            std::ostringstream vs;
            vs << std::fixed << std::setprecision(0) << val << "%";
            const auto pair_col = opp_cols[static_cast<size_t>(axis)];
            draw_reference_slider_row(
                ry, axis_labels[static_cast<size_t>(axis)], vs.str(),
                (val + 100.0) / 200.0, true,
                [pair_col](double t) { return lerp_colorref(pair_col.first, pair_col.second, t); });
        }
    } else if (raster_adjustment_inspector_tab_ == 5) {
        const auto curves = selected_bitmap_curves();
        const int ch_top = histogram_top + raster_dock_metrics::kLevelsChannelButtonsTopOffset;
        const int ch_h = raster_dock_metrics::kLevelsChannelButtonsHeight;
        const std::array<const wchar_t*, 4> ch_labels = {L"RGB", L"R", L"G", L"B"};
        const std::array<COLORREF, 4> ch_colors = {
            col_teal, RGB(239, 68, 68), RGB(34, 197, 94), RGB(59, 130, 246)};
        for (int ch = 0; ch < 4; ++ch) {
            const int left = panel_left + ch * (button_w + gap);
            RECT button_rc = {left, ch_top, left + button_w, ch_top + ch_h};
            const bool active = static_cast<int>(curves.channel) == ch;
            HBRUSH fill = CreateSolidBrush(active ? col_active_tab : col_input_bg);
            HPEN pen = CreatePen(PS_SOLID, 1, active ? ch_colors[static_cast<size_t>(ch)] : col_border);
            HGDIOBJ ob = SelectObject(mem_dc, fill);
            HGDIOBJ op = SelectObject(mem_dc, pen);
            RoundRect(mem_dc, button_rc.left, button_rc.top, button_rc.right, button_rc.bottom, 4, 4);
            SelectObject(mem_dc, ob);
            SelectObject(mem_dc, op);
            DeleteObject(fill);
            DeleteObject(pen);
            SetTextColor(mem_dc, active ? ch_colors[static_cast<size_t>(ch)] : col_text_muted);
            DrawTextW(mem_dc, ch_labels[static_cast<size_t>(ch)], -1, &button_rc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        const int fields_top = histogram_top + raster_dock_metrics::kLevelsFieldsTopOffset;
        const int row_h = raster_dock_metrics::kLevelsFieldHeight;
        const int row_step = raster_dock_metrics::kLevelsFieldRowSpacing;
        draw_compact_metric(0, fields_top, row_h, L"0%",
                            curves.output_points[0] * 100.0, NumericFieldId::RasterCurvePoint0);
        draw_compact_metric(1, fields_top, row_h, L"25%",
                            curves.output_points[1] * 100.0, NumericFieldId::RasterCurvePoint1);
        draw_compact_metric(0, fields_top + row_step, row_h, L"50%",
                            curves.output_points[2] * 100.0, NumericFieldId::RasterCurvePoint2);
        draw_compact_metric(1, fields_top + row_step, row_h, L"75%",
                            curves.output_points[3] * 100.0, NumericFieldId::RasterCurvePoint3);
        draw_compact_metric(0, fields_top + row_step * 2, row_h, L"100%",
                            curves.output_points[4] * 100.0, NumericFieldId::RasterCurvePoint4);

        // CorelDRAW / Illustrator Interactive 2D Tone Curve Graph
        const int studio_top = histogram_top + 264;
        draw_studio_card_frame(studio_top, studio_top + 182,
                               is_vi ? L"ĐỒ THỊ ĐƯỜNG CONG TONE (KÉO ĐIỂM NÚT)" : L"INTERACTIVE 2D TONE CURVE GRAPH");

        const int plot_l = panel_left + 22;
        const int plot_t = studio_top + 24;
        const int plot_w = panel_width - 34;
        const int plot_h = 132;
        const int plot_r = plot_l + plot_w;
        const int plot_b = plot_t + plot_h;

        HBRUSH plot_br = CreateSolidBrush(col_input_bg);
        HPEN plot_pen = CreatePen(PS_SOLID, 1, col_border);
        HGDIOBJ ob = SelectObject(mem_dc, plot_br);
        HGDIOBJ op = SelectObject(mem_dc, plot_pen);
        Rectangle(mem_dc, plot_l, plot_t, plot_r, plot_b);
        SelectObject(mem_dc, ob);
        SelectObject(mem_dc, op);
        DeleteObject(plot_br);
        DeleteObject(plot_pen);

        // Quarter grid + diagonal reference line
        HPEN grid_pen = CreatePen(PS_DOT, 1, col_border);
        op = SelectObject(mem_dc, grid_pen);
        for (int g = 1; g <= 3; ++g) {
            const int gx = plot_l + (plot_w * g) / 4;
            const int gy = plot_t + (plot_h * g) / 4;
            MoveToEx(mem_dc, gx, plot_t, nullptr); LineTo(mem_dc, gx, plot_b);
            MoveToEx(mem_dc, plot_l, gy, nullptr); LineTo(mem_dc, plot_r, gy);
        }
        MoveToEx(mem_dc, plot_l, plot_b, nullptr);
        LineTo(mem_dc, plot_r, plot_t);
        SelectObject(mem_dc, op);
        DeleteObject(grid_pen);

        // Bottom input reference strip
        COLORREF curve_col = ch_colors[static_cast<size_t>(curves.channel)];
        draw_color_reference_strip(
            mem_dc, {plot_l, plot_b + 4, plot_r, plot_b + 12},
            [curve_col](double t) { return lerp_colorref(RGB(10, 12, 18), curve_col, t); },
            col_border);

        // Tone curve polyline + 5 interactive handles
        HPEN c_pen = CreatePen(PS_SOLID, 2, curve_col);
        op = SelectObject(mem_dc, c_pen);
        std::array<POINT, 5> pts{};
        for (int i = 0; i < 5; ++i) {
            pts[static_cast<size_t>(i)].x = plot_l + (plot_w * i) / 4;
            pts[static_cast<size_t>(i)].y = plot_b - static_cast<int>(
                std::lround(std::clamp(curves.output_points[static_cast<size_t>(i)], 0.0, 1.0) * plot_h));
            if (i == 0) MoveToEx(mem_dc, pts[0].x, pts[0].y, nullptr);
            else LineTo(mem_dc, pts[static_cast<size_t>(i)].x, pts[static_cast<size_t>(i)].y);
        }
        SelectObject(mem_dc, op);
        DeleteObject(c_pen);

        for (int i = 0; i < 5; ++i) {
            draw_slider_thumb(mem_dc, pts[static_cast<size_t>(i)].x,
                              pts[static_cast<size_t>(i)].y - 4,
                              pts[static_cast<size_t>(i)].y + 4,
                              curve_col, is_dark);
        }
    } else if (raster_adjustment_inspector_tab_ == 6) {
        const int actions_top = histogram_top + raster_dock_metrics::kRasterAdvancedActionsTopOffset;
        const int action_h = raster_dock_metrics::kRasterAdvancedActionHeight;
        const int action_gap = raster_dock_metrics::kRasterAdvancedActionGap;
        const int action_w = (panel_width - col_gap) / 2;
        const auto has_node = [&](auto predicate) {
            return std::any_of(selected_image->edit_graph.nodes.begin(),
                               selected_image->edit_graph.nodes.end(), predicate);
        };
        const bool has_selective_hsl = has_node([](const auto& node) {
            return std::holds_alternative<document::RasterSelectiveHslParams>(node.parameters);
        });
        const bool has_channel_mixer = has_node([](const auto& node) {
            return std::holds_alternative<document::RasterChannelMixerParams>(node.parameters);
        });
        const bool has_clone_stamp = has_node([](const auto& node) {
            return std::holds_alternative<document::RasterCloneStampParams>(node.parameters);
        });
        const bool has_local_mask = has_node([](const auto& node) {
            return node.mask.has_value();
        });
        const std::array<const wchar_t*, 4> action_labels = {
            is_vi
                ? (has_selective_hsl ? L"✓ HSL dải màu" : L"HSL dải màu")
                : (has_selective_hsl ? L"✓ Selective HSL" : L"Selective HSL"),
            is_vi
                ? (has_channel_mixer ? L"✓ Trộn kênh RGB" : L"Trộn kênh RGB")
                : (has_channel_mixer ? L"✓ Channel Mixer" : L"Channel Mixer"),
            is_vi
                ? (has_clone_stamp ? L"✓ Clone Stamp" : L"Clone Stamp")
                : (has_clone_stamp ? L"✓ Clone Stamp" : L"Clone Stamp"),
            is_vi
                ? (has_local_mask ? L"✓ Mặt nạ vùng" : L"Mặt nạ vùng")
                : (has_local_mask ? L"✓ Local Mask" : L"Local Mask")};
        const std::array<bool, 4> action_active = {
            has_selective_hsl, has_channel_mixer, has_clone_stamp, has_local_mask};
        SelectObject(mem_dc, font_secondary_);
        for (int idx = 0; idx < 4; ++idx) {
            const int col = idx % 2;
            const int row = idx / 2;
            const int left = panel_left + col * (action_w + col_gap);
            const int top = actions_top + row * (action_h + action_gap);
            RECT action_rc = {left, top, left + action_w, top + action_h};
            HBRUSH fill = CreateSolidBrush(action_active[static_cast<size_t>(idx)] ? col_active_tab : col_input_bg);
            HPEN pen = CreatePen(PS_SOLID, 1, action_active[static_cast<size_t>(idx)] ? col_teal : col_border);
            HGDIOBJ ob = SelectObject(mem_dc, fill);
            HGDIOBJ op = SelectObject(mem_dc, pen);
            RoundRect(mem_dc, action_rc.left, action_rc.top, action_rc.right, action_rc.bottom, 4, 4);
            SelectObject(mem_dc, ob);
            SelectObject(mem_dc, op);
            DeleteObject(fill);
            DeleteObject(pen);
            SetTextColor(mem_dc, action_active[static_cast<size_t>(idx)] ? col_teal : col_text_white);
            DrawTextW(mem_dc, action_labels[static_cast<size_t>(idx)], -1,
                      &action_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
}
#endif

bool NativeWindow::handle_raster_dock_interactive_mouse_down(int x, int y) {
    if (active_dock_tab_ != 0 || show_style_inspector_ || show_align_panel_) return false;
    const auto snapshot = root().document().current_snapshot();
    const auto& selection = root().vector_controller().selection().selected_ids();
    if (selection.size() != 1 || !snapshot) return false;
    const auto* obj = snapshot->objects().lookup_ref(selection.front());
    if (!obj || !std::holds_alternative<document::ImageObject>(*obj)) return false;

    const int panel_left = width_ - raster_dock_metrics::kInspectorDockWidth +
                           raster_dock_metrics::kInspectorPanelInset;
    const int panel_width = raster_dock_metrics::kInspectorPanelWidth;
    const int histogram_top = raster_dock_metrics::kInspectorContentTop +
                              raster_dock_metrics::kHistogramTopOffset;
    if (x < panel_left || x > panel_left + panel_width) return false;

    const int dock_y = y + properties_dock_scroll_;

    // 1. Histogram header [Auto] / [Reset] buttons
    if (dock_y >= histogram_top && dock_y <= histogram_top + 18) {
        if (x >= panel_left + panel_width - 96 && x <= panel_left + panel_width - 50) {
            return auto_levels_selected_bitmap();
        }
        if (x >= panel_left + panel_width - 46 && x <= panel_left + panel_width) {
            return reset_selected_bitmap_tab_adjustments(raster_adjustment_inspector_tab_);
        }
    }

    // 2. Interactive Black / Gamma / White triangles on bottom of Histogram (histogram_top + 80..94)
    if (dock_y >= histogram_top + 80 && dock_y <= histogram_top + 94) {
        const auto lv = selected_bitmap_levels();
        const int plot_l = panel_left + 4;
        const int plot_w = std::max(1, panel_width - 8);
        const int bx = plot_l + static_cast<int>(std::lround(lv.black_in * plot_w));
        const int wx = plot_l + static_cast<int>(std::lround(lv.white_in * plot_w));
        const double gamma_norm = levels_gamma_position(lv.gamma);
        const int gx = bx + static_cast<int>(std::lround((wx - bx) * gamma_norm));
        const int db = std::abs(x - bx);
        const int dg = std::abs(x - gx);
        const int dw = std::abs(x - wx);
        if (db <= dg && db <= dw) raster_dock_drag_control_ = 30;
        else if (dw <= db && dw <= dg) raster_dock_drag_control_ = 32;
        else raster_dock_drag_control_ = 31;
#ifdef _WIN32
        if (hwnd_) SetCapture(hwnd_);
#endif
        return handle_raster_dock_interactive_mouse_move(x, y);
    }

    // 3. Tab 1 (Light): 6 reference sliders at studio_top = histogram_top + 244
    if (raster_adjustment_inspector_tab_ == 1) {
        const int studio_top = histogram_top + 244;
        for (int i = 0; i < 6; ++i) {
            const int ry = studio_top + 22 + i * 31;
            if (dock_y >= ry + 10 && dock_y <= ry + 30) {
                raster_dock_drag_control_ = 10 + i;
#ifdef _WIN32
                if (hwnd_) SetCapture(hwnd_);
#endif
                return handle_raster_dock_interactive_mouse_move(x, y);
            }
        }
    }

    // 4. Tab 2 (Color / HSL): 7-channel selector + 5 reference sliders
    if (raster_adjustment_inspector_tab_ == 2) {
        const int ch_bar_top = histogram_top + 212;
        if (dock_y >= ch_bar_top + 16 && dock_y <= ch_bar_top + 38) {
            const int cgap = 3;
            const int cw = (panel_width - cgap * 6) / 7;
            for (int c = 0; c < 7; ++c) {
                const int cl = panel_left + c * (cw + cgap);
                const int cr = (c == 6) ? (panel_left + panel_width) : (cl + cw);
                if (x >= cl && x <= cr) {
                    raster_hsl_active_channel_ = static_cast<uint8_t>(c);
#ifdef _WIN32
                    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
                    return true;
                }
            }
        }
        const int studio_top = histogram_top + 292;
        for (int i = 0; i < 5; ++i) {
            const int ry = studio_top + 22 + i * 31;
            if (dock_y >= ry + 10 && dock_y <= ry + 30) {
                raster_dock_drag_control_ = 20 + i;
#ifdef _WIN32
                if (hwnd_) SetCapture(hwnd_);
#endif
                return handle_raster_dock_interactive_mouse_move(x, y);
            }
        }
    }

    // 5. Tab 3 (Levels): Input ramp, Output ramp, Auto / Reset buttons
    if (raster_adjustment_inspector_tab_ == 3) {
        const int studio_top = histogram_top + 266;
        if (dock_y >= studio_top + 38 && dock_y <= studio_top + 68) {
            const auto lv = selected_bitmap_levels();
            const int bar_l = panel_left + 10;
            const int bar_w = std::max(1, panel_width - 20);
            const int bx = bar_l + static_cast<int>(std::lround(lv.black_in * bar_w));
            const int wx = bar_l + static_cast<int>(std::lround(lv.white_in * bar_w));
            const double gamma_norm = levels_gamma_position(lv.gamma);
            const int gx = bx + static_cast<int>(std::lround((wx - bx) * gamma_norm));
            const int db = std::abs(x - bx);
            const int dg = std::abs(x - gx);
            const int dw = std::abs(x - wx);
            if (db <= dg && db <= dw) raster_dock_drag_control_ = 30;
            else if (dw <= db && dw <= dg) raster_dock_drag_control_ = 32;
            else raster_dock_drag_control_ = 31;
#ifdef _WIN32
            if (hwnd_) SetCapture(hwnd_);
#endif
            return handle_raster_dock_interactive_mouse_move(x, y);
        }
        if (dock_y >= studio_top + 88 && dock_y <= studio_top + 118) {
            const auto lv = selected_bitmap_levels();
            const int bar_l = panel_left + 10;
            const int bar_w = std::max(1, panel_width - 20);
            const int bx = bar_l + static_cast<int>(std::lround(lv.black_out * bar_w));
            const int wx = bar_l + static_cast<int>(std::lround(lv.white_out * bar_w));
            raster_dock_drag_control_ = (std::abs(x - bx) <= std::abs(x - wx)) ? 33 : 34;
#ifdef _WIN32
            if (hwnd_) SetCapture(hwnd_);
#endif
            return handle_raster_dock_interactive_mouse_move(x, y);
        }
        if (dock_y >= studio_top + 128 && dock_y <= studio_top + 154) {
            if (x >= panel_left + 10 && x <= panel_left + panel_width / 2 - 4) {
                return auto_levels_selected_bitmap();
            }
            if (x >= panel_left + panel_width / 2 + 4 && x <= panel_left + panel_width - 10) {
                return reset_selected_bitmap_tab_adjustments(3);
            }
        }
    }

    // 6. Tab 4 (Color Balance): 3 opponent color sliders
    if (raster_adjustment_inspector_tab_ == 4) {
        const int studio_top = histogram_top + 292;
        for (int axis = 0; axis < 3; ++axis) {
            const int ry = studio_top + 22 + axis * 32;
            if (dock_y >= ry + 10 && dock_y <= ry + 30) {
                raster_dock_drag_control_ = 40 + axis;
#ifdef _WIN32
                if (hwnd_) SetCapture(hwnd_);
#endif
                return handle_raster_dock_interactive_mouse_move(x, y);
            }
        }
    }

    // 7. Tab 5 (Curves): Interactive 2D Tone Curve Graph
    if (raster_adjustment_inspector_tab_ == 5) {
        const int studio_top = histogram_top + 264;
        const int plot_l = panel_left + 22;
        const int plot_t = studio_top + 24;
        const int plot_w = panel_width - 34;
        const int plot_h = 132;
        if (x >= plot_l - 8 && x <= plot_l + plot_w + 8 &&
            dock_y >= plot_t - 8 && dock_y <= plot_t + plot_h + 8) {
            const double norm_x = std::clamp(static_cast<double>(x - plot_l) / std::max(1, plot_w), 0.0, 1.0);
            const int pt_idx = std::clamp(static_cast<int>(std::lround(norm_x * 4.0)), 0, 4);
            raster_dock_drag_control_ = 50 + pt_idx;
#ifdef _WIN32
            if (hwnd_) SetCapture(hwnd_);
#endif
            return handle_raster_dock_interactive_mouse_move(x, y);
        }
    }
    return false;
}

bool NativeWindow::handle_raster_dock_interactive_mouse_move(int x, int y) {
    if (raster_dock_drag_control_ == 0) return false;
    const int panel_left = width_ - raster_dock_metrics::kInspectorDockWidth +
                           raster_dock_metrics::kInspectorPanelInset;
    const int panel_width = raster_dock_metrics::kInspectorPanelWidth;
    const int histogram_top = raster_dock_metrics::kInspectorContentTop +
                              raster_dock_metrics::kHistogramTopOffset;

    const int bar_left = panel_left + 8;
    const int bar_width = std::max(1, panel_width - 16);
    const double t = std::clamp(static_cast<double>(x - bar_left) / bar_width, 0.0, 1.0);

    bool updated = false;
    if (raster_dock_drag_control_ >= 10 && raster_dock_drag_control_ <= 15) {
        const int idx = raster_dock_drag_control_ - 10;
        if (idx == 0) {
            updated = set_selected_bitmap_exposure_ev(std::round(((t * 10.0) - 5.0) * 20.0) / 20.0);
        } else {
            constexpr NumericFieldId kFields[5] = {
                NumericFieldId::RasterLightContrast,
                NumericFieldId::RasterLightHighlights,
                NumericFieldId::RasterLightShadows,
                NumericFieldId::RasterLightWhites,
                NumericFieldId::RasterLightBlacks};
            updated = set_selected_bitmap_light_value(kFields[idx - 1], std::round((t * 200.0) - 100.0));
        }
    } else if (raster_dock_drag_control_ >= 20 && raster_dock_drag_control_ <= 24) {
        const int idx = raster_dock_drag_control_ - 20;
        if (idx == 4) {
            updated = set_selected_bitmap_white_balance_value(
                NumericFieldId::RasterWhiteBalanceTemperature, std::round((t * 200.0) - 100.0));
        } else if (raster_hsl_active_channel_ == 0) {
            if (idx == 0) updated = set_selected_bitmap_color_adjustment(NumericFieldId::RasterHslHue, std::round((t * 360.0) - 180.0));
            else if (idx == 1) updated = set_selected_bitmap_color_adjustment(NumericFieldId::RasterHslSaturation, std::round((t * 200.0) - 100.0));
            else if (idx == 2) updated = set_selected_bitmap_color_adjustment(NumericFieldId::RasterHslLightness, std::round((t * 200.0) - 100.0));
            else if (idx == 3) updated = set_selected_bitmap_color_adjustment(NumericFieldId::RasterVibranceAmount, std::round((t * 200.0) - 100.0));
        } else {
            const auto range = static_cast<document::RasterHueRange>(raster_hsl_active_channel_ - 1);
            if (idx == 0) updated = set_selected_bitmap_selective_hsl_value(range, 0, std::round((t * 60.0) - 30.0));
            else if (idx == 1) updated = set_selected_bitmap_selective_hsl_value(range, 1, std::round((t * 200.0) - 100.0));
            else if (idx == 2) updated = set_selected_bitmap_selective_hsl_value(range, 2, std::round((t * 200.0) - 100.0));
            else if (idx == 3) updated = set_selected_bitmap_selective_hsl_value(range, 3, std::round(t * 100.0));
        }
    } else if (raster_dock_drag_control_ >= 30 && raster_dock_drag_control_ <= 34) {
        const auto lv = selected_bitmap_levels();
        if (raster_dock_drag_control_ == 30) {
            const double max_b = std::max(0.0, lv.white_in * 100.0 - 2.0);
            updated = set_selected_bitmap_levels_value(
                NumericFieldId::RasterLevelsBlackIn, std::clamp(std::round(t * 100.0), 0.0, max_b));
        } else if (raster_dock_drag_control_ == 32) {
            const double min_w = std::min(100.0, lv.black_in * 100.0 + 2.0);
            updated = set_selected_bitmap_levels_value(
                NumericFieldId::RasterLevelsWhiteIn, std::clamp(std::round(t * 100.0), min_w, 100.0));
        } else if (raster_dock_drag_control_ == 31) {
            const double span = std::max(0.05, lv.white_in - lv.black_in);
            const double rel = std::clamp((t - lv.black_in) / span, 0.1, 0.9);
            const double gamma = levels_gamma_from_position(rel);
            updated = set_selected_bitmap_levels_value(
                NumericFieldId::RasterLevelsGamma, std::round(gamma * 100.0) / 100.0);
        } else if (raster_dock_drag_control_ == 33) {
            const double max_b = std::max(0.0, lv.white_out * 100.0 - 2.0);
            updated = set_selected_bitmap_levels_value(
                NumericFieldId::RasterLevelsBlackOut, std::clamp(std::round(t * 100.0), 0.0, max_b));
        } else if (raster_dock_drag_control_ == 34) {
            const double min_w = std::min(100.0, lv.black_out * 100.0 + 2.0);
            updated = set_selected_bitmap_levels_value(
                NumericFieldId::RasterLevelsWhiteOut, std::clamp(std::round(t * 100.0), min_w, 100.0));
        }
    } else if (raster_dock_drag_control_ >= 40 && raster_dock_drag_control_ <= 42) {
        constexpr NumericFieldId kBalFields[3] = {
            NumericFieldId::RasterColorBalanceRedCyan,
            NumericFieldId::RasterColorBalanceGreenMagenta,
            NumericFieldId::RasterColorBalanceBlueYellow};
        updated = set_selected_bitmap_color_balance_value(
            kBalFields[raster_dock_drag_control_ - 40], std::round((t * 200.0) - 100.0));
    } else if (raster_dock_drag_control_ >= 50 && raster_dock_drag_control_ <= 54) {
        const int studio_top = histogram_top + 264;
        const int plot_t = studio_top + 24;
        const int plot_h = 132;
        const int dock_y = y + properties_dock_scroll_;
        const double out_pct = std::clamp((1.0 - static_cast<double>(dock_y - plot_t) / plot_h) * 100.0, 0.0, 100.0);
        constexpr NumericFieldId kCurveFields[5] = {
            NumericFieldId::RasterCurvePoint0,
            NumericFieldId::RasterCurvePoint1,
            NumericFieldId::RasterCurvePoint2,
            NumericFieldId::RasterCurvePoint3,
            NumericFieldId::RasterCurvePoint4};
        updated = set_selected_bitmap_curve_point(
            kCurveFields[raster_dock_drag_control_ - 50], std::round(out_pct));
    }

#ifdef _WIN32
    if (updated && hwnd_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
        UpdateWindow(hwnd_);
    }
#endif
    return true;
}

bool NativeWindow::handle_raster_dock_interactive_mouse_up(int /*x*/, int /*y*/) {
    if (raster_dock_drag_control_ == 0) return false;
    raster_dock_drag_control_ = 0;
#ifdef _WIN32
    if (hwnd_ && GetCapture() == hwnd_) {
        ReleaseCapture();
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    return true;
}

} // namespace pikdraw::desktop
