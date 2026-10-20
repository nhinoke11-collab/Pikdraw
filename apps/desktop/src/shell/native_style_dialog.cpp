#include "native_style_dialog.hpp"
#include "native_style_dialog_math.hpp"
#include "pikdraw/desktop/shell/native_style_color_model.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>
#ifdef _WIN32
#include <windowsx.h>
#endif

namespace pikdraw::desktop {
#ifdef _WIN32
namespace {
using source_color::Model;
using namespace style_dialog_math;

enum class DragTarget : int { None, Plane, Hue };

constexpr int kClientWidth = 484;
constexpr int kClientHeight = 326;

constexpr RECT picker_plane{16, 60, 206, 250};
constexpr RECT picker_hue{16, 256, 206, 268};
constexpr int kOverprintId = 260;

struct EditorState {
    color::ColorValue original, draft;
    double width{0.0};
    bool stroke{false}, vi{false}, mixed_color{false}, mixed_overprint{false};
    bool dark{false};
    DragTarget drag_target{DragTarget::None};
    ColorEditContext context{ColorEditContext::Generic};
    Model model{Model::Rgb};
    ViewerMode viewer{ViewerMode::Hsb};
    std::optional<color::DocumentColorMode> document_mode;
    bool populating{false}, color_changed{false}, width_changed{false};
    bool initial_overprint{false}, overprint_changed{false};
    std::optional<StyleEditorResult> result;
    COLORREF background{RGB(249, 250, 252)}, text{RGB(29, 37, 48)};
    COLORREF muted{RGB(100, 112, 128)}, border{RGB(200, 208, 220)};
    COLORREF edit_background{RGB(255, 255, 255)}, accent{RGB(30, 112, 232)};
    HBRUSH background_brush{nullptr}, edit_brush{nullptr};
    HFONT font{nullptr}, font_bold{nullptr}, font_small{nullptr};

    double current_hue{0.0};
    double current_sat{1.0};
    double current_val{1.0};
    double current_alpha{1.0};

    std::vector<DWORD> cached_plane;
    double cached_hue{-1.0};
    ViewerMode cached_viewer{ViewerMode::Hsb};
    int cached_plane_w{0};
    int cached_plane_h{0};

    std::vector<DWORD> cached_hue_pixels;
    int cached_hue_w{0};

    HWND edit_rgb[3]{};
    HWND edit_hex{};
    HWND edit_hsb[3]{};
    HWND edit_cmyk[4]{};
    HWND edit_hsl[3]{};
    HWND edit_lab[3]{};
    HWND edit_opacity{};
    HWND edit_width{};
};

HWND control(HWND dialog, const wchar_t *type, const wchar_t *text, DWORD style,
             int x, int y, int width, int height, int id, HFONT font) {
    const bool is_edit = (std::wcscmp(type, L"EDIT") == 0);
    const auto child = CreateWindowExW(0, type, text,
        WS_CHILD | WS_VISIBLE | (is_edit ? WS_BORDER : 0) | style,
        x, y, width, height, dialog,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    if (font) {
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    return child;
}

bool has_overprint(ColorEditContext context) noexcept {
    return context == ColorEditContext::Fill || context == ColorEditContext::Stroke;
}
bool stroke_overprint(ColorEditContext context) noexcept { return context == ColorEditContext::Stroke; }

std::optional<double> read_number_from_hwnd(HWND edit, double maximum) {
    if (!edit) return std::nullopt;
    wchar_t buffer[128]{};
    GetWindowTextW(edit, buffer, 128);
    return parse_number(buffer, maximum);
}

void set_edit_text_if_changed(HWND edit, const std::wstring &text) {
    if (!edit) return;
    wchar_t current[64]{};
    GetWindowTextW(edit, current, 64);
    if (text != current) SetWindowTextW(edit, text.c_str());
}

COLORREF colorref_from(const color::ColorValue &value) {
    const auto rgb = rgb_from(value);
    return RGB(static_cast<BYTE>(std::clamp(std::lround(rgb.r * 255.0), 0L, 255L)),
               static_cast<BYTE>(std::clamp(std::lround(rgb.g * 255.0), 0L, 255L)),
               static_cast<BYTE>(std::clamp(std::lround(rgb.b * 255.0), 0L, 255L)));
}

void assign_viewer_ids(EditorState &state) {
    for (int i = 0; i < 3; ++i) SetWindowLongPtrW(state.edit_rgb[i], GWLP_ID, state.viewer == ViewerMode::Rgb ? 201 + i : 211 + i);
    SetWindowLongPtrW(state.edit_hex, GWLP_ID, 214);
    for (int i = 0; i < 4; ++i) SetWindowLongPtrW(state.edit_cmyk[i], GWLP_ID, state.viewer == ViewerMode::CMYK ? 201 + i : 221 + i);
    for (int i = 0; i < 3; ++i) SetWindowLongPtrW(state.edit_hsb[i], GWLP_ID, state.viewer == ViewerMode::Hsb ? 201 + i : 231 + i);
    for (int i = 0; i < 3; ++i) SetWindowLongPtrW(state.edit_hsl[i], GWLP_ID, state.viewer == ViewerMode::Hsl ? 201 + i : 241 + i);
    for (int i = 0; i < 3; ++i) SetWindowLongPtrW(state.edit_lab[i], GWLP_ID, 251 + i);
    SetWindowLongPtrW(state.edit_width, GWLP_ID, 206);
    SetWindowLongPtrW(state.edit_opacity, GWLP_ID, 207);
}

void select_viewer(HWND combo, EditorState &state, ViewerMode mode) {
    for (int i = 0; i < SendMessageW(combo, CB_GETCOUNT, 0, 0); ++i)
        if (SendMessageW(combo, CB_GETITEMDATA, i, 0) == static_cast<LRESULT>(mode))
            SendMessageW(combo, CB_SETCURSEL, i, 0);
    state.viewer = mode;
    assign_viewer_ids(state);
}

void select_model(HWND combo, Model model) {
    for (int i = 0; i < SendMessageW(combo, CB_GETCOUNT, 0, 0); ++i)
        if (SendMessageW(combo, CB_GETITEMDATA, i, 0) == model_index(model))
            SendMessageW(combo, CB_SETCURSEL, i, 0);
}

void set_overprint_caption(HWND dialog, const EditorState &state) {
    if (!has_overprint(state.context)) return;
    const auto check = SendDlgItemMessageW(dialog, kOverprintId, BM_GETCHECK, 0, 0);
    std::wstring caption = stroke_overprint(state.context)
        ? (state.vi ? L"Overprint viền" : L"Overprint stroke")
        : (state.vi ? L"Overprint tô" : L"Overprint fill");
    caption += check == BST_INDETERMINATE ? (state.vi ? L" · Mixed" : L" · Mixed")
             : check == BST_CHECKED ? (state.vi ? L" · Bật" : L" · On")
                                    : (state.vi ? L" · Tắt" : L" · Off");
    SetDlgItemTextW(dialog, kOverprintId, caption.c_str());
}

color::RGBColor current_active_rgb(const EditorState &state) {
    return hsv_to_rgb(state.current_hue, state.current_sat, state.current_val, state.current_alpha);
}

void sync_inputs_from_state(HWND dialog, EditorState &state) {
    (void)dialog;
    state.populating = true;
    const auto rgb = current_active_rgb(state);
    auto cmyk = rgb_to_cmyk(rgb);
    if (state.model == Model::Cmyk) {
        if (const auto *c = std::get_if<color::CMYKColor>(&state.draft.payload())) {
            cmyk = {c->c, c->m, c->y, c->k};
        }
    }
    const auto hsl = to_hsl(rgb, state.current_hue);
    const auto lab = rgb_to_lab(rgb);

    set_edit_text_if_changed(state.edit_rgb[0], format_channel_number(rgb.r * 255.0, 0));
    set_edit_text_if_changed(state.edit_rgb[1], format_channel_number(rgb.g * 255.0, 0));
    set_edit_text_if_changed(state.edit_rgb[2], format_channel_number(rgb.b * 255.0, 0));
    set_edit_text_if_changed(state.edit_hex, format_hex_color(rgb));

    set_edit_text_if_changed(state.edit_hsb[0], format_channel_number(state.current_hue, 0));
    set_edit_text_if_changed(state.edit_hsb[1], format_channel_number(state.current_sat * 100.0, 0));
    set_edit_text_if_changed(state.edit_hsb[2], format_channel_number(state.current_val * 100.0, 0));

    set_edit_text_if_changed(state.edit_cmyk[0], format_channel_number(cmyk[0] * 100.0, 0));
    set_edit_text_if_changed(state.edit_cmyk[1], format_channel_number(cmyk[1] * 100.0, 0));
    set_edit_text_if_changed(state.edit_cmyk[2], format_channel_number(cmyk[2] * 100.0, 0));
    set_edit_text_if_changed(state.edit_cmyk[3], format_channel_number(cmyk[3] * 100.0, 0));

    set_edit_text_if_changed(state.edit_hsl[0], format_channel_number(hsl.h, 0));
    set_edit_text_if_changed(state.edit_hsl[1], format_channel_number(hsl.s * 100.0, 0));
    set_edit_text_if_changed(state.edit_hsl[2], format_channel_number(hsl.l * 100.0, 0));

    set_edit_text_if_changed(state.edit_lab[0], format_channel_number(lab.l, 0));
    set_edit_text_if_changed(state.edit_lab[1], format_channel_number(lab.a, 0));
    set_edit_text_if_changed(state.edit_lab[2], format_channel_number(lab.b, 0));

    set_edit_text_if_changed(state.edit_opacity, format_channel_number(state.current_alpha * 100.0, 0));
    if (state.stroke) {
        set_edit_text_if_changed(state.edit_width, format_channel_number(state.width, 2));
    }
    state.populating = false;
}

void apply_active_color_to_draft(EditorState &state) {
    const auto rgb = current_active_rgb(state);
    auto next = color::ColorValue::make_rgb(rgb.r, rgb.g, rgb.b, rgb.a);
    next.set_overprint(state.draft.overprint());
    state.draft = source_color::convert(next, state.model);
    state.color_changed = true;
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
    (void)dialog;
    const double hue = state.current_hue;
    const int plane_width = picker_plane.right - picker_plane.left;
    const int plane_height = picker_plane.bottom - picker_plane.top;

    if (state.cached_plane.empty() ||
        std::abs(state.cached_hue - hue) > 0.01 ||
        state.cached_viewer != state.viewer ||
        state.cached_plane_w != plane_width ||
        state.cached_plane_h != plane_height) {
        state.cached_plane.resize(static_cast<size_t>(plane_width * plane_height));
        for (int y = 0; y < plane_height; ++y) {
            const double vertical = 1.0 - static_cast<double>(y) / (plane_height - 1);
            for (int x = 0; x < plane_width; ++x) {
                const double saturation = static_cast<double>(x) / (plane_width - 1);
                const auto color = is_hsl(state.viewer)
                    ? hsl_to_rgb(hue, saturation, vertical)
                    : hsv_to_rgb(hue, saturation, vertical);
                state.cached_plane[static_cast<size_t>(y * plane_width + x)] = dib_pixel(color);
            }
        }
        state.cached_hue = hue;
        state.cached_viewer = state.viewer;
        state.cached_plane_w = plane_width;
        state.cached_plane_h = plane_height;
    }

    const int hue_width = picker_hue.right - picker_hue.left;
    if (state.cached_hue_pixels.empty() || state.cached_hue_w != hue_width) {
        state.cached_hue_pixels.resize(static_cast<size_t>(hue_width));
        for (int x = 0; x < hue_width; ++x) {
            state.cached_hue_pixels[static_cast<size_t>(x)] =
                dib_pixel(hsv_to_rgb(360.0 * x / (hue_width - 1), 1.0, 1.0));
        }
        state.cached_hue_w = hue_width;
    }

    // Bounding box enclosing both picker plane, hue bar, and clear margins
    const int bbox_left = picker_plane.left - 6;
    const int bbox_top = picker_plane.top - 6;
    const int bbox_right = picker_plane.right + 6;
    const int bbox_bottom = picker_hue.bottom + 6;
    const int bbox_w = bbox_right - bbox_left;
    const int bbox_h = bbox_bottom - bbox_top;

    HDC mem_dc = CreateCompatibleDC(dc);
    HBITMAP mem_bmp = CreateCompatibleBitmap(dc, bbox_w, bbox_h);
    HGDIOBJ old_bmp = SelectObject(mem_dc, mem_bmp);

    RECT local_fill{0, 0, bbox_w, bbox_h};
    FillRect(mem_dc, &local_fill, state.background_brush);

    const int ox = -bbox_left;
    const int oy = -bbox_top;

    RECT local_plane{picker_plane.left + ox, picker_plane.top + oy,
                     picker_plane.right + ox, picker_plane.bottom + oy};
    draw_dib(mem_dc, local_plane, plane_width, plane_height, state.cached_plane);

    HPEN border_pen = CreatePen(PS_SOLID, 1, state.border);
    auto old_pen = SelectObject(mem_dc, border_pen);
    auto old_brush = SelectObject(mem_dc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(mem_dc, local_plane.left - 1, local_plane.top - 1, local_plane.right + 1, local_plane.bottom + 1);

    const auto current_rgb = hsv_to_rgb(state.current_hue, state.current_sat, state.current_val);
    const auto hsl = to_hsl(current_rgb, state.current_hue);
    const double marker_vertical = is_hsl(state.viewer) ? hsl.l : state.current_val;
    const double marker_sat = is_hsl(state.viewer) ? hsl.s : state.current_sat;
    const int marker_x = local_plane.left + static_cast<int>(std::lround(marker_sat * (plane_width - 1)));
    const int marker_y = local_plane.top + static_cast<int>(std::lround((1.0 - marker_vertical) * (plane_height - 1)));

    // Corel-style square marker with double border
    HPEN white = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    HPEN dark = CreatePen(PS_SOLID, 1, RGB(20, 24, 30));
    SelectObject(mem_dc, white);
    Rectangle(mem_dc, marker_x - 4, marker_y - 4, marker_x + 5, marker_y + 5);
    SelectObject(mem_dc, dark);
    Rectangle(mem_dc, marker_x - 3, marker_y - 3, marker_x + 4, marker_y + 4);
    DeleteObject(white);
    DeleteObject(dark);

    RECT local_hue{picker_hue.left + ox, picker_hue.top + oy,
                   picker_hue.right + ox, picker_hue.bottom + oy};
    draw_dib(mem_dc, local_hue, hue_width, 1, state.cached_hue_pixels);
    Rectangle(mem_dc, local_hue.left - 1, local_hue.top - 1, local_hue.right + 1, local_hue.bottom + 1);

    // Corel-style slider box cursor on hue bar
    const int hue_x = local_hue.left + static_cast<int>(std::lround(hue / 360.0 * (hue_width - 1)));
    HPEN hue_dark = CreatePen(PS_SOLID, 2, RGB(20, 24, 30)), old_hp = static_cast<HPEN>(SelectObject(mem_dc, hue_dark));
    Rectangle(mem_dc, hue_x - 3, local_hue.top - 2, hue_x + 4, local_hue.bottom + 3);
    HPEN hue_white = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    SelectObject(mem_dc, hue_white);
    Rectangle(mem_dc, hue_x - 2, local_hue.top - 1, hue_x + 3, local_hue.bottom + 2);
    SelectObject(mem_dc, old_hp); DeleteObject(hue_dark); DeleteObject(hue_white);

    SelectObject(mem_dc, old_pen); SelectObject(mem_dc, old_brush);
    DeleteObject(border_pen);

    // Blit whole picker area in one atomic shot to window DC
    BitBlt(dc, bbox_left, bbox_top, bbox_w, bbox_h, mem_dc, 0, 0, SRCCOPY);

    SelectObject(mem_dc, old_bmp);
    DeleteObject(mem_bmp);
    DeleteDC(mem_dc);
}

void update_picker_from_mouse(HWND dialog, EditorState &state, int x, int y) {
    if (state.drag_target == DragTarget::None) return;

    if (state.drag_target == DragTarget::Hue) {
        const int hue_w = picker_hue.right - picker_hue.left;
        const double fraction = hue_w > 1
            ? std::clamp(static_cast<double>(x - picker_hue.left) / (hue_w - 1), 0.0, 1.0)
            : 0.0;
        state.current_hue = std::clamp(fraction * 360.0, 0.0, 360.0);
    } else if (state.drag_target == DragTarget::Plane) {
        const int plane_w = picker_plane.right - picker_plane.left;
        const int plane_h = picker_plane.bottom - picker_plane.top;
        const double sat = plane_w > 1
            ? std::clamp(static_cast<double>(x - picker_plane.left) / (plane_w - 1), 0.0, 1.0)
            : 0.0;
        const double vert = plane_h > 1
            ? std::clamp(static_cast<double>(y - picker_plane.top) / (plane_h - 1), 0.0, 1.0)
            : 0.0;
        if (is_hsl(state.viewer)) {
            const auto rgb = hsl_to_rgb(state.current_hue, sat, 1.0 - vert);
            const auto hsv = to_hsv(rgb, state.current_hue);
            state.current_sat = hsv.s;
            state.current_val = hsv.v;
        } else {
            state.current_sat = sat;
            state.current_val = 1.0 - vert;
        }
    }

    apply_active_color_to_draft(state);
    sync_inputs_from_state(dialog, state);

    RECT picker_area{picker_plane.left - 6, picker_plane.top - 6,
                     picker_plane.right + 6, picker_hue.bottom + 8};
    InvalidateRect(dialog, &picker_area, FALSE);
    RECT swatch_area{16, 274, 110, 320};
    InvalidateRect(dialog, &swatch_area, FALSE);
}

void draw_swatch(HWND dialog, const DRAWITEMSTRUCT &item, const EditorState &state) {
    (void)dialog;
    const auto rgb = colorref_from(item.CtlID == 120 ? state.original : state.draft);
    HBRUSH brush = CreateSolidBrush(rgb), old_b = static_cast<HBRUSH>(SelectObject(item.hDC, brush));
    HPEN pen = CreatePen(PS_SOLID, 1, state.border), old_p = static_cast<HPEN>(SelectObject(item.hDC, pen));
    RoundRect(item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right, item.rcItem.bottom, 4, 4);
    SelectObject(item.hDC, old_b); SelectObject(item.hDC, old_p);
    DeleteObject(brush); DeleteObject(pen);
}

void initialize_editor(HWND dialog, EditorState &state) {
    state.populating = true;
    const wchar_t *title = state.context == ColorEditContext::Stroke
        ? (state.vi ? L"Chỉnh viền và màu (Edit Outline)" : L"Edit Outline")
        : (state.vi ? L"Chỉnh màu tô (Edit Fill)" : L"Edit Fill");
    SetWindowTextW(dialog, title);

    state.font = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    state.font_bold = CreateFontW(-12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    state.font_small = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    RECT dlg_rect{0, 0, kClientWidth, kClientHeight};
    AdjustWindowRectEx(&dlg_rect, WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, FALSE, 0);
    const int dialog_width = dlg_rect.right - dlg_rect.left;
    const int dialog_height = dlg_rect.bottom - dlg_rect.top;

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

    // Configure DWM attributes for crisp modern borders
    typedef HRESULT (WINAPI *DwmSetWindowAttributeFn)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE dwm = GetModuleHandleW(L"dwmapi.dll");
    if (!dwm) dwm = LoadLibraryW(L"dwmapi.dll");
    if (dwm) {
        auto set_attr = reinterpret_cast<DwmSetWindowAttributeFn>(
            GetProcAddress(dwm, "DwmSetWindowAttribute"));
        if (set_attr) {
            // Windows 11: DWMWA_WINDOW_CORNER_PREFERENCE = 33, DWMWCP_DONOTROUND = 1
            DWORD corner = 1;
            set_attr(dialog, 33, &corner, sizeof(corner));
        }
    }
    const ULONG_PTR class_style = GetClassLongPtrW(dialog, GCL_STYLE);
    if (class_style & CS_DROPSHADOW) {
        SetClassLongPtrW(dialog, GCL_STYLE, class_style & ~CS_DROPSHADOW);
    }
    SetWindowPos(dialog, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

    state.background = state.dark ? RGB(20, 26, 38) : RGB(246, 248, 250);
    state.text = state.dark ? RGB(241, 245, 249) : RGB(30, 41, 59);
    state.muted = state.dark ? RGB(148, 163, 184) : RGB(100, 116, 139);
    state.border = state.dark ? RGB(45, 55, 72) : RGB(203, 213, 225);
    state.edit_background = state.dark ? RGB(28, 36, 50) : RGB(255, 255, 255);
    state.accent = state.dark ? RGB(0, 210, 175) : RGB(14, 116, 144);
    state.background_brush = CreateSolidBrush(state.background);
    state.edit_brush = CreateSolidBrush(state.edit_background);

    const auto rgb = rgb_from(state.draft);
    const auto hsv = to_hsv(rgb, 0.0);
    state.current_hue = hsv.h;
    state.current_sat = hsv.s;
    state.current_val = hsv.v;
    state.current_alpha = rgb.a;

    // Header left: Uniform fill + Viewer dropdown
    control(dialog, L"STATIC", state.vi ? L"Màu đồng nhất" : L"Uniform Fill",
            0, 16, 12, 190, 16, 0, state.font_bold);
    const auto viewer = control(dialog, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                                16, 30, 190, 160, 110, state.font);
    const wchar_t *viewers_en[] = {L"HSB · Hue based", L"HSL · Lightness", L"RGB · 0–255", L"CMYK · 0–100%"};
    const wchar_t *viewers_vi[] = {L"HSB · Sắc độ", L"HSL · Độ sáng", L"RGB · 0–255", L"CMYK · 0–100%"};
    const auto *viewers = state.vi ? viewers_vi : viewers_en;
    for (int i = 0; i < 4; ++i) {
        const auto item = SendMessageW(viewer, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(viewers[i]));
        SendMessageW(viewer, CB_SETITEMDATA, item, i);
    }

    // Header right: Color model
    control(dialog, L"STATIC", state.vi ? L"Hệ màu đích:" : L"Color model:",
            0, 226, 12, 100, 16, 0, state.font);
    const auto combo = control(dialog, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                               226, 30, 238, 160, 100, state.font);
    const wchar_t *models_en[] = {L"RGB · 0–255", L"CMYK · 0–100%", L"Spot · name + tint", L"Grayscale · 0–100%"};
    const wchar_t *models_vi[] = {L"RGB · 0–255", L"CMYK · 0–100%", L"Spot · tên + tint", L"Thang xám · 0–100%"};
    const auto *models = state.vi ? models_vi : models_en;
    for (int m = 0; m < 4; ++m) {
        const auto index = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(models[m]));
        SendMessageW(combo, CB_SETITEMDATA, index, m);
    }
    state.model = source_color::model_of(state.draft);
    select_model(combo, state.model);

    // Right Column: CorelDRAW 3-column input grid
    // Column 1: RGB (top), Hex, HSB (bottom)
    const wchar_t *rgb_lbls[] = {L"R:", L"G:", L"B:"};
    for (int i = 0; i < 3; ++i) {
        control(dialog, L"STATIC", rgb_lbls[i], SS_RIGHT, 216, 62 + i * 26, 14, 18, 0, state.font);
        state.edit_rgb[i] = control(dialog, L"EDIT", L"", ES_AUTOHSCROLL | ES_RIGHT | WS_TABSTOP,
                                    232, 60 + i * 26, 44, 21, 211 + i, state.font);
    }
    state.edit_hex = control(dialog, L"EDIT", L"", ES_AUTOHSCROLL | ES_CENTER | WS_TABSTOP,
                             216, 138, 60, 21, 214, state.font);
    const wchar_t *hsb_lbls[] = {L"H:", L"S:", L"B:"};
    for (int i = 0; i < 3; ++i) {
        control(dialog, L"STATIC", hsb_lbls[i], SS_RIGHT, 216, 168 + i * 26, 14, 18, 0, state.font);
        state.edit_hsb[i] = control(dialog, L"EDIT", L"", ES_AUTOHSCROLL | ES_RIGHT | WS_TABSTOP,
                                    232, 166 + i * 26, 44, 21, 231 + i, state.font);
    }

    // Column 2: CMYK (top), HLS (bottom)
    const wchar_t *cmyk_lbls[] = {L"C:", L"M:", L"Y:", L"K:"};
    for (int i = 0; i < 4; ++i) {
        control(dialog, L"STATIC", cmyk_lbls[i], SS_RIGHT, 296, 62 + i * 26, 14, 18, 0, state.font);
        state.edit_cmyk[i] = control(dialog, L"EDIT", L"", ES_AUTOHSCROLL | ES_RIGHT | WS_TABSTOP,
                                     312, 60 + i * 26, 44, 21, 221 + i, state.font);
    }
    const wchar_t *hsl_lbls[] = {L"H:", L"S:", L"L:"};
    for (int i = 0; i < 3; ++i) {
        control(dialog, L"STATIC", hsl_lbls[i], SS_RIGHT, 296, 168 + i * 26, 14, 18, 0, state.font);
        state.edit_hsl[i] = control(dialog, L"EDIT", L"", ES_AUTOHSCROLL | ES_RIGHT | WS_TABSTOP,
                                    312, 166 + i * 26, 44, 21, 241 + i, state.font);
    }

    // Column 3: Lab (top), Opacity, Width
    const wchar_t *lab_lbls[] = {L"L:", L"a:", L"b:"};
    for (int i = 0; i < 3; ++i) {
        control(dialog, L"STATIC", lab_lbls[i], SS_RIGHT, 376, 62 + i * 26, 14, 18, 0, state.font);
        state.edit_lab[i] = control(dialog, L"EDIT", L"", ES_AUTOHSCROLL | ES_RIGHT | WS_TABSTOP,
                                    392, 60 + i * 26, 44, 21, 251 + i, state.font);
    }
    control(dialog, L"STATIC", state.vi ? L"Độ:" : L"Op:", SS_RIGHT, 366, 168, 24, 18, 0, state.font);
    state.edit_opacity = control(dialog, L"EDIT", L"", ES_AUTOHSCROLL | ES_RIGHT | WS_TABSTOP,
                                 392, 166, 44, 21, 207, state.font);
    control(dialog, L"STATIC", L"%", 0, 440, 168, 14, 18, 0, state.font_small);

    control(dialog, L"STATIC", state.vi ? L"Dày:" : L"W:", SS_RIGHT, 366, 194, 24, 18, 313, state.font);
    state.edit_width = control(dialog, L"EDIT", format_channel_number(state.width, 2).c_str(),
                               ES_AUTOHSCROLL | ES_RIGHT | WS_TABSTOP, 392, 192, 44, 21, 206, state.font);
    control(dialog, L"STATIC", L"mm", 0, 440, 194, 20, 18, 314, state.font_small);
    EnableWindow(state.edit_width, state.stroke);
    ShowWindow(GetDlgItem(dialog, 313), state.stroke ? SW_SHOW : SW_HIDE);
    ShowWindow(state.edit_width, state.stroke ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dialog, 314), state.stroke ? SW_SHOW : SW_HIDE);

    // Swatches (bottom left under Hue bar)
    control(dialog, L"STATIC", state.vi ? L"Mới" : L"New", 0, 16, 274, 36, 14, 0, state.font_small);
    control(dialog, L"STATIC", L"", SS_OWNERDRAW, 16, 290, 42, 26, 121, state.font);
    control(dialog, L"STATIC", state.vi ? L"Gốc" : L"Orig", 0, 64, 274, 36, 14, 0, state.font_small);
    control(dialog, L"STATIC", L"", SS_OWNERDRAW, 64, 290, 42, 26, 120, state.font);

    // Bottom row: Overprint checkbox & OK/Cancel buttons
    if (has_overprint(state.context)) {
        const auto check = control(dialog, L"BUTTON", L"", BS_3STATE | WS_TABSTOP,
                                   216, 252, 250, 20, kOverprintId, state.font);
        state.initial_overprint = stroke_overprint(state.context)
            ? state.original.overprint().stroke : state.original.overprint().fill;
        SendMessageW(check, BM_SETCHECK, state.mixed_overprint ? BST_INDETERMINATE
            : state.initial_overprint ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    set_overprint_caption(dialog, state);

    control(dialog, L"BUTTON", state.vi ? L"Đồng ý" : L"OK",
            BS_DEFPUSHBUTTON | BS_OWNERDRAW | WS_TABSTOP, 312, 286, 72, 26, IDOK, state.font);
    control(dialog, L"BUTTON", state.vi ? L"Hủy" : L"Cancel",
            BS_OWNERDRAW | WS_TABSTOP, 394, 286, 72, 26, IDCANCEL, state.font);

    select_viewer(viewer, state, ViewerMode::Hsb);
    sync_inputs_from_state(dialog, state);
    state.populating = false;
}

void update_hsv_and_refresh(HWND dialog, EditorState &state, Hsv hsv) {
    state.current_hue = hsv.h;
    state.current_sat = hsv.s;
    state.current_val = hsv.v;
    apply_active_color_to_draft(state);
    sync_inputs_from_state(dialog, state);
    InvalidateRect(dialog, nullptr, FALSE);
}

void on_color_input_changed(HWND dialog, EditorState &state, int id) {
    if (state.populating) return;

    if (id >= 201 && id <= 204) {
        if (state.viewer == ViewerMode::Hsb) {
            const auto h = read_number_from_hwnd(state.edit_hsb[0], 360.0);
            const auto s = read_number_from_hwnd(state.edit_hsb[1], 100.0);
            const auto v = read_number_from_hwnd(state.edit_hsb[2], 100.0);
            if (h && s && v) update_hsv_and_refresh(dialog, state, {*h, *s / 100.0, *v / 100.0});
        } else if (state.viewer == ViewerMode::Hsl) {
            const auto h = read_number_from_hwnd(state.edit_hsl[0], 360.0);
            const auto s = read_number_from_hwnd(state.edit_hsl[1], 100.0);
            const auto l = read_number_from_hwnd(state.edit_hsl[2], 100.0);
            if (h && s && l) update_hsv_and_refresh(dialog, state, to_hsv(hsl_to_rgb(*h, *s / 100.0, *l / 100.0, state.current_alpha), *h));
        } else if (state.viewer == ViewerMode::CMYK) {
            const auto c = read_number_from_hwnd(state.edit_cmyk[0], 100.0);
            const auto m = read_number_from_hwnd(state.edit_cmyk[1], 100.0);
            const auto y = read_number_from_hwnd(state.edit_cmyk[2], 100.0);
            const auto k = read_number_from_hwnd(state.edit_cmyk[3], 100.0);
            if (c && m && y && k) {
                const auto rgb = cmyk_to_rgb(*c / 100.0, *m / 100.0, *y / 100.0, *k / 100.0, state.current_alpha);
                const auto hsv = to_hsv(rgb, state.current_hue);
                state.current_hue = hsv.h;
                state.current_sat = hsv.s;
                state.current_val = hsv.v;
                if (state.model == Model::Cmyk) {
                    state.draft = color::ColorValue::make_cmyk(*c / 100.0, *m / 100.0, *y / 100.0, *k / 100.0, state.current_alpha);
                    state.color_changed = true;
                } else {
                    apply_active_color_to_draft(state);
                }
                sync_inputs_from_state(dialog, state);
                InvalidateRect(dialog, nullptr, FALSE);
            }
        } else if (state.viewer == ViewerMode::Rgb) {
            const auto r = read_number_from_hwnd(state.edit_rgb[0], 255.0);
            const auto g = read_number_from_hwnd(state.edit_rgb[1], 255.0);
            const auto b = read_number_from_hwnd(state.edit_rgb[2], 255.0);
            if (r && g && b) update_hsv_and_refresh(dialog, state, to_hsv({*r / 255.0, *g / 255.0, *b / 255.0, state.current_alpha}, state.current_hue));
        }
    } else if (id >= 211 && id <= 213) {
        const auto r = read_number_from_hwnd(state.edit_rgb[0], 255.0);
        const auto g = read_number_from_hwnd(state.edit_rgb[1], 255.0);
        const auto b = read_number_from_hwnd(state.edit_rgb[2], 255.0);
        if (r && g && b) update_hsv_and_refresh(dialog, state, to_hsv({*r / 255.0, *g / 255.0, *b / 255.0, state.current_alpha}, state.current_hue));
    } else if (id == 214) {
        wchar_t buf[32]{};
        GetWindowTextW(state.edit_hex, buf, 32);
        if (const auto parsed = parse_hex_color(buf))
            update_hsv_and_refresh(dialog, state, to_hsv(*parsed, state.current_hue));
    } else if (id >= 221 && id <= 224) {
        const auto c = read_number_from_hwnd(state.edit_cmyk[0], 100.0);
        const auto m = read_number_from_hwnd(state.edit_cmyk[1], 100.0);
        const auto y = read_number_from_hwnd(state.edit_cmyk[2], 100.0);
        const auto k = read_number_from_hwnd(state.edit_cmyk[3], 100.0);
        if (c && m && y && k) {
            const auto rgb = cmyk_to_rgb(*c / 100.0, *m / 100.0, *y / 100.0, *k / 100.0, state.current_alpha);
            const auto hsv = to_hsv(rgb, state.current_hue);
            state.current_hue = hsv.h;
            state.current_sat = hsv.s;
            state.current_val = hsv.v;
            if (state.model == Model::Cmyk) {
                state.draft = color::ColorValue::make_cmyk(*c / 100.0, *m / 100.0, *y / 100.0, *k / 100.0, state.current_alpha);
                state.color_changed = true;
            } else {
                apply_active_color_to_draft(state);
            }
            sync_inputs_from_state(dialog, state);
            InvalidateRect(dialog, nullptr, FALSE);
        }
    } else if (id >= 231 && id <= 233) {
        const auto h = read_number_from_hwnd(state.edit_hsb[0], 360.0);
        const auto s = read_number_from_hwnd(state.edit_hsb[1], 100.0);
        const auto v = read_number_from_hwnd(state.edit_hsb[2], 100.0);
        if (h && s && v) update_hsv_and_refresh(dialog, state, {*h, *s / 100.0, *v / 100.0});
    } else if (id >= 241 && id <= 243) {
        const auto h = read_number_from_hwnd(state.edit_hsl[0], 360.0);
        const auto s = read_number_from_hwnd(state.edit_hsl[1], 100.0);
        const auto l = read_number_from_hwnd(state.edit_hsl[2], 100.0);
        if (h && s && l) update_hsv_and_refresh(dialog, state, to_hsv(hsl_to_rgb(*h, *s / 100.0, *l / 100.0, state.current_alpha), *h));
    } else if (id >= 251 && id <= 253) {
        const auto l = read_number_from_hwnd(state.edit_lab[0], 100.0);
        const auto a = read_number_from_hwnd(state.edit_lab[1], 127.0);
        const auto b = read_number_from_hwnd(state.edit_lab[2], 127.0);
        if (l && a && b) update_hsv_and_refresh(dialog, state, to_hsv(lab_to_rgb(*l, *a, *b, state.current_alpha), state.current_hue));
    } else if (id == 207) {
        if (const auto a = read_number_from_hwnd(state.edit_opacity, 100.0)) {
            state.current_alpha = *a / 100.0;
            if (state.model == Model::Cmyk) {
                if (const auto *cmyk = std::get_if<color::CMYKColor>(&state.draft.payload())) {
                    state.draft = color::ColorValue::make_cmyk(cmyk->c, cmyk->m, cmyk->y, cmyk->k, state.current_alpha);
                    state.color_changed = true;
                } else {
                    apply_active_color_to_draft(state);
                }
            } else {
                apply_active_color_to_draft(state);
            }
            sync_inputs_from_state(dialog, state);
            InvalidateRect(dialog, nullptr, FALSE);
        }
    } else if (id == 206) {
        state.width_changed = true;
    }
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
    if (message == WM_ERASEBKGND || message == WM_PRINTCLIENT) {
        RECT client{};
        GetClientRect(dialog, &client);
        FillRect(reinterpret_cast<HDC>(wparam), &client, state->background_brush);
        return TRUE;
    }
    if (message == WM_CTLCOLORDLG) return reinterpret_cast<INT_PTR>(state->background_brush);
    if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN) {
        const auto dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(dc, state->text);
        SetBkColor(dc, state->background);
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
            const bool selected = (item->itemState & ODS_SELECTED) != 0;
            const bool focused = (item->itemState & ODS_FOCUS) != 0;
            RECT rect = item->rcItem;

            COLORREF fill = primary
                ? (selected ? RGB(10, 95, 120) : state->accent)
                : (selected ? RGB(220, 226, 234) : (state->dark ? RGB(36, 46, 60) : RGB(238, 242, 246)));
            COLORREF border_col = primary
                ? (focused ? RGB(255, 255, 255) : state->accent)
                : (focused ? state->accent : state->border);

            HBRUSH brush = CreateSolidBrush(fill);
            HPEN pen = CreatePen(PS_SOLID, 1, border_col);
            const auto old_brush = SelectObject(item->hDC, brush);
            const auto old_pen = SelectObject(item->hDC, pen);
            RoundRect(item->hDC, rect.left, rect.top, rect.right, rect.bottom, 6, 6);
            SelectObject(item->hDC, old_brush);
            SelectObject(item->hDC, old_pen);
            DeleteObject(brush);
            DeleteObject(pen);

            wchar_t caption[64]{};
            GetWindowTextW(item->hwndItem, caption, 64);
            SetBkMode(item->hDC, TRANSPARENT);
            SetTextColor(item->hDC, primary ? RGB(255, 255, 255) : state->text);
            HFONT btn_font = primary ? state->font_bold : state->font;
            if (btn_font) SelectObject(item->hDC, btn_font);
            DrawTextW(item->hDC, caption, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }
    }
    if (message == WM_LBUTTONDOWN) {
        const int x = GET_X_LPARAM(lparam), y = GET_Y_LPARAM(lparam);
        if (x >= picker_plane.left && x <= picker_plane.right &&
            y >= picker_plane.top && y <= picker_plane.bottom) {
            state->drag_target = DragTarget::Plane;
            SetCapture(dialog);
            update_picker_from_mouse(dialog, *state, x, y);
            return TRUE;
        }
        if (x >= picker_hue.left && x <= picker_hue.right &&
            y >= picker_hue.top && y <= picker_hue.bottom) {
            state->drag_target = DragTarget::Hue;
            SetCapture(dialog);
            update_picker_from_mouse(dialog, *state, x, y);
            return TRUE;
        }
        // Clicking on empty background/header space moves the dialog:
        // Convert to SCREEN coordinates to ensure smooth, zero-latency dragging!
        ReleaseCapture();
        POINT pt{x, y};
        ClientToScreen(dialog, &pt);
        SendMessageW(dialog, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(pt.x, pt.y));
        return TRUE;
    }
    if (message == WM_MOUSEMOVE && state->drag_target != DragTarget::None) {
        const int x = GET_X_LPARAM(lparam), y = GET_Y_LPARAM(lparam);
        update_picker_from_mouse(dialog, *state, x, y);
        return TRUE;
    }
    if (message == WM_LBUTTONUP && state->drag_target != DragTarget::None) {
        const int x = GET_X_LPARAM(lparam), y = GET_Y_LPARAM(lparam);
        update_picker_from_mouse(dialog, *state, x, y);
        state->drag_target = DragTarget::None;
        if (GetCapture() == dialog) ReleaseCapture();
        return TRUE;
    }
    if (message == WM_CAPTURECHANGED) {
        state->drag_target = DragTarget::None;
        return FALSE;
    }
    if (message == WM_NCDESTROY) {
        if (state->background_brush) DeleteObject(state->background_brush);
        if (state->edit_brush) DeleteObject(state->edit_brush);
        if (state->font) DeleteObject(state->font);
        if (state->font_bold) DeleteObject(state->font_bold);
        if (state->font_small) DeleteObject(state->font_small);
        SetWindowLongPtrW(dialog, DWLP_USER, 0);
        return FALSE;
    }
    if (message == WM_CLOSE) { EndDialog(dialog, IDCANCEL); return TRUE; }
    if (message != WM_COMMAND) return FALSE;
    const auto id = LOWORD(wparam);

    if (id == 100 && HIWORD(wparam) == CBN_SELCHANGE) {
        const auto selected = SendDlgItemMessageW(dialog, 100, CB_GETCURSEL, 0, 0);
        const auto target = model_from_index(static_cast<int>(SendDlgItemMessageW(dialog, 100, CB_GETITEMDATA, selected, 0)));
        state->draft = source_color::convert(state->draft, target);
        state->model = target;
        state->color_changed = true;
        apply_active_color_to_draft(*state);
        sync_inputs_from_state(dialog, *state);
        InvalidateRect(dialog, nullptr, FALSE);
        return TRUE;
    }
    if (id == 110 && HIWORD(wparam) == CBN_SELCHANGE) {
        const auto selected = SendDlgItemMessageW(dialog, 110, CB_GETCURSEL, 0, 0);
        state->viewer = viewer_from_index(static_cast<int>(SendDlgItemMessageW(dialog, 110, CB_GETITEMDATA, selected, 0)));
        assign_viewer_ids(*state);
        apply_active_color_to_draft(*state);
        sync_inputs_from_state(dialog, *state);
        InvalidateRect(dialog, nullptr, FALSE);
        return TRUE;
    }
    if (HIWORD(wparam) == EN_CHANGE) {
        on_color_input_changed(dialog, *state, id);
        return TRUE;
    }
    if (id == kOverprintId && HIWORD(wparam) == BN_CLICKED) {
        const auto previous = SendDlgItemMessageW(dialog, kOverprintId, BM_GETCHECK, 0, 0);
        const auto next = previous == BST_INDETERMINATE ? BST_CHECKED
            : previous == BST_CHECKED ? BST_UNCHECKED : BST_CHECKED;
        SendDlgItemMessageW(dialog, kOverprintId, BM_SETCHECK, next, 0);
        state->overprint_changed = state->mixed_overprint ||
            (next == BST_CHECKED) != state->initial_overprint;
        set_overprint_caption(dialog, *state);
        return TRUE;
    }
    if (id == IDOK) {
        const auto width = read_number_from_hwnd(state->edit_width, 10000.0).value_or(state->width);
        if (!state->color_changed) state->draft = state->original;
        const auto overprint = state->overprint_changed
            ? std::optional<bool>{SendDlgItemMessageW(dialog, kOverprintId, BM_GETCHECK, 0, 0) == BST_CHECKED}
            : std::nullopt;
        state->result = StyleEditorResult{state->draft, width, state->color_changed,
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
    layout.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    layout.dialog.cx = 242;
    layout.dialog.cy = 163;
    EditorState state;
    state.original = initial; state.draft = initial; state.width = width_mm;
    state.document_mode = document_mode;
    state.stroke = stroke; state.vi = vietnamese; state.context = context;
    state.mixed_color = mixed_color; state.mixed_overprint = mixed_overprint;
    state.dark = dark_theme;
    DialogBoxIndirectParamW(GetModuleHandleW(nullptr), &layout.dialog, owner, editor_proc,
                            reinterpret_cast<LPARAM>(&state));
    return state.result;
}
#endif
} // namespace pikdraw::desktop
