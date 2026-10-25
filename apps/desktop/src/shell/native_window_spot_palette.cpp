#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/desktop/presenters/spot_palette_catalog.hpp"
#include <algorithm>
#include <array>
#include <cwchar>
#include <cwctype>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <commdlg.h>
#endif

namespace pikdraw::desktop {
#ifdef _WIN32
namespace {
constexpr int kLibrary = 101;
constexpr int kImport = 102;
constexpr int kSearch = 103;
constexpr int kList = 104;
constexpr int kTint = 105;
constexpr int kStatus = 107;
constexpr int kApply = IDOK;
constexpr int kCancel = IDCANCEL;

struct PickerState {
    NativeWindow* owner{nullptr};
    HWND dialog{nullptr};
    HWND library{nullptr};
    HWND search{nullptr};
    HWND list{nullptr};
    HWND tint{nullptr};
    HWND status{nullptr};
    std::vector<SpotPaletteLibrary> libraries;
    size_t library_index{0};
    bool target_stroke{false};
    bool vietnamese{false};
    bool done{false};
    bool applied{false};
};

std::wstring lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return value;
}

std::wstring swatch_label(const SpotPaletteSwatch& swatch) {
    const auto name = NativeWindow::utf8_to_wide(swatch.name);
    if (swatch.group.empty()) return name;
    return NativeWindow::utf8_to_wide(swatch.group) + L"  ·  " + name;
}

std::wstring control_text(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<size_t>(std::max(0, length)) + 1, L'\0');
    const int copied = GetWindowTextW(control, value.data(), length + 1);
    value.resize(static_cast<size_t>(std::max(0, copied)));
    return value;
}

HWND add_control(PickerState& state, const wchar_t* type, const std::wstring& text,
                 DWORD style, int id, int x, int y, int width, int height,
                 DWORD ex_style = 0) {
    HWND control = CreateWindowExW(ex_style, type, text.c_str(),
        WS_CHILD | WS_VISIBLE | style, state.owner->dip(x), state.owner->dip(y),
        state.owner->dip(width), state.owner->dip(height), state.dialog,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    if (control)
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return control;
}

void update_status(PickerState& state, const std::wstring& message) {
    if (state.status) SetWindowTextW(state.status, message.c_str());
}

std::wstring search_text(PickerState& state) {
    return lower(control_text(state.search));
}

void refresh_swatch_list(PickerState& state) {
    if (!state.list || state.library_index >= state.libraries.size()) return;
    SendMessageW(state.list, LB_RESETCONTENT, 0, 0);
    const auto query = search_text(state);
    const auto& swatches = state.libraries[state.library_index].swatches;
    size_t matches = 0;
    for (size_t i = 0; i < swatches.size(); ++i) {
        const auto name = lower(NativeWindow::utf8_to_wide(swatches[i].name));
        const auto group = lower(NativeWindow::utf8_to_wide(swatches[i].group));
        if (!query.empty() && name.find(query) == std::wstring::npos &&
            group.find(query) == std::wstring::npos)
            continue;
        const auto label = swatch_label(swatches[i]);
        const LRESULT row = SendMessageW(state.list, LB_ADDSTRING, 0,
                                         reinterpret_cast<LPARAM>(label.c_str()));
        if (row == LB_ERR || row == LB_ERRSPACE) continue;
        SendMessageW(state.list, LB_SETITEMDATA, static_cast<WPARAM>(row),
                     static_cast<LPARAM>(i));
        if (matches++ == 0)
            SendMessageW(state.list, LB_SETCURSEL, static_cast<WPARAM>(row), 0);
    }
    InvalidateRect(state.list, nullptr, TRUE);
    update_status(state, matches == 0
        ? (state.vietnamese ? L"Không tìm thấy màu Spot." : L"No Spot swatches match this search.")
        : (state.vietnamese ? L"Chọn màu rồi nhấn Áp dụng." : L"Select a swatch, then choose Apply."));
}

void select_library(PickerState& state, size_t index) {
    if (index >= state.libraries.size()) return;
    state.library_index = index;
    refresh_swatch_list(state);
}

void add_library_to_combo(PickerState& state, size_t index) {
    const auto label = NativeWindow::utf8_to_wide(state.libraries[index].name);
    const LRESULT row = SendMessageW(state.library, CB_ADDSTRING, 0,
                                     reinterpret_cast<LPARAM>(label.c_str()));
    if (row != CB_ERR && row != CB_ERRSPACE)
        SendMessageW(state.library, CB_SETITEMDATA, static_cast<WPARAM>(row),
                     static_cast<LPARAM>(index));
}

void import_library(PickerState& state) {
    std::array<wchar_t, 32768> path{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = state.dialog;
    dialog.lpstrFilter = L"Adobe Swatch Exchange (*.ase)\0*.ase\0\0";
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrTitle = state.vietnamese ? L"Nhập thư viện Spot ASE" : L"Import Spot ASE library";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return;
    SpotPaletteLibrary imported;
    std::string error;
    if (!import_ase_spot_palette(path.data(), spot_palette_directory(), imported, &error)) {
        const auto message = NativeWindow::utf8_to_wide(error);
        MessageBoxW(state.dialog, message.c_str(), L"PikDraw · Spot palette",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    const size_t index = state.libraries.size();
    state.libraries.push_back(std::move(imported));
    add_library_to_combo(state, index);
    SendMessageW(state.library, CB_SETCURSEL,
                 static_cast<WPARAM>(SendMessageW(state.library, CB_GETCOUNT, 0, 0) - 1), 0);
    select_library(state, index);
}

bool apply_swatch(PickerState& state) {
    const LRESULT row = SendMessageW(state.list, LB_GETCURSEL, 0, 0);
    if (row == LB_ERR) {
        update_status(state, state.vietnamese ? L"Chọn một màu Spot trước." : L"Select a Spot swatch first.");
        return false;
    }
    const LRESULT data = SendMessageW(state.list, LB_GETITEMDATA, static_cast<WPARAM>(row), 0);
    const auto& swatches = state.libraries[state.library_index].swatches;
    if (data < 0 || static_cast<size_t>(data) >= swatches.size()) return false;
    const auto tint_text = control_text(state.tint);
    wchar_t* end = nullptr;
    const long tint = std::wcstol(tint_text.c_str(), &end, 10);
    if (tint_text.empty() || !end || *end != L'\0' || tint < 0 || tint > 100) {
        update_status(state, state.vietnamese ? L"Tint phải nằm trong khoảng 0–100%."
                                              : L"Tint must be between 0 and 100%.");
        SetFocus(state.tint);
        return false;
    }
    const auto& swatch = swatches[static_cast<size_t>(data)];
    color::ColorValue value{color::SpotColor{swatch.name, tint / 100.0,
                                              swatch.alternate_rgb, false}};
    state.applied = state.owner->apply_selection_color(state.target_stroke, std::move(value));
    if (!state.applied) {
        update_status(state, state.vietnamese ? L"Không thể áp màu vào vùng chọn hiện tại."
                                              : L"This Spot color cannot be applied to the current selection.");
    }
    return state.applied;
}

void create_controls(PickerState& state) {
    const bool vi = state.vietnamese;
    add_control(state, L"STATIC", vi ? L"Thư viện" : L"Library", 0, 0, 18, 18, 76, 24);
    state.library = add_control(state, L"COMBOBOX", L"",
        CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, kLibrary, 96, 15, 386, 180);
    add_control(state, L"BUTTON", vi ? L"Nhập ASE…" : L"Import ASE…",
        BS_PUSHBUTTON | WS_TABSTOP, kImport, 492, 16, 105, 27);
    add_control(state, L"STATIC", vi ? L"Tìm tên hoặc nhóm" : L"Search name or group",
        0, 0, 18, 54, 170, 24);
    state.search = add_control(state, L"EDIT", L"",
        ES_AUTOHSCROLL | WS_TABSTOP | WS_BORDER, kSearch, 188, 51, 409, 24);
    state.list = add_control(state, L"LISTBOX", L"",
        LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT |
            WS_TABSTOP | WS_VSCROLL | WS_BORDER,
        kList, 18, 83, 579, 318);
    add_control(state, L"STATIC", vi ? L"Tint (%)" : L"Tint (%)", 0, 0, 18, 412, 62, 24);
    state.tint = add_control(state, L"EDIT", L"100",
        ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP | WS_BORDER, kTint, 82, 410, 65, 24);
    const auto target = vi
        ? (state.target_stroke ? L"Đích: Viền" : L"Đích: Tô màu")
        : (state.target_stroke ? L"Target: Stroke" : L"Target: Fill");
    add_control(state, L"STATIC", target, 0, 0, 170, 410, 250, 24);
    state.status = add_control(state, L"STATIC", L"", 0, kStatus, 18, 442, 390, 24);
    add_control(state, L"BUTTON", vi ? L"Áp dụng" : L"Apply",
        BS_DEFPUSHBUTTON | WS_TABSTOP, kApply, 406, 442, 88, 30);
    add_control(state, L"BUTTON", vi ? L"Hủy" : L"Cancel",
        BS_PUSHBUTTON | WS_TABSTOP, kCancel, 507, 442, 90, 30);
    for (size_t i = 0; i < state.libraries.size(); ++i) add_library_to_combo(state, i);
    SendMessageW(state.library, CB_SETCURSEL, 0, 0);
    SendMessageW(state.tint, EM_LIMITTEXT, 3, 0);
    select_library(state, 0);
}

LRESULT CALLBACK picker_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<PickerState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        state = static_cast<PickerState*>(create->lpCreateParams);
        state->dialog = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state) return DefWindowProcW(hwnd, message, wparam, lparam);
    switch (message) {
    case WM_CREATE:
        create_controls(*state);
        return 0;
    case WM_MEASUREITEM: {
        auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
        if (measure->CtlID == kList) {
            measure->itemHeight = state->owner->dip(25);
            return TRUE;
        }
        break;
    }
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (draw->CtlID != kList) break;
        const bool selected = (draw->itemState & ODS_SELECTED) != 0;
        const COLORREF background = GetSysColor(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW);
        const COLORREF foreground = GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT);
        HBRUSH back = CreateSolidBrush(background);
        FillRect(draw->hDC, &draw->rcItem, back);
        DeleteObject(back);
        if (draw->itemID != static_cast<UINT>(-1) && state->library_index < state->libraries.size()) {
            const LRESULT data = SendMessageW(state->list, LB_GETITEMDATA, draw->itemID, 0);
            const auto& swatches = state->libraries[state->library_index].swatches;
            if (data >= 0 && static_cast<size_t>(data) < swatches.size()) {
                const auto& swatch_source = swatches[static_cast<size_t>(data)];
                const color::ColorValue spot_color{color::SpotColor{
                    swatch_source.name, 1.0, swatch_source.alternate_rgb, false}};
                const auto device = state->owner->preview_color_for_ui(spot_color).to_device();
                const int left = draw->rcItem.left + state->owner->dip(5);
                const int top = draw->rcItem.top + state->owner->dip(4);
                RECT swatch{left, top, left + state->owner->dip(17), top + state->owner->dip(17)};
                HBRUSH color_brush = CreateSolidBrush(RGB(device.r, device.g, device.b));
                FillRect(draw->hDC, &swatch, color_brush);
                DeleteObject(color_brush);
                FrameRect(draw->hDC, &swatch, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
                SetBkMode(draw->hDC, TRANSPARENT);
                SetTextColor(draw->hDC, foreground);
                RECT text = draw->rcItem;
                text.left += state->owner->dip(29);
                DrawTextW(draw->hDC, swatch_label(swatches[static_cast<size_t>(data)]).c_str(), -1,
                    &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
        }
        if (draw->itemState & ODS_FOCUS) DrawFocusRect(draw->hDC, &draw->rcItem);
        return TRUE;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wparam), notification = HIWORD(wparam);
        if (id == kLibrary && notification == CBN_SELCHANGE) {
            const LRESULT row = SendMessageW(state->library, CB_GETCURSEL, 0, 0);
            const LRESULT index = row == CB_ERR ? CB_ERR
                : SendMessageW(state->library, CB_GETITEMDATA, static_cast<WPARAM>(row), 0);
            if (index >= 0) select_library(*state, static_cast<size_t>(index));
            return 0;
        }
        if (id == kSearch && notification == EN_CHANGE) {
            refresh_swatch_list(*state);
            return 0;
        }
        if (id == kImport) {
            import_library(*state);
            return 0;
        }
        if (id == kList && notification == LBN_DBLCLK) {
            if (apply_swatch(*state)) DestroyWindow(hwnd);
            return 0;
        }
        if (id == kApply) {
            if (apply_swatch(*state)) DestroyWindow(hwnd);
            return 0;
        }
        if (id == kCancel) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        state->done = true;
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

ATOM picker_class() {
    static const ATOM atom = [] {
        WNDCLASSEXW cls{};
        cls.cbSize = sizeof(cls);
        cls.lpfnWndProc = picker_proc;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        cls.lpszClassName = L"PikDrawSpotPalettePicker";
        const ATOM registered = RegisterClassExW(&cls);
        return registered ? registered : static_cast<ATOM>(GetLastError() == ERROR_CLASS_ALREADY_EXISTS ? 1 : 0);
    }();
    return atom;
}

} // namespace

void show_spot_palette_picker(NativeWindow& window, bool target_stroke) {
    PickerState state;
    state.owner = &window;
    state.target_stroke = target_stroke;
    state.vietnamese = window.language() == UILanguage::Vietnamese;
    state.libraries.push_back(sample_spot_palette());
    auto imported = load_spot_palette_libraries(spot_palette_directory());
    state.libraries.insert(state.libraries.end(),
                           std::make_move_iterator(imported.begin()),
                           std::make_move_iterator(imported.end()));
    if (!picker_class()) {
        window.set_status_message(state.vietnamese ? "Không thể mở bộ chọn màu Spot."
                                                   : "Could not open the Spot color picker.");
        return;
    }
    const int width = window.dip(620), height = window.dip(520);
    RECT parent{};
    GetWindowRect(window.hwnd(), &parent);
    const int x = parent.left + ((parent.right - parent.left) - width) / 2;
    const int y = parent.top + ((parent.bottom - parent.top) - height) / 2;
    const std::wstring title = state.vietnamese ? L"Chọn màu Spot" : L"Choose Spot color";
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
        L"PikDrawSpotPalettePicker", title.c_str(), WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, width, height, window.hwnd(), nullptr, GetModuleHandleW(nullptr), &state);
    if (!dialog) {
        window.set_status_message(state.vietnamese ? "Không thể mở bộ chọn màu Spot."
                                                   : "Could not open the Spot color picker.");
        return;
    }
    EnableWindow(window.hwnd(), FALSE);
    ShowWindow(dialog, SW_SHOW);
    SetForegroundWindow(dialog);
    SetFocus(state.list);
    MSG message{};
    while (!state.done) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) break;
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (IsWindow(dialog)) DestroyWindow(dialog);
    EnableWindow(window.hwnd(), TRUE);
    SetActiveWindow(window.hwnd());
    SetForegroundWindow(window.hwnd());
    if (state.applied) InvalidateRect(window.hwnd(), nullptr, FALSE);
}

void show_spot_palette_picker(NativeWindow& window) {
    show_spot_palette_picker(window, false);
}

#else
void show_spot_palette_picker(NativeWindow&, bool) {}
void show_spot_palette_picker(NativeWindow&) {}
#endif
} // namespace pikdraw::desktop
