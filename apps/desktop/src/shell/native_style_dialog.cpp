#include "native_style_dialog.hpp"
#include "pikdraw/desktop/shell/native_style_color_model.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <vector>
#ifdef _WIN32
#include <windowsx.h>
#endif

namespace pikdraw::desktop {
#ifdef _WIN32
namespace {
using source_color::Model;
enum class ViewerMode : int { Hsb, Hsl, Rgb, Cmyk };
struct EditorState {
    color::ColorValue original, draft;
    double width{0.0};
    bool stroke{false}, vi{false}, mixed_color{false}, mixed_overprint{false};
    bool dark{false}, dragging_picker{false};
    ColorEditContext context{ColorEditContext::Generic};
    Model model{Model::Rgb};
    ViewerMode viewer{ViewerMode::Hsb};
    std::optional<color::DocumentColorMode> document_mode;
    bool populating{false}, color_changed{false}, width_changed{false};
    bool initial_overprint{false}, overprint_changed{false};
    std::optional<StyleEditorResult> result;
    COLORREF background{RGB(249, 250, 252)}, text{RGB(29, 37, 48)};
    COLORREF muted{RGB(100, 112, 128)}, border{RGB(218, 224, 232)};
    COLORREF edit_background{RGB(255, 255, 255)}, accent{RGB(38, 125, 230)};
    HBRUSH background_brush{nullptr}, edit_brush{nullptr};
};

HWND control(HWND dialog, const wchar_t *type, const wchar_t *text, DWORD style,
             int x, int y, int width, int height, int id) {
    const auto child = CreateWindowExW(std::wcscmp(type, L"EDIT") == 0 ? WS_EX_CLIENTEDGE : 0,
        type, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height, dialog,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    if (std::wcscmp(type, L"EDIT") == 0 && width > 12 && height > 12) {
        const auto region = CreateRoundRectRgn(0, 0, width + 1, height + 1, 8, 8);
        if (!SetWindowRgn(child, region, TRUE))
            DeleteObject(region);
    }
    return child;
}

struct Hsv { double h{0}, s{0}, v{0}; };
struct Hsl { double h{0}, s{0}, l{0}; };

color::RGBColor hsv_to_rgb(double hue, double saturation, double value, double alpha = 1) {
    hue = std::fmod(std::fmod(hue, 360.0) + 360.0, 360.0);
    saturation = std::clamp(saturation, 0.0, 1.0);
    value = std::clamp(value, 0.0, 1.0);
    const double chroma = value * saturation;
    const double sector = hue / 60.0;
    const double x = chroma * (1.0 - std::abs(std::fmod(sector, 2.0) - 1.0));
    double r = 0, g = 0, b = 0;
    if (sector < 1) { r = chroma; g = x; }
    else if (sector < 2) { r = x; g = chroma; }
    else if (sector < 3) { g = chroma; b = x; }
    else if (sector < 4) { g = x; b = chroma; }
    else if (sector < 5) { r = x; b = chroma; }
    else { r = chroma; b = x; }
    const double m = value - chroma;
    return {r + m, g + m, b + m, alpha};
}

color::RGBColor hsl_to_rgb(double hue, double saturation, double lightness, double alpha = 1) {
    hue = std::fmod(std::fmod(hue, 360.0) + 360.0, 360.0);
    saturation = std::clamp(saturation, 0.0, 1.0);
    lightness = std::clamp(lightness, 0.0, 1.0);
    const double chroma = (1.0 - std::abs(2.0 * lightness - 1.0)) * saturation;
    const double sector = hue / 60.0;
    const double x = chroma * (1.0 - std::abs(std::fmod(sector, 2.0) - 1.0));
    double r = 0, g = 0, b = 0;
    if (sector < 1) { r = chroma; g = x; }
    else if (sector < 2) { r = x; g = chroma; }
    else if (sector < 3) { g = chroma; b = x; }
    else if (sector < 4) { g = x; b = chroma; }
    else if (sector < 5) { r = x; b = chroma; }
    else { r = chroma; b = x; }
    const double m = lightness - chroma / 2.0;
    return {r + m, g + m, b + m, alpha};
}

color::RGBColor rgb_from(const color::ColorValue &value) {
    const auto preview = color::to_preview_rgba(value);
    return {preview.r, preview.g, preview.b, preview.a};
}

Hsv to_hsv(color::RGBColor rgb) {
    const double maximum = std::max({rgb.r, rgb.g, rgb.b});
    const double minimum = std::min({rgb.r, rgb.g, rgb.b});
    const double delta = maximum - minimum;
    double hue = 0;
    if (delta > 1e-12) {
        if (maximum == rgb.r) hue = 60.0 * std::fmod((rgb.g - rgb.b) / delta, 6.0);
        else if (maximum == rgb.g) hue = 60.0 * ((rgb.b - rgb.r) / delta + 2.0);
        else hue = 60.0 * ((rgb.r - rgb.g) / delta + 4.0);
    }
    return {std::fmod(hue + 360.0, 360.0), maximum > 0 ? delta / maximum : 0, maximum};
}

Hsl to_hsl(color::RGBColor rgb) {
    const double maximum = std::max({rgb.r, rgb.g, rgb.b});
    const double minimum = std::min({rgb.r, rgb.g, rgb.b});
    const double delta = maximum - minimum;
    const double lightness = (maximum + minimum) / 2.0;
    double hue = 0, saturation = 0;
    if (delta > 1e-12) {
        if (maximum == rgb.r) hue = 60.0 * std::fmod((rgb.g - rgb.b) / delta, 6.0);
        else if (maximum == rgb.g) hue = 60.0 * ((rgb.b - rgb.r) / delta + 2.0);
        else hue = 60.0 * ((rgb.r - rgb.g) / delta + 4.0);
        saturation = delta / (1.0 - std::abs(2.0 * lightness - 1.0));
    }
    return {std::fmod(hue + 360.0, 360.0), saturation, lightness};
}

bool is_hsl(ViewerMode mode) { return mode == ViewerMode::Hsl; }
bool uses_picker(Model model) { return model == Model::Rgb || model == Model::Cmyk; }
std::optional<double> read_number(HWND dialog, int id, double maximum);

ViewerMode viewer_from_index(int index) {
    return static_cast<ViewerMode>(std::clamp(index, 0, 3));
}

void select_viewer(HWND combo, ViewerMode mode) {
    for (int i = 0; i < SendMessageW(combo, CB_GETCOUNT, 0, 0); ++i)
        if (SendMessageW(combo, CB_GETITEMDATA, i, 0) == static_cast<LRESULT>(mode))
            SendMessageW(combo, CB_SETCURSEL, i, 0);
}

std::optional<color::ColorValue> process_color_from_view(HWND dialog, const EditorState &state) {
    const auto alpha = read_number(dialog, 207, 100);
    if (!alpha) return std::nullopt;
    color::ColorValue displayed;
    if (state.viewer == ViewerMode::Rgb) {
        const auto r = read_number(dialog, 201, 255);
        const auto g = read_number(dialog, 202, 255);
        const auto b = read_number(dialog, 203, 255);
        if (!r || !g || !b) return std::nullopt;
        displayed = color::ColorValue::make_rgb(*r / 255, *g / 255, *b / 255, *alpha / 100);
    } else if (state.viewer == ViewerMode::Cmyk) {
        std::array<double, 4> channel{};
        for (int i = 0; i < 4; ++i) {
            const auto value = read_number(dialog, 201 + i, 100);
            if (!value) return std::nullopt;
            channel[static_cast<size_t>(i)] = *value / 100;
        }
        displayed = color::ColorValue::make_cmyk(channel[0], channel[1], channel[2], channel[3], *alpha / 100);
    } else {
        const auto hue = read_number(dialog, 201, 360);
        const auto saturation = read_number(dialog, 202, 100);
        const auto third = read_number(dialog, 203, 100);
        if (!hue || !saturation || !third) return std::nullopt;
        const auto rgb = is_hsl(state.viewer)
            ? hsl_to_rgb(*hue, *saturation / 100, *third / 100, *alpha / 100)
            : hsv_to_rgb(*hue, *saturation / 100, *third / 100, *alpha / 100);
        displayed = color::ColorValue::make_rgb(rgb.r, rgb.g, rgb.b, rgb.a);
    }
    return source_color::convert(displayed, state.model);
}

COLORREF colorref_from(const color::ColorValue &value) {
    const auto rgb = rgb_from(value);
    return RGB(static_cast<BYTE>(std::clamp(std::lround(rgb.r * 255), 0L, 255L)),
               static_cast<BYTE>(std::clamp(std::lround(rgb.g * 255), 0L, 255L)),
               static_cast<BYTE>(std::clamp(std::lround(rgb.b * 255), 0L, 255L)));
}

std::wstring number(double value) {
    std::wostringstream text;
    text << std::setprecision(10) << value;
    return text.str();
}

std::optional<double> read_number(HWND dialog, int id, double maximum) {
    wchar_t buffer[128]{};
    GetDlgItemTextW(dialog, id, buffer, 128);
    std::wstring text(buffer);
    std::replace(text.begin(), text.end(), L',', L'.');
    wchar_t *end = nullptr;
    const double result = std::wcstod(text.c_str(), &end);
    while (end && std::iswspace(*end)) ++end;
    if (end == text.c_str() || !end || *end || !std::isfinite(result) || result < 0 || result > maximum)
        return std::nullopt;
    return result;
}

std::optional<std::string> read_spot_name(HWND dialog) {
    const int length = GetWindowTextLengthW(GetDlgItem(dialog, 201));
    if (length < 1 || length > 128) return std::nullopt;
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetDlgItemTextW(dialog, 201, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    while (!text.empty() && std::iswspace(text.front())) text.erase(text.begin());
    while (!text.empty() && std::iswspace(text.back())) text.pop_back();
    if (text.empty()) return std::nullopt;
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (bytes < 1 || bytes > 512) return std::nullopt;
    std::string result(static_cast<size_t>(bytes), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), result.data(), bytes, nullptr, nullptr)) return std::nullopt;
    return result;
}

bool has_overprint(ColorEditContext context) {
    return context == ColorEditContext::Fill || context == ColorEditContext::Stroke;
}
bool stroke_overprint(ColorEditContext context) { return context == ColorEditContext::Stroke; }
int model_index(Model model) { return static_cast<int>(model); }
Model model_from_index(int index) { return static_cast<Model>(std::clamp(index, 0, 3)); }
void select_model(HWND combo, Model model) {
    for (int i = 0; i < SendMessageW(combo, CB_GETCOUNT, 0, 0); ++i)
        if (SendMessageW(combo, CB_GETITEMDATA, i, 0) == model_index(model))
            SendMessageW(combo, CB_SETCURSEL, i, 0);
}

std::optional<color::ColorValue> read_color(HWND dialog, const EditorState &state) {
    color::ColorValue result;
    if (uses_picker(state.model)) {
        const auto viewed = process_color_from_view(dialog, state);
        if (!viewed) return std::nullopt;
        result = *viewed;
    } else if (state.model == Model::Spot) {
        const auto name = read_spot_name(dialog);
        const auto tint = read_number(dialog, 202, 100);
        const auto r = read_number(dialog, 203, 255), g = read_number(dialog, 204, 255);
        const auto b = read_number(dialog, 205, 255);
        const auto *spot = std::get_if<color::SpotColor>(&state.draft.payload());
        if (!name || !tint || !r || !g || !b || !spot) return std::nullopt;
        result = color::ColorValue{color::SpotColor{*name, *tint / 100,
            {*r / 255, *g / 255, *b / 255, spot->alternate_rgb.a}, spot->overprint}};
    } else {
        const auto alpha = read_number(dialog, 207, 100);
        const auto gray = read_number(dialog, 201, 100);
        if (!gray || !alpha) return std::nullopt;
        result = color::ColorValue{color::GrayscaleColor{*gray / 100, *alpha / 100}};
    }
    result.set_overprint(state.draft.overprint());
    if (has_overprint(state.context)) {
        const auto check = SendDlgItemMessageW(dialog, 208, BM_GETCHECK, 0, 0);
        if (check != BST_INDETERMINATE)
            result = source_color::with_overprint(std::move(result), stroke_overprint(state.context),
                                                   check == BST_CHECKED);
    }
    return result;
}

void set_overprint_caption(HWND dialog, const EditorState &state) {
    if (!has_overprint(state.context)) return;
    const auto check = SendDlgItemMessageW(dialog, 208, BM_GETCHECK, 0, 0);
    std::wstring caption = stroke_overprint(state.context)
        ? (state.vi ? L"Overprint viền" : L"Overprint stroke")
        : (state.vi ? L"Overprint tô" : L"Overprint fill");
    caption += check == BST_INDETERMINATE ? (state.vi ? L" · Nhiều giá trị" : L" · Mixed")
             : check == BST_CHECKED ? (state.vi ? L" · Bật" : L" · On")
                                    : (state.vi ? L" · Tắt" : L" · Off");
    SetDlgItemTextW(dialog, 208, caption.c_str());
}

void populate(HWND dialog, EditorState &state) {
    state.populating = true;
    std::array<std::wstring, 5> labels{}, values{};
    int rows = 0;
    std::wstring alpha_label = state.model == Model::Spot
        ? (state.vi ? L"Alpha RGB thay thế (%)" : L"Alternate RGB alpha (%)")
        : state.vi ? L"Độ đục (%)" : L"Opacity (%)";
    std::wstring alpha_value = L"100";
    if (uses_picker(state.model)) {
        const auto rgb = rgb_from(state.draft);
        alpha_value = number(rgb.a * 100);
        if (state.viewer == ViewerMode::Rgb) {
            labels = {L"R (0–255)", L"G (0–255)", L"B (0–255)", L"", L""};
            values = {number(rgb.r * 255), number(rgb.g * 255), number(rgb.b * 255), L"", L""};
            rows = 3;
        } else if (state.viewer == ViewerMode::Cmyk) {
            const auto cmyk_value = source_color::convert(state.draft, Model::Cmyk);
            const auto *cmyk = std::get_if<color::CMYKColor>(&cmyk_value.payload());
            if (cmyk) {
                labels = {L"C (%)", L"M (%)", L"Y (%)", L"K (%)", L""};
                values = {number(cmyk->c * 100), number(cmyk->m * 100), number(cmyk->y * 100),
                          number(cmyk->k * 100), L""};
                alpha_value = number(cmyk->a * 100);
                rows = 4;
            }
        } else if (is_hsl(state.viewer)) {
            const auto hsl = to_hsl(rgb);
            labels = {L"H (°)", L"S (%)", L"L (%)", L"", L""};
            values = {number(hsl.h), number(hsl.s * 100), number(hsl.l * 100), L"", L""};
            rows = 3;
        } else {
            const auto hsv = to_hsv(rgb);
            labels = {L"H (°)", L"S (%)", L"B (%)", L"", L""};
            values = {number(hsv.h), number(hsv.s * 100), number(hsv.v * 100), L"", L""};
            rows = 3;
        }
    } else if (const auto *spot = std::get_if<color::SpotColor>(&state.draft.payload());
               state.model == Model::Spot && spot) {
        wchar_t name[260]{};
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, spot->spot_name.data(),
            static_cast<int>(spot->spot_name.size()), name, 260);
        labels = state.vi
            ? std::array<std::wstring, 5>{L"Tên Spot", L"Tint (%)", L"R thay thế", L"G thay thế", L"B thay thế"}
            : std::array<std::wstring, 5>{L"Spot name", L"Tint (%)", L"Alternate R", L"Alternate G", L"Alternate B"};
        values = {name, number(spot->tint * 100), number(spot->alternate_rgb.r * 255),
                  number(spot->alternate_rgb.g * 255), number(spot->alternate_rgb.b * 255)};
        alpha_value = number(spot->alternate_rgb.a * 100); rows = 5;
    } else if (const auto *gray = std::get_if<color::GrayscaleColor>(&state.draft.payload());
               state.model == Model::Grayscale && gray) {
        labels = {state.vi ? L"Xám (%)" : L"Gray (%)", L"", L"", L"", L""};
        values = {number(gray->gray * 100), L"", L"", L"", L""};
        alpha_value = number(gray->a * 100); rows = 1;
    }
    for (int i = 0; i < 5; ++i) {
        SetDlgItemTextW(dialog, 101 + i, labels[static_cast<size_t>(i)].c_str());
        SetDlgItemTextW(dialog, 201 + i, values[static_cast<size_t>(i)].c_str());
        ShowWindow(GetDlgItem(dialog, 101 + i), i < rows ? SW_SHOW : SW_HIDE);
        ShowWindow(GetDlgItem(dialog, 201 + i), i < rows ? SW_SHOW : SW_HIDE);
    }
    SetDlgItemTextW(dialog, 106, alpha_label.c_str());
    SetDlgItemTextW(dialog, 207, alpha_value.c_str());
    const bool picker_enabled = uses_picker(state.model);
    EnableWindow(GetDlgItem(dialog, 110), picker_enabled);
    InvalidateRect(dialog, nullptr, FALSE);
    state.populating = false;
}

constexpr RECT picker_plane{22, 70, 322, 292};
constexpr RECT picker_hue{22, 306, 322, 324};

DWORD dib_pixel(color::RGBColor rgb) {
    const auto channel = [](double value) {
        return static_cast<DWORD>(std::clamp(std::lround(value * 255), 0L, 255L));
    };
    return (channel(rgb.r) << 16) | (channel(rgb.g) << 8) | channel(rgb.b);
}

void draw_dib(HDC dc, const RECT &destination, int width, int height,
              const std::vector<DWORD> &pixels) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, destination.left, destination.top,
        destination.right - destination.left, destination.bottom - destination.top,
        0, 0, width, height, pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
}

void draw_picker(HWND dialog, EditorState &state, HDC dc) {
    const auto current = read_color(dialog, state).value_or(state.draft);
    const auto rgb = rgb_from(current);
    const auto hsv = to_hsv(rgb);
    const double hue = hsv.h;
    constexpr int plane_width = 300, plane_height = 222;
    std::vector<DWORD> plane(static_cast<size_t>(plane_width * plane_height));
    for (int y = 0; y < plane_height; ++y) {
        const double vertical = 1.0 - static_cast<double>(y) / (plane_height - 1);
        for (int x = 0; x < plane_width; ++x) {
            const double saturation = static_cast<double>(x) / (plane_width - 1);
            const auto color = is_hsl(state.viewer)
                ? hsl_to_rgb(hue, saturation, vertical)
                : hsv_to_rgb(hue, saturation, vertical);
            plane[static_cast<size_t>(y * plane_width + x)] = dib_pixel(color);
        }
    }
    draw_dib(dc, picker_plane, plane_width, plane_height, plane);
    const double vertical = is_hsl(state.viewer) ? to_hsl(rgb).l : hsv.v;
    const double marker_saturation = is_hsl(state.viewer) ? to_hsl(rgb).s : hsv.s;
    const int marker_x = picker_plane.left + static_cast<int>(std::lround(marker_saturation * (picker_plane.right - picker_plane.left - 1)));
    const int marker_y = picker_plane.top + static_cast<int>(std::lround((1.0 - vertical) * (picker_plane.bottom - picker_plane.top - 1)));
    HPEN white = CreatePen(PS_SOLID, 3, RGB(255, 255, 255));
    HPEN dark = CreatePen(PS_SOLID, 1, RGB(20, 24, 30));
    const auto previous_pen = SelectObject(dc, white);
    HBRUSH previous_brush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(HOLLOW_BRUSH)));
    Ellipse(dc, marker_x - 6, marker_y - 6, marker_x + 7, marker_y + 7);
    SelectObject(dc, dark);
    Ellipse(dc, marker_x - 5, marker_y - 5, marker_x + 6, marker_y + 6);
    SelectObject(dc, previous_brush);
    SelectObject(dc, previous_pen);
    DeleteObject(white);
    DeleteObject(dark);

    constexpr int hue_width = 300;
    std::vector<DWORD> hues(static_cast<size_t>(hue_width));
    for (int x = 0; x < hue_width; ++x)
        hues[static_cast<size_t>(x)] = dib_pixel(hsv_to_rgb(360.0 * x / (hue_width - 1), 1, 1));
    draw_dib(dc, picker_hue, hue_width, 1, hues);
    const int hue_x = picker_hue.left + static_cast<int>(std::lround(hue / 360.0 * (picker_hue.right - picker_hue.left - 1)));
    HPEN hue_pen = CreatePen(PS_SOLID, 2, RGB(26, 30, 36));
    const auto old_pen = SelectObject(dc, hue_pen);
    MoveToEx(dc, hue_x, picker_hue.top - 3, nullptr);
    LineTo(dc, hue_x, picker_hue.bottom + 3);
    SelectObject(dc, old_pen);
    DeleteObject(hue_pen);

    if (!uses_picker(state.model)) {
        RECT disabled = picker_plane;
        HBRUSH overlay = CreateSolidBrush(state.background);
        SetBkMode(dc, TRANSPARENT);
        FillRect(dc, &disabled, overlay);
        DeleteObject(overlay);
        SetTextColor(dc, state.muted);
        DrawTextW(dc, state.vi ? L"Bộ chọn màu khả dụng cho RGB / CMYK"
                              : L"Color picker is available for RGB / CMYK",
                  -1, &disabled, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        FillRect(dc, const_cast<RECT *>(&picker_hue), state.background_brush);
    }
}

void update_picker(HWND dialog, EditorState &state, int x, int y) {
    if (!uses_picker(state.model)) return;
    auto base = read_color(dialog, state).value_or(state.draft);
    const auto current = to_hsv(rgb_from(base));
    const auto base_rgb = rgb_from(base);
    const auto base_hsl = to_hsl(base_rgb);
    double hue = current.h, saturation = is_hsl(state.viewer) ? base_hsl.s : current.s;
    double vertical = is_hsl(state.viewer) ? base_hsl.l : current.v;
    if (y >= picker_hue.top - 3 && y <= picker_hue.bottom + 3) {
        const double fraction = std::clamp(static_cast<double>(x - picker_hue.left) /
            (picker_hue.right - picker_hue.left - 1), 0.0, 1.0);
        hue = fraction * 360.0;
    } else {
        saturation = std::clamp(static_cast<double>(x - picker_plane.left) /
            (picker_plane.right - picker_plane.left - 1), 0.0, 1.0);
        vertical = 1.0 - std::clamp(static_cast<double>(y - picker_plane.top) /
            (picker_plane.bottom - picker_plane.top - 1), 0.0, 1.0);
    }
    const auto selected = is_hsl(state.viewer)
        ? hsl_to_rgb(hue, saturation, vertical, base_rgb.a)
        : hsv_to_rgb(hue, saturation, vertical, base_rgb.a);
    auto next = color::ColorValue::make_rgb(selected.r, selected.g, selected.b, selected.a);
    next.set_overprint(base.overprint());
    state.draft = source_color::convert(next, state.model);
    state.color_changed = true;
    populate(dialog, state);
}

void draw_swatch(HWND dialog, const DRAWITEMSTRUCT &item, const EditorState &state) {
    const auto value = item.CtlID == 120 ? state.original
        : read_color(dialog, state).value_or(state.draft);
    RECT rect = item.rcItem;
    const auto rgb = colorref_from(value);
    HBRUSH brush = CreateSolidBrush(rgb);
    HPEN pen = CreatePen(PS_SOLID, 1, state.border);
    const auto old_brush = SelectObject(item.hDC, brush);
    const auto old_pen = SelectObject(item.hDC, pen);
    RoundRect(item.hDC, rect.left, rect.top, rect.right, rect.bottom, 8, 8);
    SelectObject(item.hDC, old_brush);
    SelectObject(item.hDC, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void initialize_editor(HWND dialog, EditorState &state) {
    state.populating = true;
    const wchar_t *title = state.context == ColorEditContext::Stroke
        ? (state.vi ? L"Màu và độ dày viền" : L"Source stroke color and width")
        : state.context == ColorEditContext::Fill
            ? (state.vi ? L"Màu tô nguồn" : L"Source fill color")
            : (state.vi ? L"Màu nguồn" : L"Source color");
    SetWindowTextW(dialog, title);
    constexpr int dialog_width = 760, dialog_height = 610;
    RECT work{};
    if (GetParent(dialog) && GetWindowRect(GetParent(dialog), &work)) {
        const int x = (work.left + work.right - dialog_width) / 2;
        const int y = (work.top + work.bottom - dialog_height) / 2;
        SetWindowPos(dialog, nullptr, x, y, dialog_width, dialog_height, SWP_NOZORDER);
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        SetWindowPos(dialog, nullptr, (work.left + work.right - dialog_width) / 2,
                     (work.top + work.bottom - dialog_height) / 2, dialog_width, dialog_height, SWP_NOZORDER);
    }
    const auto window_region = CreateRoundRectRgn(0, 0, dialog_width + 1, dialog_height + 1, 20, 20);
    if (!SetWindowRgn(dialog, window_region, TRUE)) DeleteObject(window_region);

    state.background = state.dark ? RGB(25, 30, 38) : RGB(249, 250, 252);
    state.text = state.dark ? RGB(232, 237, 245) : RGB(29, 37, 48);
    state.muted = state.dark ? RGB(157, 169, 185) : RGB(100, 112, 128);
    state.border = state.dark ? RGB(65, 76, 91) : RGB(218, 224, 232);
    state.edit_background = state.dark ? RGB(35, 42, 53) : RGB(255, 255, 255);
    state.accent = state.dark ? RGB(0, 190, 160) : RGB(38, 125, 230);
    state.background_brush = CreateSolidBrush(state.background);
    state.edit_brush = CreateSolidBrush(state.edit_background);

    control(dialog, L"STATIC", state.vi ? L"Hệ màu nguồn" : L"Source color model", 0, 22, 18, 104, 22, 0);
    const auto combo = control(dialog, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                               130, 14, 224, 180, 100);
    const wchar_t *models_en[] = {L"RGB · 0–255", L"CMYK · 0–100%", L"Spot · name + tint", L"Grayscale · 0–100%"};
    const wchar_t *models_vi[] = {L"RGB · 0–255", L"CMYK · 0–100%", L"Spot · tên + tint", L"Thang xám · 0–100%"};
    const auto *models = state.vi ? models_vi : models_en;
    for (int m = 0; m < 4; ++m) {
        if (state.document_mode && m == (state.document_mode == color::DocumentColorMode::Cmyk ? 0 : 1))
            continue;
        const auto index = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(models[m]));
        SendMessageW(combo, CB_SETITEMDATA, index, m);
    }
    state.model = source_color::model_of(state.draft);
    select_model(combo, state.model);
    control(dialog, L"STATIC", state.vi ? L"Chế độ xem" : L"Color viewer", 0, 386, 18, 88, 22, 0);
    const auto viewer = control(dialog, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                                480, 14, 250, 180, 110);
    const wchar_t *viewers_en[] = {L"HSB · Hue based", L"HSL · Lightness", L"RGB · 0–255", L"CMYK · 0–100%"};
    const wchar_t *viewers_vi[] = {L"HSB · Theo sắc độ", L"HSL · Độ sáng", L"RGB · 0–255", L"CMYK · 0–100%"};
    const auto *viewers = state.vi ? viewers_vi : viewers_en;
    for (int i = 0; i < 4; ++i) {
        const auto item = SendMessageW(viewer, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(viewers[i]));
        SendMessageW(viewer, CB_SETITEMDATA, item, i);
    }
    state.viewer = ViewerMode::Hsb;
    select_viewer(viewer, state.viewer);
    control(dialog, L"STATIC", state.vi ? L"Màu đồng nhất" : L"Uniform color", 0, 22, 46, 300, 20, 0);
    for (int i = 0; i < 5; ++i) {
        const int y = 72 + i * 40;
        control(dialog, L"STATIC", L"", 0, 354, y + 4, 96, 24, 101 + i);
        control(dialog, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, 460, y, 270, 30, 201 + i);
    }
    control(dialog, L"STATIC", L"", 0, 354, 282, 96, 24, 106);
    control(dialog, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, 460, 276, 270, 30, 207);
    control(dialog, L"STATIC", state.vi ? L"Bản gốc" : L"Original", 0, 22, 351, 68, 22, 0);
    control(dialog, L"STATIC", L"", SS_OWNERDRAW, 92, 345, 94, 38, 120);
    control(dialog, L"STATIC", state.vi ? L"Bản mới" : L"New color", 0, 204, 351, 74, 22, 0);
    control(dialog, L"STATIC", L"", SS_OWNERDRAW, 280, 345, 94, 38, 121);
    if (has_overprint(state.context)) {
        const auto check = control(dialog, L"BUTTON", L"", BS_3STATE | WS_TABSTOP, 354, 334, 376, 28, 208);
        state.initial_overprint = stroke_overprint(state.context)
            ? state.original.overprint().stroke : state.original.overprint().fill;
        SendMessageW(check, BM_SETCHECK, state.mixed_overprint ? BST_INDETERMINATE
            : state.initial_overprint ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    set_overprint_caption(dialog, state);
    control(dialog, L"STATIC", state.vi ? L"Viền nguồn (mm)" : L"Source width (mm)",
            0, 354, 376, 96, 24, 209);
    const auto width = control(dialog, L"EDIT", number(state.width).c_str(), ES_AUTOHSCROLL | WS_TABSTOP,
                               460, 370, 270, 30, 206);
    EnableWindow(width, state.stroke);
    ShowWindow(GetDlgItem(dialog, 209), state.stroke ? SW_SHOW : SW_HIDE);
    ShowWindow(width, state.stroke ? SW_SHOW : SW_HIDE);
    const std::wstring hint = state.mixed_color
        ? (state.vi ? L"Màu đang Mixed. Đổi kênh thay màu; overprint chưa chỉnh vẫn giữ riêng từng đối tượng."
                    : L"Colors are Mixed. Channel edits replace source color; untouched overprint stays per object.")
        : state.document_mode ? (state.vi ? L"Màu process khóa theo tài liệu. Đổi hệ màu toàn tài liệu trong menu Edit. Spot và overprint được giữ."
                    : L"Process colors follow the document. Change document mode in Edit. Spot and overprint are retained.")
        : (state.vi ? L"Chuyển hệ màu gần đúng; overprint được giữ."
                    : L"Approximate model conversion; overprint is retained.");
    control(dialog, L"STATIC", hint.c_str(), 0, 22, 422, 708, 58, 0);
    control(dialog, L"BUTTON", state.vi ? L"Áp dụng" : L"Apply", BS_DEFPUSHBUTTON | BS_OWNERDRAW | WS_TABSTOP,
            550, 532, 84, 32, IDOK);
    control(dialog, L"BUTTON", state.vi ? L"Hủy" : L"Cancel", BS_OWNERDRAW | WS_TABSTOP,
            646, 532, 84, 32, IDCANCEL);
    populate(dialog, state);
    state.populating = false;
}

INT_PTR CALLBACK editor_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    auto *state = reinterpret_cast<EditorState *>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<EditorState *>(lparam);
        SetWindowLongPtrW(dialog, DWLP_USER, lparam);
        initialize_editor(dialog, *state);
        return TRUE;
    }
    if (!state) return FALSE;
    if (message == WM_ERASEBKGND) {
        RECT client{};
        GetClientRect(dialog, &client);
        FillRect(reinterpret_cast<HDC>(wparam), &client, state->background_brush);
        return TRUE;
    }
    if (message == WM_CTLCOLORDLG) return reinterpret_cast<INT_PTR>(state->background_brush);
    if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN) {
        const auto dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(dc, state->text);
        SetBkMode(dc, TRANSPARENT);
        return reinterpret_cast<INT_PTR>(state->background_brush);
    }
    if (message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX) {
        const auto dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(dc, state->text);
        SetBkColor(dc, state->edit_background);
        return reinterpret_cast<INT_PTR>(state->edit_brush);
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        const auto dc = BeginPaint(dialog, &paint);
        draw_picker(dialog, *state, dc);
        EndPaint(dialog, &paint);
        return TRUE;
    }
    if (message == WM_DRAWITEM) {
        const auto *item = reinterpret_cast<const DRAWITEMSTRUCT *>(lparam);
        if (!item) return FALSE;
        if (item->CtlID == 120 || item->CtlID == 121) {
            draw_swatch(dialog, *item, *state);
            return TRUE;
        }
        if (item->CtlID == IDOK || item->CtlID == IDCANCEL) {
            const bool primary = item->CtlID == IDOK;
            RECT rect = item->rcItem;
            const COLORREF fill = primary ? state->accent
                : state->dark ? RGB(42, 50, 62) : RGB(235, 239, 244);
            HBRUSH brush = CreateSolidBrush(fill);
            HPEN pen = CreatePen(PS_SOLID, 1, primary ? state->accent : state->border);
            const auto old_brush = SelectObject(item->hDC, brush);
            const auto old_pen = SelectObject(item->hDC, pen);
            RoundRect(item->hDC, rect.left, rect.top, rect.right, rect.bottom, 9, 9);
            SelectObject(item->hDC, old_brush);
            SelectObject(item->hDC, old_pen);
            wchar_t caption[64]{};
            GetWindowTextW(item->hwndItem, caption, 64);
            SetBkMode(item->hDC, TRANSPARENT);
            SetTextColor(item->hDC, primary ? RGB(255, 255, 255) : state->text);
            DrawTextW(item->hDC, caption, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            DeleteObject(brush);
            DeleteObject(pen);
            return TRUE;
        }
    }
    if (message == WM_LBUTTONDOWN || message == WM_MOUSEMOVE || message == WM_LBUTTONUP) {
        const int x = GET_X_LPARAM(lparam), y = GET_Y_LPARAM(lparam);
        if (message == WM_LBUTTONDOWN && uses_picker(state->model) &&
            ((x >= picker_plane.left && x < picker_plane.right && y >= picker_plane.top && y < picker_plane.bottom) ||
             (x >= picker_hue.left && x < picker_hue.right && y >= picker_hue.top - 3 && y <= picker_hue.bottom + 3))) {
            state->dragging_picker = true;
            SetCapture(dialog);
            update_picker(dialog, *state, x, y);
            return TRUE;
        }
        if (message == WM_MOUSEMOVE && state->dragging_picker) {
            update_picker(dialog, *state, x, y);
            return TRUE;
        }
        if (message == WM_LBUTTONUP && state->dragging_picker) {
            update_picker(dialog, *state, x, y);
            state->dragging_picker = false;
            if (GetCapture() == dialog) ReleaseCapture();
            return TRUE;
        }
    }
    if (message == WM_CAPTURECHANGED) {
        state->dragging_picker = false;
        return FALSE;
    }
    if (message == WM_NCDESTROY) {
        if (state->background_brush) DeleteObject(state->background_brush);
        if (state->edit_brush) DeleteObject(state->edit_brush);
        SetWindowLongPtrW(dialog, DWLP_USER, 0);
        return FALSE;
    }
    if (message == WM_CLOSE) { EndDialog(dialog, IDCANCEL); return TRUE; }
    if (message != WM_COMMAND) return FALSE;
    const auto id = LOWORD(wparam);
    if (id == 100 && HIWORD(wparam) == CBN_SELCHANGE) {
        const auto current = read_color(dialog, *state);
        if (!current) {
            select_model(GetDlgItem(dialog, 100), state->model);
            MessageBoxW(dialog, state->vi ? L"Sửa giá trị chưa hợp lệ trước khi đổi hệ màu."
                                           : L"Correct the invalid value before changing the color model.",
                        L"PikDraw", MB_OK | MB_ICONWARNING);
            return TRUE;
        }
        const auto selected = SendDlgItemMessageW(dialog, 100, CB_GETCURSEL, 0, 0);
        const auto target = model_from_index(static_cast<int>(SendDlgItemMessageW(dialog, 100, CB_GETITEMDATA, selected, 0)));
        state->draft = source_color::convert(*current, target);
        state->model = source_color::model_of(state->draft);
        if (state->model == Model::Rgb) state->viewer = ViewerMode::Rgb;
        else if (state->model == Model::Cmyk) state->viewer = ViewerMode::Cmyk;
        select_viewer(GetDlgItem(dialog, 110), state->viewer);
        state->color_changed = true;
        populate(dialog, *state);
        return TRUE;
    }
    if (id == 110 && HIWORD(wparam) == CBN_SELCHANGE) {
        const auto current = read_color(dialog, *state);
        if (!current) {
            select_viewer(GetDlgItem(dialog, 110), state->viewer);
            MessageBoxW(dialog, state->vi ? L"Sửa giá trị chưa hợp lệ trước khi đổi chế độ xem."
                                           : L"Correct the invalid value before changing the color view.",
                        L"PikDraw", MB_OK | MB_ICONWARNING);
            return TRUE;
        }
        const auto selected = SendDlgItemMessageW(dialog, 110, CB_GETCURSEL, 0, 0);
        state->draft = *current;
        state->viewer = viewer_from_index(static_cast<int>(SendDlgItemMessageW(dialog, 110, CB_GETITEMDATA, selected, 0)));
        populate(dialog, *state);
        return TRUE;
    }
    if (id >= 201 && id <= 205 && HIWORD(wparam) == EN_CHANGE && !state->populating)
        state->color_changed = true;
    if (id == 207 && HIWORD(wparam) == EN_CHANGE && !state->populating)
        state->color_changed = true;
    if (id == 206 && HIWORD(wparam) == EN_CHANGE && !state->populating)
        state->width_changed = true;
    if (id == 208 && HIWORD(wparam) == BN_CLICKED) {
        const auto previous = SendDlgItemMessageW(dialog, 208, BM_GETCHECK, 0, 0);
        const auto next = previous == BST_INDETERMINATE ? BST_CHECKED
            : previous == BST_CHECKED ? BST_UNCHECKED : BST_CHECKED;
        SendDlgItemMessageW(dialog, 208, BM_SETCHECK, next, 0);
        state->overprint_changed = state->mixed_overprint ||
            (next == BST_CHECKED) != state->initial_overprint;
        set_overprint_caption(dialog, *state);
        return TRUE;
    }
    if (id == IDOK) {
        auto color = read_color(dialog, *state);
        const auto width = read_number(dialog, 206, 10000);
        if (!color || !width) {
            MessageBoxW(dialog,
                state->vi ? L"Giá trị không hợp lệ. H: 0–360; kênh: 0–100% hoặc 0–255; tên Spot: 1–128 ký tự; viền: 0–10000 mm."
                          : L"Invalid values. H: 0–360; channels: 0–100% or 0–255; Spot name: 1–128 characters; width: 0–10000 mm.",
                L"PikDraw", MB_OK | MB_ICONWARNING);
            return TRUE;
        }
        if (!state->color_changed) color = state->original;
        const auto overprint = state->overprint_changed
            ? std::optional<bool>{SendDlgItemMessageW(dialog, 208, BM_GETCHECK, 0, 0) == BST_CHECKED}
            : std::nullopt;
        state->result = StyleEditorResult{*color, *width, state->color_changed,
                                          state->width_changed, overprint};
        EndDialog(dialog, IDOK);
        return TRUE;
    }
    if (id == IDCANCEL) { EndDialog(dialog, IDCANCEL); return TRUE; }
    return FALSE;
}
} // namespace

std::optional<StyleEditorResult> edit_source_color(HWND owner, const color::ColorValue &initial,
    double width_mm, bool stroke, bool vietnamese, ColorEditContext context,
    bool mixed_color, bool mixed_overprint, std::optional<color::DocumentColorMode> document_mode,
    bool dark_theme) {
    struct EmptyDialog { DLGTEMPLATE dialog; WORD menu{0}, klass{0}, title{0}; } layout{};
    layout.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
    layout.dialog.cx = 280;
    layout.dialog.cy = 300;
    EditorState state;
    state.original = initial; state.draft = initial; state.width = width_mm;
    state.document_mode = document_mode;
    if (document_mode) state.draft = color::convert_color_to_mode(initial, *document_mode);
    state.stroke = stroke; state.vi = vietnamese; state.context = context;
    state.mixed_color = mixed_color; state.mixed_overprint = mixed_overprint;
    state.dark = dark_theme;
    DialogBoxIndirectParamW(GetModuleHandleW(nullptr), &layout.dialog, owner, editor_proc,
                            reinterpret_cast<LPARAM>(&state));
    return state.result;
}
#endif
} // namespace pikdraw::desktop
