#include "native_style_dialog.hpp"
#include "pikdraw/color/preview/color_transform.hpp"
#include <array>
#include <cwchar>
#include <cwctype>
#include <iomanip>
#include <sstream>

namespace pikdraw::desktop {
#ifdef _WIN32
namespace {
struct EditorState {
    color::ColorValue original, draft;
    double width;
    bool stroke, vi;
    int model{0};
    bool populating{false}, changed{false};
    std::optional<StyleEditorResult> result;
};
HWND control(HWND dialog, const wchar_t *type, const wchar_t *text, DWORD style, int x, int y, int width,
             int height, int id) {
    const auto child = CreateWindowExW(std::wcscmp(type, L"EDIT") == 0 ? WS_EX_CLIENTEDGE : 0, type, text,
                                       WS_CHILD | WS_VISIBLE | style, x, y, width, height, dialog,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                       GetModuleHandleW(nullptr), nullptr);
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return child;
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
    while (end && std::iswspace(*end))
        ++end;
    if (end == text.c_str() || !end || *end || !std::isfinite(result) || result < 0 || result > maximum)
        return std::nullopt;
    return result;
}
std::optional<color::ColorValue> read_color(HWND dialog, const EditorState &state) {
    std::array<double, 4> channels{};
    const int count = state.model == 0 ? 3 : 4;
    for (int i = 0; i < count; ++i) {
        const auto value = read_number(dialog, 201 + i, state.model == 0 ? 255 : 100);
        if (!value)
            return std::nullopt;
        channels[static_cast<size_t>(i)] = *value / (state.model == 0 ? 255 : 100);
    }
    const auto alpha = read_number(dialog, 205, 100);
    if (!alpha)
        return std::nullopt;
    if (state.model == 0)
        return color::ColorValue::make_rgb(channels[0], channels[1], channels[2], *alpha / 100);
    return color::ColorValue::make_cmyk(channels[0], channels[1], channels[2], channels[3], *alpha / 100);
}
void populate(HWND dialog, EditorState &state) {
    state.populating = true;
    const auto rgb = color::to_preview_rgba(state.draft);
    std::array<double, 4> values{rgb.r * 255.0, rgb.g * 255.0, rgb.b * 255.0, 0};
    double alpha = rgb.a;
    if (state.model == 1) {
        if (const auto *cmyk = std::get_if<color::CMYKColor>(&state.draft.payload())) {
            values = {cmyk->c * 100, cmyk->m * 100, cmyk->y * 100, cmyk->k * 100};
            alpha = cmyk->a;
        } else {
            const double k = 1.0 - std::max({rgb.r, rgb.g, rgb.b});
            const double divisor = 1.0 - k;
            values = divisor <= 1e-9 ? std::array<double, 4>{0, 0, 0, 100}
                                     : std::array<double, 4>{(1 - rgb.r - k) / divisor * 100,
                                                             (1 - rgb.g - k) / divisor * 100,
                                                             (1 - rgb.b - k) / divisor * 100, k * 100};
        }
    } else if (const auto *source = std::get_if<color::RGBColor>(&state.draft.payload())) {
        values = {source->r * 255, source->g * 255, source->b * 255, 0};
        alpha = source->a;
    }
    const wchar_t *rgb_labels[] = {L"R (0–255)", L"G (0–255)", L"B (0–255)", L""};
    const wchar_t *cmyk_labels[] = {L"C (%)", L"M (%)", L"Y (%)", L"K (%)"};
    for (int i = 0; i < 4; ++i) {
        SetDlgItemTextW(dialog, 101 + i, state.model == 0 ? rgb_labels[i] : cmyk_labels[i]);
        SetDlgItemTextW(dialog, 201 + i, number(values[static_cast<size_t>(i)]).c_str());
        ShowWindow(GetDlgItem(dialog, 201 + i), i == 3 && state.model == 0 ? SW_HIDE : SW_SHOW);
    }
    SetDlgItemTextW(dialog, 205, number(alpha * 100).c_str());
    state.populating = false;
}
void initialize_editor(HWND dialog, EditorState &state) {
    SetWindowTextW(dialog, state.vi
                               ? (state.stroke ? L"Màu và độ dày viền" : L"Màu tô nguồn")
                               : (state.stroke ? L"Source stroke color and width" : L"Source fill color"));
    RECT parent{};
    GetWindowRect(GetParent(dialog), &parent);
    SetWindowPos(dialog, nullptr, (parent.left + parent.right - 410) / 2,
                 (parent.top + parent.bottom - 405) / 2, 410, 405, SWP_NOZORDER);
    control(dialog, L"STATIC", state.vi ? L"Hệ màu nguồn" : L"Source color model", 0, 16, 20, 115, 22, 0);
    const auto combo =
        control(dialog, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 145, 16, 235, 150, 100);
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"RGB · 0–255"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"CMYK · 0–100%"));
    state.model = state.original.color_space() == color::ColorSpace::Cmyk ? 1 : 0;
    SendMessageW(combo, CB_SETCURSEL, state.model, 0);
    for (int i = 0; i < 4; ++i) {
        control(dialog, L"STATIC", L"", 0, 16, 60 + i * 32, 120, 22, 101 + i);
        control(dialog, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, 145, 56 + i * 32, 235, 25, 201 + i);
    }
    control(dialog, L"STATIC", state.vi ? L"Độ đục (%)" : L"Opacity (%)", 0, 16, 194, 120, 22, 0);
    control(dialog, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, 145, 190, 235, 25, 205);
    control(dialog, L"STATIC", state.vi ? L"Viền nguồn (mm)" : L"Source width (mm)", 0, 16, 230, 125, 22, 0);
    const auto width = control(dialog, L"EDIT", number(state.width).c_str(), ES_AUTOHSCROLL | WS_TABSTOP, 145,
                               226, 235, 25, 206);
    EnableWindow(width, state.stroke);
    control(dialog, L"STATIC",
            state.vi ? L"Kênh nguồn được lưu nguyên dạng. Đổi hệ ở đây dùng công thức gần đúng; ICC được cấu "
                       L"hình riêng."
                     : L"Source channels are retained. Model switching here uses an approximate formula; ICC "
                       L"is configured separately.",
            0, 16, 267, 365, 45, 0);
    control(dialog, L"BUTTON", state.vi ? L"Áp dụng" : L"Apply", BS_DEFPUSHBUTTON | WS_TABSTOP, 205, 325, 85,
            28, IDOK);
    control(dialog, L"BUTTON", state.vi ? L"Hủy" : L"Cancel", WS_TABSTOP, 300, 325, 80, 28, IDCANCEL);
    populate(dialog, state);
}
INT_PTR CALLBACK editor_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    auto *state = reinterpret_cast<EditorState *>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<EditorState *>(lparam);
        SetWindowLongPtrW(dialog, DWLP_USER, lparam);
        initialize_editor(dialog, *state);
        return TRUE;
    }
    if (message != WM_COMMAND || !state)
        return FALSE;
    const auto id = LOWORD(wparam);
    if (id == 100 && HIWORD(wparam) == CBN_SELCHANGE) {
        const auto old = read_color(dialog, *state);
        if (old)
            state->draft = *old;
        state->model = static_cast<int>(SendDlgItemMessageW(dialog, 100, CB_GETCURSEL, 0, 0));
        state->changed = true;
        populate(dialog, *state);
        return TRUE;
    }
    if (id >= 201 && id <= 205 && HIWORD(wparam) == EN_CHANGE && !state->populating)
        state->changed = true;
    if (id == IDOK) {
        auto color = read_color(dialog, *state);
        const auto width = read_number(dialog, 206, 10000);
        if (!color || !width) {
            MessageBoxW(dialog,
                        state->vi ? L"Giá trị không hợp lệ. RGB: 0–255; CMYK/độ đục: 0–100; viền: 0–10000 mm."
                                  : L"Invalid values. RGB: 0–255; CMYK/opacity: 0–100; width: 0–10000 mm.",
                        L"PikDraw", MB_OK | MB_ICONWARNING);
            return TRUE;
        }
        if (!state->changed)
            color = state->original;
        state->result = StyleEditorResult{*color, *width};
        EndDialog(dialog, IDOK);
        return TRUE;
    }
    if (id == IDCANCEL) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}
} // namespace
std::optional<StyleEditorResult> edit_source_color(HWND owner, const color::ColorValue &initial,
                                                   double width_mm, bool stroke, bool vietnamese) {
    struct EmptyDialog {
        DLGTEMPLATE dialog;
        WORD menu{0}, klass{0}, title{0};
    } layout{};
    layout.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
    layout.dialog.cx = 260;
    layout.dialog.cy = 240;
    EditorState state{initial, initial, width_mm, stroke, vietnamese};
    DialogBoxIndirectParamW(GetModuleHandleW(nullptr), &layout.dialog, owner, editor_proc,
                            reinterpret_cast<LPARAM>(&state));
    return state.result;
}
#endif
} // namespace pikdraw::desktop
