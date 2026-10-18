#include "pikdraw/desktop/shell/native_window.hpp"
#include "native_window_document_color.hpp"
#include "native_window_menus.hpp"
#include <array>

namespace pikdraw::desktop::document_color_ui {
const char* action_id(int action) noexcept {
    switch (action) {
    case kCmyk: return "action.edit.color_mode_cmyk";
    case kRgb: return "action.edit.color_mode_rgb";
    case kDpi300: return "action.edit.raster_dpi_300";
    case kDpi150: return "action.edit.raster_dpi_150";
    case kDpi72: return "action.edit.raster_dpi_72";
    default: return "";
    }
}
void register_actions(actions::ActionDispatcher& dispatcher,
    std::function<void(color::DocumentColorMode)> convert, std::function<void(int32_t)> set_dpi) {
    dispatcher.register_action({action_id(kCmyk), "CMYK (In ấn)", "CMYK (Print)", "",
        [convert] { convert(color::DocumentColorMode::Cmyk); }, nullptr});
    dispatcher.register_action({action_id(kRgb), "RGB (Màn hình)", "RGB (Screen)", "",
        [convert] { convert(color::DocumentColorMode::Rgb); }, nullptr});
    for (auto [action, dpi] : {std::pair{kDpi300, 300}, {kDpi150, 150}, {kDpi72, 72}})
        dispatcher.register_action({action_id(action), std::to_string(dpi) + " DPI",
            std::to_string(dpi) + " DPI", "", [set_dpi, dpi] { set_dpi(dpi); }, nullptr});
}
#ifdef _WIN32
bool confirm_conversion(HWND owner, bool vi) {
    return MessageBoxW(owner, vi
        ? L"Chuyển toàn bộ màu process trên mọi trang và layer sang hệ màu mới?\n"
          L"Giá trị màu sẽ được ánh xạ lại; một số màu có thể nằm ngoài gamut.\n"
          L"Dùng công thức gần đúng khi chưa có ICC nguồn/đích thực. Spot và overprint được giữ.\n"
          L"Ctrl+Z khôi phục chính xác màu gốc."
        : L"Convert process colors on every page and layer to the new document mode?\n"
          L"Channel values will be remapped; some colors may fall outside the destination gamut.\n"
          L"Approximate conversion is used without actual source/destination ICC profiles. Spot and overprint survive.\n"
          L"Ctrl+Z restores the exact original colors.",
        vi ? L"Hệ màu tài liệu" : L"Document Color Mode", MB_OKCANCEL | MB_ICONINFORMATION) == IDOK;
}
int choose_setting(HWND owner, const document::DocumentSnapshot& snapshot, bool vi, bool dpi, int menu_left) {
    const auto menu = CreatePopupMenu();
    if (!menu) return 0;
    if (dpi) {
        for (auto [action, value] : {std::pair{kDpi300, 300}, {kDpi150, 150}, {kDpi72, 72}}) {
            const std::wstring label = std::to_wstring(value) + (vi
                ? (value == 300 ? L" DPI (Cao / In ấn)" : value == 150 ? L" DPI (Trung bình)" : L" DPI (Màn hình)")
                : (value == 300 ? L" DPI (High / Print)" : value == 150 ? L" DPI (Medium)" : L" DPI (Screen)"));
            AppendMenuW(menu, MF_STRING | (snapshot.raster_dpi() == value ? MF_CHECKED : 0),
                action, label.c_str());
        }
    } else {
        AppendMenuW(menu, MF_STRING | (snapshot.color_mode() == color::DocumentColorMode::Cmyk ? MF_CHECKED : 0),
            kCmyk, vi ? L"CMYK (In ấn)" : L"CMYK (Print)");
        AppendMenuW(menu, MF_STRING | (snapshot.color_mode() == color::DocumentColorMode::Rgb ? MF_CHECKED : 0),
            kRgb, vi ? L"RGB (Màn hình)" : L"RGB (Screen)");
    }
    RECT client{};
    GetClientRect(owner, &client);
    using GetDpi = UINT(WINAPI*)(HWND);
    const auto get_dpi = reinterpret_cast<GetDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    const double scale = get_dpi ? std::max(1u, get_dpi(owner)) / 96.0 : 1.0;
    const int width = static_cast<int>(client.right / scale);
    const int height = static_cast<int>(client.bottom / scale);
    int row = kMenuPadding;
    for (const auto& item : get_menu_items(MenuId::Edit)) {
        if (static_cast<int>(item.action) == (dpi ? kDpiMenu : kModeMenu)) break;
        row += item.is_separator ? kMenuSeparatorHeight : kMenuRowHeight;
    }
    // Anchor consistently for mouse and keyboard activation.
    POINT point{static_cast<LONG>((std::clamp(menu_left, 4, std::max(4, width - 304)) + 296) * scale),
        static_cast<LONG>((std::clamp(38, 4, std::max(4, height - get_menu_height(MenuId::Edit) - 4)) + row) * scale)};
    ClientToScreen(owner, &point);
    const int chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
        point.x, point.y, 0, owner, nullptr);
    DestroyMenu(menu);
    return chosen;
}
#endif
} // namespace pikdraw::desktop::document_color_ui
