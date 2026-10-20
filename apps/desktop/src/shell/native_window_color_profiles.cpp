#include "pikdraw/desktop/shell/native_window.hpp"
#include "../utf8_path.hpp"
#include <fstream>
#include <utility>
#ifdef _WIN32
#include <commdlg.h>
#endif

namespace pikdraw::desktop {
bool NativeWindow::configure_preview_profiles(const std::filesystem::path &rgb_source,
                                              const std::filesystem::path &cmyk_source, std::string *error) {
    const auto preview = color::IccPreview::create(rgb_source, cmyk_source, error);
    if (!preview)
        return false;
    if (!root().renderer().backend().set_color_preview(preview)) {
        if (error)
            *error = "Current render backend does not support ICC preview";
        return false;
    }
    color_preview_ = preview;
    rgb_preview_profile_ = rgb_source;
    cmyk_preview_profile_ = cmyk_source;
    update_color_preview_options();
    return true;
}

void NativeWindow::update_color_preview_options() noexcept {
    if (color_preview_) {
        color::PreviewOptions options;
        options.proof_colors = proof_colors_enabled_;
        options.gamut_warning = gamut_warning_enabled_;
        options.overprint_preview = overprint_preview_enabled_;
        color_preview_->set_preview_options(options);
    }
    canvas_cache_valid_ = false;
#ifdef _WIN32
    if (hwnd_)
        InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}
#ifdef _WIN32
namespace {
std::pair<std::filesystem::path, std::filesystem::path> default_profile_sources() {
    const auto rgb = color::IccPreview::system_srgb_profile();
    auto cmyk = rgb.empty() ? std::filesystem::path{} : rgb.parent_path() / L"RSWOP.icm";
    std::error_code error;
    if (cmyk.empty() || !std::filesystem::is_regular_file(cmyk, error))
        cmyk.clear();
    return {rgb, cmyk};
}
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
    const auto defaults = default_profile_sources();
    auto rgb = defaults.first;
    auto cmyk = defaults.second;
    const auto file = preferences_file();
    if (!file.empty()) {
        std::ifstream input(file, std::ios::binary);
        std::string version, saved_rgb, saved_cmyk;
        if (std::getline(input, version) && version == "pikdraw-preview-profiles-v1" &&
            std::getline(input, saved_rgb) && std::getline(input, saved_cmyk) &&
            saved_rgb.size() <= 32768 && saved_cmyk.size() <= 32768) {
            if (!saved_rgb.empty())
                rgb = detail::path_from_utf8(saved_rgb);
            if (!saved_cmyk.empty())
                cmyk = detail::path_from_utf8(saved_cmyk);
        }
    }
    std::string error;
    if (configure_preview_profiles(rgb, cmyk, &error))
        return;
    if ((rgb != defaults.first || cmyk != defaults.second) &&
        configure_preview_profiles(defaults.first, defaults.second, &error)) {
        status_msg_ = language_ == UILanguage::Vietnamese
                          ? "ICC tùy chỉnh không khả dụng; đã khôi phục profile mặc định."
                          : "A saved ICC profile is unavailable; default profiles were restored.";
        save_preferences(defaults.first, defaults.second);
        return;
    }
    status_msg_ = language_ == UILanguage::Vietnamese
                      ? "ICC preview không khả dụng; màu CMYK đang hiển thị gần đúng."
                      : "ICC preview unavailable; CMYK colors are shown approximately.";
}
void NativeWindow::show_color_profiles() {
    const bool vi = language_ == UILanguage::Vietnamese;
    const auto menu = CreatePopupMenu();
    const auto defaults = default_profile_sources();
    const auto rgb_name = rgb_preview_profile_.empty() || rgb_preview_profile_ == defaults.first
                              ? std::wstring(L"sRGB IEC61966-2.1")
                              : rgb_preview_profile_.filename().wstring();
    const auto cmyk_name = cmyk_preview_profile_.empty()
                               ? (vi ? std::wstring(L"CMYK: gần đúng · chưa có ICC")
                                     : std::wstring(L"CMYK: approximate · no ICC"))
                               : L"CMYK: " + (cmyk_preview_profile_ == defaults.second
                                    ? std::wstring(L"U.S. Web Coated (SWOP) v2")
                                    : cmyk_preview_profile_.filename().wstring());
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, (L"RGB: " + rgb_name).c_str());
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, cmyk_name.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 1, vi ? L"Nạp ICC nguồn RGB…" : L"Load source RGB ICC…");
    AppendMenuW(menu, MF_STRING, 2, vi ? L"Nạp ICC nguồn CMYK…" : L"Load source CMYK ICC…");
    const auto swop = defaults.second;
    if (!swop.empty())
        AppendMenuW(menu, MF_STRING, 5,
                    vi ? L"Dùng U.S. Web Coated (SWOP) v2" : L"Use U.S. Web Coated (SWOP) v2");
    AppendMenuW(menu, MF_STRING, 3,
                vi ? L"Khôi phục ICC mặc định (sRGB + SWOP)" : L"Restore default ICC profiles (sRGB + SWOP)");
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
        rgb = defaults.first;
        cmyk = defaults.second;
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
