#include "pikdraw/desktop/shell/native_window.hpp"
#include <fstream>
#ifdef _WIN32
#include <commdlg.h>
#endif

namespace pikdraw::desktop {
bool NativeWindow::configure_preview_profiles(const std::filesystem::path &rgb_source,
                                              const std::filesystem::path &cmyk_source, std::string *error) {
    const auto preview = color::IccPreview::create(rgb_source, cmyk_source, error);
    if (!preview)
        return false;
    if (!root_.renderer().backend().set_color_preview(preview)) {
        if (error)
            *error = "Current render backend does not support ICC preview";
        return false;
    }
    color_preview_ = preview;
    rgb_preview_profile_ = rgb_source;
    cmyk_preview_profile_ = cmyk_source;
#ifdef _WIN32
    if (hwnd_)
        InvalidateRect(hwnd_, nullptr, FALSE);
#endif
    return true;
}
#ifdef _WIN32
namespace {
std::filesystem::path preferences_file() {
    wchar_t local[32768]{};
    const auto size = GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768);
    if (size == 0 || size >= 32768)
        return {};
    return std::filesystem::path(local) / L"PikDraw" / L"preview-profiles.txt";
}
std::string utf8_path(const std::filesystem::path &path) {
    const auto text = path.u8string();
    return std::string(reinterpret_cast<const char *>(text.data()), text.size());
}
void save_preferences(const std::filesystem::path &rgb, const std::filesystem::path &cmyk) {
    const auto file = preferences_file();
    if (file.empty())
        return;
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    if (output)
        output << "pikdraw-preview-profiles-v1\n" << utf8_path(rgb) << '\n' << utf8_path(cmyk) << '\n';
}
std::optional<std::filesystem::path> choose_profile(HWND owner, bool cmyk, bool vi) {
    wchar_t path[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"ICC profiles (*.icc;*.icm)\0*.icc;*.icm\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = 32768;
    dialog.lpstrTitle = cmyk ? (vi ? L"Chọn ICC nguồn CMYK" : L"Select source CMYK ICC profile")
                             : (vi ? L"Chọn ICC nguồn RGB" : L"Select source RGB ICC profile");
    const auto directory = color::IccPreview::system_srgb_profile().parent_path().wstring();
    dialog.lpstrInitialDir = directory.c_str();
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog))
        return std::nullopt;
    return std::filesystem::path(path);
}
} // namespace
void NativeWindow::load_color_preferences() {
    const auto file = preferences_file();
    if (file.empty())
        return;
    std::ifstream input(file, std::ios::binary);
    std::string version, rgb, cmyk;
    if (!std::getline(input, version) || version != "pikdraw-preview-profiles-v1" ||
        !std::getline(input, rgb) || !std::getline(input, cmyk) || rgb.size() > 32768 || cmyk.size() > 32768)
        return;
    std::string error;
    if (!configure_preview_profiles(std::filesystem::u8path(rgb), std::filesystem::u8path(cmyk), &error))
        status_msg_ = language_ == UILanguage::Vietnamese
                          ? "Profile preview không khả dụng; đang dùng preview gần đúng."
                          : "Preview profile unavailable; approximate preview is active.";
}
void NativeWindow::show_color_profiles() {
    const bool vi = language_ == UILanguage::Vietnamese;
    const auto menu = CreatePopupMenu();
    const auto rgb_name = rgb_preview_profile_.empty() ? std::wstring(L"sRGB (Windows)")
                                                       : rgb_preview_profile_.filename().wstring();
    const auto cmyk_name = cmyk_preview_profile_.empty()
                               ? (vi ? std::wstring(L"CMYK: gần đúng · chưa có ICC")
                                     : std::wstring(L"CMYK: approximate · no ICC"))
                               : L"CMYK: " + cmyk_preview_profile_.filename().wstring();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, (L"RGB: " + rgb_name).c_str());
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, cmyk_name.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 1, vi ? L"Nạp ICC nguồn RGB…" : L"Load source RGB ICC…");
    AppendMenuW(menu, MF_STRING, 2, vi ? L"Nạp ICC nguồn CMYK…" : L"Load source CMYK ICC…");
    const auto swop = color::IccPreview::system_srgb_profile().parent_path() / L"RSWOP.icm";
    std::error_code lookup_error;
    if (std::filesystem::is_regular_file(swop, lookup_error))
        AppendMenuW(menu, MF_STRING, 5,
                    vi ? L"CMYK: SWOP có sẵn trên Windows" : L"CMYK: installed Windows SWOP profile");
    AppendMenuW(menu, MF_STRING, 3, vi ? L"Khôi phục preview sRGB" : L"Reset preview to sRGB");
    AppendMenuW(menu, MF_STRING, 4, vi ? L"Phạm vi quản lý màu…" : L"Color management scope…");
    POINT point{};
    GetCursorPos(&point);
    const auto action =
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (action == 4) {
        MessageBoxW(
            hwnd_,
            vi ? L"ICC nguồn RGB/CMYK được chuyển sang sRGB bằng Windows Color Management. Thiết lập được "
                 L"lưu trên máy và chỉ đổi preview, không đổi kênh nguồn.\n\nChưa có: gán/nhúng ICC trong "
                 L"tài liệu, soft proof mực giấy, hoặc xuất PDF/X. Chưa nạp ICC CMYK thì preview CMYK dùng "
                 L"công thức gần đúng."
               : L"Source RGB/CMYK ICC profiles convert to sRGB through Windows Color Management. Settings "
                 L"are saved on this machine and affect preview only; source channels remain intact.\n\nNot "
                 L"yet available: document ICC assignment/embedding, paper/ink soft proof, or PDF/X export. "
                 L"CMYK preview is approximate until a CMYK ICC is loaded.",
            L"PikDraw · ICC preview", MB_OK | MB_ICONINFORMATION);
        return;
    }
    auto rgb = rgb_preview_profile_, cmyk = cmyk_preview_profile_;
    if (action == 1 || action == 2) {
        const auto path = choose_profile(hwnd_, action == 2, vi);
        if (!path)
            return;
        if (action == 1)
            rgb = *path;
        else
            cmyk = *path;
    } else if (action == 5) {
        cmyk = swop;
    } else if (action == 3) {
        rgb.clear();
        cmyk.clear();
    } else
        return;
    std::string error;
    if (!configure_preview_profiles(rgb, cmyk, &error)) {
        const auto message =
            utf8_to_wide((vi ? "Không thể áp dụng profile. " : "Could not apply profile. ") + error);
        MessageBoxW(hwnd_, message.c_str(), L"PikDraw ICC", MB_OK | MB_ICONWARNING);
        return;
    }
    save_preferences(rgb, cmyk);
    status_msg_ = vi ? "Đã cập nhật ICC preview · dữ liệu nguồn được giữ nguyên"
                     : "ICC preview updated · source data preserved";
    InvalidateRect(hwnd_, nullptr, FALSE);
}
#endif
} // namespace pikdraw::desktop
