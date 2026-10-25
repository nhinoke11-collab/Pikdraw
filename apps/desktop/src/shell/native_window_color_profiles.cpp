#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/desktop/actions/action_id.hpp"
#include "pikdraw/document/commands/color_commands.hpp"
#include "pikdraw/color/profiles/profile_registry.hpp"
#include "../utf8_path.hpp"
#include <algorithm>
#include <fstream>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <commdlg.h>
#include <windows.h>
#endif

namespace pikdraw::desktop {
#ifdef _WIN32
namespace {
struct DisplayProfileSelection {
    std::wstring device;
    std::filesystem::path path;
    std::shared_ptr<const color::ProfileResource> resource;
};

DisplayProfileSelection select_display_profile(
    HWND owner, const std::wstring& previous_device = {},
    const std::filesystem::path& previous_path = {},
    std::shared_ptr<const color::ProfileResource> previous_resource = {},
    bool force_reload = true) {
    DisplayProfileSelection selection;
    HMONITOR monitor = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);
    HDC display_dc = nullptr;
    HWND release_owner = nullptr;
    if (monitor && GetMonitorInfoW(monitor, &monitor_info)) {
        selection.device = monitor_info.szDevice;
        if (!force_reload && previous_resource &&
            selection.device == previous_device) {
            selection.path = previous_path;
            selection.resource = std::move(previous_resource);
            return selection;
        }
        display_dc = CreateDCW(L"DISPLAY", monitor_info.szDevice, nullptr, nullptr);
    }
    const bool owns_dc = display_dc != nullptr;
    if (!display_dc && owner) {
        display_dc = GetDC(owner);
        if (display_dc) release_owner = owner;
    }
    if (!display_dc) {
        display_dc = GetDC(nullptr);
        if (display_dc) release_owner = nullptr;
    }

    if (display_dc) {
        DWORD path_characters = 0;
        if (GetICMProfileW(display_dc, &path_characters, nullptr) &&
            path_characters >= 2 && path_characters <= 32768) {
            std::vector<wchar_t> path(path_characters, L'\0');
            if (GetICMProfileW(display_dc, &path_characters, path.data())) {
                selection.path = std::filesystem::path(path.data());
                auto loaded = color::ProfileResource::load_file(
                    selection.path, color::ColorSpace::Rgb);
                if (loaded)
                    selection.resource = std::move(loaded.resource);
            }
        }
        if (owns_dc)
            DeleteDC(display_dc);
        else
            ReleaseDC(release_owner, display_dc);
    }

    if (!selection.resource) {
        selection.path = color::IccPreview::system_srgb_profile();
        if (!selection.path.empty()) {
            auto loaded = color::ProfileResource::load_file(
                selection.path, color::ColorSpace::Rgb);
            if (loaded)
                selection.resource = std::move(loaded.resource);
        }
    }
    return selection;
}
} // namespace
#endif

bool NativeWindow::configure_preview_profiles(const std::filesystem::path &rgb_source,
                                              const std::filesystem::path &cmyk_source, std::string *error) {
    const auto effective_rgb = rgb_source.empty() ? color::IccPreview::system_srgb_profile() : rgb_source;
    auto rgb_loaded = color::ProfileResource::load_file(effective_rgb, color::ColorSpace::Rgb);
    if (!rgb_loaded) {
        if (error) *error = "RGB ICC file failed resource validation";
        return false;
    }
    std::shared_ptr<const color::ProfileResource> cmyk_resource;
    if (!cmyk_source.empty()) {
        auto cmyk_loaded = color::ProfileResource::load_file(cmyk_source, color::ColorSpace::Cmyk);
        if (!cmyk_loaded) {
            if (error) *error = "CMYK ICC file failed resource validation";
            return false;
        }
        cmyk_resource = std::move(cmyk_loaded.resource);
    }
    auto rgb_resource = std::move(rgb_loaded.resource);
#ifdef _WIN32
    auto display = select_display_profile(hwnd_);
    if (!display.resource) {
        if (error) *error = "Neither the active monitor nor system sRGB profile is available";
        return false;
    }
    const auto preview = color::IccPreview::create_from_resources(
        rgb_resource, cmyk_resource, display.resource, error);
#else
    const auto preview = color::IccPreview::create_from_resources(
        rgb_resource, cmyk_resource, error);
#endif
    if (!preview)
        return false;
    if (!root().renderer().backend().set_color_preview(preview)) {
        if (error)
            *error = "Current render backend does not support ICC preview";
        return false;
    }
    color_preview_ = preview;
    rgb_preview_resource_ = rgb_resource;
    cmyk_preview_resource_ = cmyk_resource;
#ifdef _WIN32
    display_preview_resource_ = std::move(display.resource);
    display_preview_profile_path_ = std::move(display.path);
    display_preview_device_ = std::move(display.device);
#endif
    auto& registry = color::ProfileRegistry::instance();
    (void)registry.register_resource(rgb_resource);
    if (cmyk_resource) (void)registry.register_resource(cmyk_resource);
    rgb_preview_profile_ = rgb_source;
    cmyk_preview_profile_ = cmyk_source;
    update_color_preview_options();
    return true;
}

bool NativeWindow::refresh_display_preview_profile(bool force_reload) {
#ifdef _WIN32
    if (!rgb_preview_resource_) return false;
    auto display = select_display_profile(hwnd_, display_preview_device_,
        display_preview_profile_path_, display_preview_resource_, force_reload);
    if (!display.resource) return false;
    if (!force_reload && display.device == display_preview_device_ &&
        display.resource->identity() == display_preview_resource_->identity())
        return true;

    std::string error;
    auto preview = color::IccPreview::create_from_resources(
        rgb_preview_resource_, cmyk_preview_resource_, display.resource, &error);
    if (!preview || !root().renderer().backend().set_color_preview(preview)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không thể áp profile màn hình; giữ cấu hình hiển thị trước đó."
            : "Could not apply the monitor profile; the previous display transform remains active.";
        return false;
    }

    display_preview_resource_ = std::move(display.resource);
    display_preview_profile_path_ = std::move(display.path);
    display_preview_device_ = std::move(display.device);
    color_preview_ = std::move(preview);
    render_color_preview_ = color_preview_;
    update_color_preview_options();
    return true;
#else
    (void)force_reload;
    return false;
#endif
}

void NativeWindow::update_color_preview_options() noexcept {
    try {
        auto render_preview = color_preview_;
        const auto snapshot = root().document().current_snapshot();
        const auto page_resource = snapshot ? snapshot->page().color_profile_resource : nullptr;
        page_color_preview_available_ = true;
        if (page_resource) {
            auto rgb = rgb_preview_resource_;
            auto cmyk = cmyk_preview_resource_;
            if (page_resource->identity().color_space == color::ColorSpace::Rgb)
                rgb = page_resource;
            else if (page_resource->identity().color_space == color::ColorSpace::Cmyk)
                cmyk = page_resource;
            std::string error;
            auto page_preview = color::IccPreview::create_from_resources(
                std::move(rgb), std::move(cmyk), display_preview_resource_, &error);
            if (page_preview)
                render_preview = std::move(page_preview);
            else
                page_color_preview_available_ = false;
        }
        if (render_preview && render_preview != render_color_preview_) {
            if (root().renderer().backend().set_color_preview(render_preview))
                render_color_preview_ = std::move(render_preview);
            else if (page_resource)
                page_color_preview_available_ = false;
        }
        if (color_preview_) {
            color::PreviewOptions options;
            options.proof_colors = proof_colors_enabled_;
            options.gamut_warning = gamut_warning_enabled_;
            options.overprint_preview = overprint_preview_enabled_;
            options.proof_intent = proof_intent_;
            color_preview_->set_preview_options(options);
            if (render_color_preview_ && render_color_preview_ != color_preview_)
                render_color_preview_->set_preview_options(options);
        }
        if (page_resource && !page_color_preview_available_)
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "ICC trang đã gắn nhưng preview không khả dụng; canvas dùng profile gần đúng"
                : "Page ICC is assigned but unavailable for preview; canvas uses the fallback profile";
    } catch (...) {
        page_color_preview_available_ = false;
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
void save_preferences(const std::filesystem::path &rgb,
                      const std::filesystem::path &cmyk,
                      color::IccRenderingIntent proof_intent) {
    const auto file = preferences_file();
    if (file.empty())
        return;
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    if (output)
        output << "pikdraw-preview-profiles-v2\n" << utf8_path(rgb) << '\n'
               << utf8_path(cmyk) << '\n'
               << static_cast<unsigned>(proof_intent) << '\n';
}
std::optional<std::filesystem::path> choose_profile(HWND owner, bool cmyk, bool vi,
                                                     bool destination = false,
                                                     bool any_paint_space = false) {
    wchar_t path[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"ICC profiles (*.icc;*.icm)\0*.icc;*.icm\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = 32768;
    dialog.lpstrTitle = any_paint_space
        ? (vi ? L"Chọn profile ICC cho màu được chọn" : L"Select ICC profile for selected paints")
        : destination
        ? (vi ? L"Chọn profile ICC đích" : L"Select destination ICC profile")
        : cmyk ? (vi ? L"Chọn ICC nguồn/mô phỏng CMYK"
                     : L"Select source/proof CMYK ICC profile")
               : (vi ? L"Chọn ICC nguồn RGB" : L"Select source RGB ICC profile");
    const auto directory = color::IccPreview::system_srgb_profile().parent_path().wstring();
    dialog.lpstrInitialDir = directory.c_str();
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog))
        return std::nullopt;
    return std::filesystem::path(path);
}

std::shared_ptr<const color::ProfileResource> load_process_profile(
    const std::filesystem::path& path, std::string& error) {
    auto rgb = color::ProfileResource::load_file(path, color::ColorSpace::Rgb);
    if (rgb) return std::move(rgb.resource);
    auto cmyk = color::ProfileResource::load_file(path, color::ColorSpace::Cmyk);
    if (cmyk) return std::move(cmyk.resource);
    error = "Choose a valid RGB or CMYK ICC profile. Gray/Spot profiles are unsupported.";
    return {};
}

std::shared_ptr<const color::ProfileResource> load_paint_profile(
    const std::filesystem::path& path) {
    for (const auto space : {color::ColorSpace::Rgb, color::ColorSpace::Cmyk,
                             color::ColorSpace::Grayscale}) {
        auto loaded = color::ProfileResource::load_file(path, space);
        if (loaded) return std::move(loaded.resource);
    }
    return {};
}

void collect_paint_source_profile(
    const color::ColorValue& paint,
    const std::shared_ptr<const color::ProfileResource>& page_profile,
    std::vector<std::shared_ptr<const color::ProfileResource>>& profiles,
    std::string& error) {
    if (paint.color_space() == color::ColorSpace::Spot ||
        paint.color_space() == color::ColorSpace::Grayscale)
        return;
    const auto source = paint.profile_resource() ? paint.profile_resource() : page_profile;
    if (!source) {
        error = "A selected RGB/CMYK paint has no resolved ICC source profile";
        return;
    }
    if (source->identity().color_space != paint.color_space()) {
        error = "A selected paint's ICC source color space does not match its channels";
        return;
    }
    if (std::none_of(profiles.begin(), profiles.end(), [&](const auto& item) {
            return item->identity() == source->identity();
        }))
        profiles.push_back(source);
}

std::vector<std::shared_ptr<const color::ProfileResource>> selected_paint_sources(
    const document::DocumentSnapshot& snapshot,
    const std::vector<document::ObjectId>& selected,
    std::string& error) {
    std::vector<std::shared_ptr<const color::ProfileResource>> profiles;
    const auto page_profile = snapshot.page().color_profile_resource;
    for (const auto id : selected) {
        const auto* object = snapshot.objects().lookup_ref(id);
        if (!object) {
            error = "A selected object is no longer on the active page";
            return {};
        }
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, document::PathObject> ||
                          std::is_same_v<T, document::TextObject>) {
                collect_paint_source_profile(value.fill, page_profile, profiles, error);
                if constexpr (std::is_same_v<T, document::PathObject>) {
                    collect_paint_source_profile(value.stroke, page_profile, profiles, error);
                    if (value.fill_gradient)
                        for (const auto& stop : value.fill_gradient->stops)
                            collect_paint_source_profile(stop.color, page_profile, profiles, error);
                }
                for (const auto& effect : value.appearance.effects)
                    if (const auto* shadow = std::get_if<document::DropShadowEffect>(
                            &effect.parameters))
                        collect_paint_source_profile(shadow->color, page_profile,
                                                     profiles, error);
            }
        }, *object);
        if (!error.empty()) return {};
    }
    return profiles;
}

std::optional<color::IccRenderingIntent> choose_conversion_intent(HWND owner, bool vi) {
    const auto menu = CreatePopupMenu();
    if (!menu) return std::nullopt;
    AppendMenuW(menu, MF_STRING, 1, vi ? L"Cảm nhận (Perceptual)" : L"Perceptual");
    AppendMenuW(menu, MF_STRING | MF_CHECKED, 2,
        vi ? L"Tương đối đo màu (Relative colorimetric)" : L"Relative colorimetric");
    AppendMenuW(menu, MF_STRING, 3, vi ? L"Bão hòa (Saturation)" : L"Saturation");
    AppendMenuW(menu, MF_STRING, 4,
        vi ? L"Tuyệt đối đo màu (Absolute colorimetric)" : L"Absolute colorimetric");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 5,
        vi ? L"Bù điểm đen · Windows CMM chưa hỗ trợ" : L"Black point compensation · unavailable in Windows CMM");
    RECT bounds{};
    GetWindowRect(owner, &bounds);
    const int selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
        bounds.left + 80, bounds.top + 80, 0, owner, nullptr);
    DestroyMenu(menu);
    switch (selected) {
    case 1: return color::IccRenderingIntent::Perceptual;
    case 2: return color::IccRenderingIntent::RelativeColorimetric;
    case 3: return color::IccRenderingIntent::Saturation;
    case 4: return color::IccRenderingIntent::AbsoluteColorimetric;
    default: return std::nullopt;
    }
}

const wchar_t* intent_name(color::IccRenderingIntent intent) noexcept {
    switch (intent) {
    case color::IccRenderingIntent::Perceptual: return L"Perceptual";
    case color::IccRenderingIntent::RelativeColorimetric: return L"Relative colorimetric";
    case color::IccRenderingIntent::Saturation: return L"Saturation";
    case color::IccRenderingIntent::AbsoluteColorimetric: return L"Absolute colorimetric";
    }
    return L"Unknown";
}

const char* intent_status_name(color::IccRenderingIntent intent) noexcept {
    switch (intent) {
    case color::IccRenderingIntent::Perceptual: return "Perceptual";
    case color::IccRenderingIntent::RelativeColorimetric: return "Relative colorimetric";
    case color::IccRenderingIntent::Saturation: return "Saturation";
    case color::IccRenderingIntent::AbsoluteColorimetric: return "Absolute colorimetric";
    }
    return "Unknown";
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
        if (std::getline(input, version) &&
            (version == "pikdraw-preview-profiles-v1" ||
             version == "pikdraw-preview-profiles-v2") &&
            std::getline(input, saved_rgb) && std::getline(input, saved_cmyk) &&
            saved_rgb.size() <= 32768 && saved_cmyk.size() <= 32768) {
            if (!saved_rgb.empty())
                rgb = detail::path_from_utf8(saved_rgb);
            if (!saved_cmyk.empty())
                cmyk = detail::path_from_utf8(saved_cmyk);
            if (version == "pikdraw-preview-profiles-v2") {
                std::string intent_text;
                if (std::getline(input, intent_text)) {
                    try {
                        size_t parsed = 0;
                        const auto intent = std::stoi(intent_text, &parsed);
                        if (parsed == intent_text.size() && intent >= 0 && intent <= 3)
                            proof_intent_ =
                                static_cast<color::IccRenderingIntent>(intent);
                    } catch (...) {
                    }
                }
            }
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
        save_preferences(defaults.first, defaults.second, proof_intent_);
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
    const auto display_name = display_preview_profile_path_.empty()
        ? (vi ? std::wstring(L"Màn hình: profile sRGB dự phòng")
              : std::wstring(L"Display: sRGB fallback"))
        : (vi ? std::wstring(L"Màn hình: ") : std::wstring(L"Display: ")) +
              display_preview_profile_path_.filename().wstring();
    const auto snapshot = root().document().current_snapshot();
    const auto page_profile_name = snapshot
        ? utf8_to_wide(snapshot->page().color_profile_name) : std::wstring(L"—");
    const auto page_profile_state = snapshot && snapshot->page().color_profile_resource
        ? (vi ? L"đã gắn · " : L"bound · ")
        : (vi ? L"nhãn chưa resolve · " : L"unresolved label · ");
    const auto page_profile_line = (vi ? L"ICC trang: " : L"Page ICC: ") +
        std::wstring(page_profile_state) + page_profile_name;
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, (L"RGB: " + rgb_name).c_str());
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, cmyk_name.c_str());
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, display_name.c_str());
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, page_profile_line.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 1, vi ? L"Nạp ICC nguồn RGB…" : L"Load source RGB ICC…");
    AppendMenuW(menu, MF_STRING, 2,
                vi ? L"Nạp ICC nguồn/mô phỏng CMYK…"
                   : L"Load source/proof CMYK ICC…");
    AppendMenuW(menu, MF_STRING | (root().document().is_content_locked() ? MF_GRAYED : 0), 6,
                vi ? L"Gán ICC cho trang hiện tại…" : L"Assign ICC to active page…");
    const bool can_convert_page = snapshot && snapshot->page().color_profile_resource &&
        !root().document().is_content_locked();
    AppendMenuW(menu, MF_STRING | (can_convert_page ? 0 : MF_GRAYED), 7,
                vi ? L"Chuyển màu trang sang ICC đích…" : L"Convert page colors to destination ICC…");
    const auto swop = defaults.second;
    if (!swop.empty())
        AppendMenuW(menu, MF_STRING, 5,
                    vi ? L"Dùng U.S. Web Coated (SWOP) v2" : L"Use U.S. Web Coated (SWOP) v2");
    AppendMenuW(menu, MF_STRING, 3,
                vi ? L"Khôi phục ICC mặc định (sRGB + SWOP)" : L"Restore default ICC profiles (sRGB + SWOP)");
    AppendMenuW(menu, MF_STRING, 4, vi ? L"Phạm vi quản lý màu…" : L"Color management scope…");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    constexpr const wchar_t* proof_intent_labels_en[]{
        L"Proof intent: Perceptual", L"Proof intent: Relative colorimetric",
        L"Proof intent: Saturation", L"Proof intent: Absolute colorimetric"};
    constexpr const wchar_t* proof_intent_labels_vi[]{
        L"Ý định proof: Perceptual", L"Ý định proof: Tương đối",
        L"Ý định proof: Saturation", L"Ý định proof: Tuyệt đối"};
    const auto* proof_preview = effective_color_preview();
    const UINT proof_intent_enabled = proof_preview &&
            proof_preview->has_cmyk_transform()
        ? 0u : MF_GRAYED;
    for (int intent = 0; intent < 4; ++intent) {
        const auto selected = static_cast<int>(proof_intent_) == intent
            ? MF_CHECKED : 0;
        AppendMenuW(menu, MF_STRING | selected | proof_intent_enabled, 8 + intent,
                    vi ? proof_intent_labels_vi[intent]
                       : proof_intent_labels_en[intent]);
    }
    POINT point{};
    GetCursorPos(&point);
    const auto action =
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (action == 4) {
        MessageBoxW(
            hwnd_,
            vi ? L"Nạp ICC RGB/CMYK đổi preview trên máy; ICC CMYK cũng là profile đích cho Proof Colors. "
                 L"Gán ICC đổi binding trang và giữ channels. Chuyển ICC "
                 L"đổi RGB/CMYK process paints cùng binding trong một Undo; chọn rendering intent trước khi áp dụng.\n\n"
                 L"Proof/gamut là mô phỏng kỹ thuật số qua Windows CMM và profile màn hình; không xác nhận bản in. "
                 L"Spot, grayscale và pixels ảnh giữ nguyên. Bù điểm đen và PDF/X export chưa khả dụng. "
                 L"Nếu Windows CMM không tạo được transform, tài liệu không đổi."
               : L"Loading RGB/CMYK ICC changes local preview; the CMYK profile also serves as the Proof Colors target. "
                 L"Assign changes the page binding and preserves channels. "
                 L"Convert changes RGB/CMYK process paints and the binding in one Undo; choose a rendering intent.\n\n"
                 L"Proof/gamut are digital simulations through the Windows CMM and display profile; they do not verify a print. "
                 L"Spot, grayscale and raster pixels remain unchanged. Black point compensation and PDF/X export are unavailable. "
                 L"If the Windows CMM cannot create the transform, the "
                 L"document is unchanged.",
            L"PikDraw · ICC preview", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (action == 6) {
        (void)root().window().action_dispatcher().dispatch(actions::ActionId::kColorAssignProfile);
        return;
    }
    if (action == 7) {
        (void)root().window().action_dispatcher().dispatch(actions::ActionId::kColorConvertProfile);
        return;
    }
    if (action >= 8 && action <= 11) {
        const auto requested = static_cast<color::IccRenderingIntent>(action - 8);
        const auto* preview = effective_color_preview();
        if (!preview || !preview->supports_proofing(requested)) {
            status_msg_ = vi
                ? "Windows CMM không hỗ trợ ý định proof này với bộ ICC hiện tại."
                : "The Windows CMM cannot create this proof intent for the current ICC profiles.";
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        proof_intent_ = requested;
        update_color_preview_options();
        save_preferences(rgb_preview_profile_, cmyk_preview_profile_, proof_intent_);
        status_msg_ = std::string(vi ? "Đã đổi ý định proof: " : "Proof intent: ") +
            intent_status_name(proof_intent_);
        InvalidateRect(hwnd_, nullptr, FALSE);
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
    save_preferences(rgb, cmyk, proof_intent_);
    status_msg_ = vi ? "Đã cập nhật ICC preview · dữ liệu nguồn được giữ nguyên"
                     : "ICC preview updated · source data preserved";
    InvalidateRect(hwnd_, nullptr, FALSE);
}
#endif

void NativeWindow::assign_active_page_color_profile() {
#ifdef _WIN32
    const bool vi = language_ == UILanguage::Vietnamese;
    const auto snapshot = root().document().current_snapshot();
    if (!snapshot) return;
    const auto* page = snapshot->find_page(snapshot->active_page_id());
    if (!page) return;
    const bool page_is_cmyk = page->layout.color_mode == color::DocumentColorMode::Cmyk;
    const auto path = choose_profile(hwnd_, page_is_cmyk, vi);
    if (!path) return;
    const auto space = page_is_cmyk ? color::ColorSpace::Cmyk : color::ColorSpace::Rgb;
    auto loaded = color::ProfileResource::load_file(*path, space);
    if (!loaded) {
        const auto message = vi
            ? L"ICC không hợp lệ hoặc không khớp hệ màu của trang."
            : L"The ICC file is invalid or does not match the page color mode.";
        MessageBoxW(hwnd_, message, L"PikDraw ICC", MB_OK | MB_ICONWARNING);
        return;
    }
    const auto resource = loaded.resource;
    const auto result = root().document().execute_command(
        std::make_unique<document::AssignPageColorProfileCommand>(page->id, resource));
    if (!result.success) {
        const auto message = utf8_to_wide(result.error_message);
        MessageBoxW(hwnd_, message.c_str(), L"PikDraw ICC", MB_OK | MB_ICONWARNING);
        return;
    }
    (void)color::ProfileRegistry::instance().register_resource(resource);
    update_color_preview_options();
    status_msg_ = page_color_preview_available_
        ? (vi ? "Đã gán ICC cho trang · Undo để hoàn tác"
              : "Page ICC assigned · Undo to revert")
        : (vi ? "Đã gán ICC; Windows CMM không xử lý được profile nên canvas dùng preview gần đúng"
              : "ICC assigned; Windows CMM cannot use this profile, so canvas preview is approximate");
    InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::assign_selected_paint_color_profile() {
#ifdef _WIN32
    const bool vi = language_ == UILanguage::Vietnamese;
    if (root().document().is_content_locked()) return;
    const auto snapshot = root().document().current_snapshot();
    if (!snapshot) return;
    const auto selected = root().vector_controller().selection().selected_ids();
    if (selected.empty()) return;
    const auto path = choose_profile(hwnd_, false, vi, false, true);
    if (!path) return;
    const auto resource = load_paint_profile(*path);
    if (!resource) {
        MessageBoxW(hwnd_, vi
            ? L"Chọn ICC hợp lệ cho RGB, CMYK hoặc grayscale. Profile spot không phải ICC."
            : L"Choose a valid RGB, CMYK, or grayscale ICC profile. Spot inks are not ICC profiles.",
            L"PikDraw ICC", MB_OK | MB_ICONWARNING);
        return;
    }
    std::wstring prompt = vi
        ? L"Gán cách diễn giải ICC này cho màu phù hợp trong selection?\n\n"
        : L"Assign this ICC interpretation to matching paints in the selection?\n\n";
    prompt += utf8_to_wide(resource->identity().name);
    prompt += vi
        ? L"\n\nGiá trị kênh được giữ nguyên. Chỉ màu cùng hệ RGB/CMYK/Gray được gán; màu khác hệ giữ nguyên. Một Undo hoàn tác toàn bộ."
        : L"\n\nChannel values stay unchanged. Only RGB/CMYK/Gray paints matching this profile are assigned; other models stay unchanged. One Undo restores the whole assignment.";
    if (MessageBoxW(hwnd_, prompt.c_str(), L"PikDraw ICC", MB_OKCANCEL | MB_ICONINFORMATION) != IDOK)
        return;

    if (!workflow().assign_selected_objects_color_profile(resource)) {
        const auto message = utf8_to_wide(workflow().last_color_conversion_error());
        MessageBoxW(hwnd_, message.c_str(), L"PikDraw ICC", MB_OK | MB_ICONWARNING);
        return;
    }
    (void)color::ProfileRegistry::instance().register_resource(resource);
    update_color_preview_options();
    status_msg_ = vi
        ? "Đã gán ICC cho màu phù hợp trong selection · channels giữ nguyên · Ctrl+Z hoàn tác"
        : "Assigned ICC to matching selected paints · channels preserved · Ctrl+Z to undo";
    InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::convert_selected_paint_color_profile() {
#ifdef _WIN32
    const bool vi = language_ == UILanguage::Vietnamese;
    if (root().document().is_content_locked()) return;
    if (active_num_field_ != NumericFieldId::None) {
        commit_numeric_field();
        if (active_num_field_ != NumericFieldId::None) return;
    }
    if (is_inline_text_editing_) {
        commit_inline_text_editing();
        if (is_inline_text_editing_) return;
    }
    cancel_canvas_interaction();
    const auto snapshot = root().document().current_snapshot();
    if (!snapshot) return;
    const auto selected = root().vector_controller().selection().selected_ids();
    if (selected.empty()) return;

    std::string error;
    const auto sources = selected_paint_sources(*snapshot, selected, error);
    if (!error.empty()) {
        const auto message = utf8_to_wide(error);
        MessageBoxW(hwnd_, message.c_str(), L"PikDraw ICC", MB_OK | MB_ICONWARNING);
        return;
    }
    if (sources.empty()) {
        MessageBoxW(hwnd_, vi
            ? L"Selection không có màu RGB/CMYK cần chuyển. Grayscale, Spot và ảnh được giữ nguyên."
            : L"The selection has no RGB/CMYK paints to convert. Grayscale, Spot, and images stay unchanged.",
            L"PikDraw ICC", MB_OK | MB_ICONINFORMATION);
        return;
    }

    const auto path = choose_profile(hwnd_, false, vi, true);
    if (!path) return;
    const auto destination = load_process_profile(*path, error);
    if (!destination) {
        const auto message = utf8_to_wide((vi ? "Profile đích không hợp lệ. " : "Invalid destination profile. ") + error);
        MessageBoxW(hwnd_, message.c_str(), L"PikDraw ICC", MB_OK | MB_ICONWARNING);
        return;
    }
    const auto intent = choose_conversion_intent(hwnd_, vi);
    if (!intent) return;

    bool needs_transform = false;
    std::vector<std::shared_ptr<const color::ProfileColorTransform>> transforms;
    transforms.reserve(sources.size());
    for (const auto& source : sources) {
        if (source->identity() == destination->identity()) continue;
        needs_transform = true;
        const auto transform = color::ProfileColorTransform::create(
            source, destination, {*intent, false});
        if (!transform) {
            const auto message = utf8_to_wide(transform.diagnostic);
            MessageBoxW(hwnd_, message.c_str(), L"PikDraw ICC", MB_OK | MB_ICONWARNING);
            return;
        }
        transforms.push_back(transform.transform);
    }
    if (!needs_transform) {
        status_msg_ = vi ? "Selection đã gắn ICC đích · tài liệu không đổi"
                         : "Selection already uses the destination ICC profile · no changes made";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    std::wstring prompt = vi
        ? L"Chuyển các màu process được chọn sang ICC đích?\n\n"
        : L"Convert selected process paints to the destination ICC profile?\n\n";
    prompt += vi ? L"Số nguồn ICC: " : L"Source ICC profiles: ";
    prompt += std::to_wstring(transforms.size());
    prompt += L"\n" + std::wstring(vi ? L"Đích: " : L"Destination: ") +
        utf8_to_wide(destination->identity().name);
    prompt += L"\nRendering intent: " + std::wstring(intent_name(*intent));
    prompt += vi
        ? L"\n\nChỉ selection thay đổi; mỗi màu chuyển được gắn ICC đích. Trang và màu nguồn khác giữ nguyên. Một Undo hoàn tác toàn bộ. Bù điểm đen chưa khả dụng."
        : L"\n\nOnly the selection changes; converted paints receive the destination binding. The page and other source paints stay unchanged. One Undo restores the whole operation. Black point compensation is unavailable.";
    if (MessageBoxW(hwnd_, prompt.c_str(), L"PikDraw ICC", MB_OKCANCEL | MB_ICONWARNING) != IDOK)
        return;

    if (!workflow().convert_selected_objects_color_profile(
            snapshot->active_page_id(), destination, std::move(transforms))) {
        const auto detail = workflow().last_color_conversion_error();
        const auto message = detail.empty()
            ? (vi ? L"Selection đã thay đổi; chuyển đổi bị từ chối và tài liệu không đổi."
                  : L"The selection changed; conversion was rejected and the document is unchanged.")
            : utf8_to_wide(detail);
        MessageBoxW(hwnd_, message.c_str(), L"PikDraw ICC", MB_OK | MB_ICONWARNING);
        return;
    }
    (void)color::ProfileRegistry::instance().register_resource(destination);
    update_color_preview_options();
    const auto converted_count = workflow().last_color_conversion_summary()
        ? workflow().last_color_conversion_summary()->icc_managed : 0;
    status_msg_ = vi
        ? "Đã chuyển " + std::to_string(converted_count) + " màu process · " + intent_status_name(*intent) + " · Ctrl+Z hoàn tác"
        : "Converted " + std::to_string(converted_count) + " selected process paints · " + intent_status_name(*intent) + " · Ctrl+Z to undo";
    InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::convert_active_page_color_profile() {
#ifdef _WIN32
    const bool vi = language_ == UILanguage::Vietnamese;
    if (root().document().is_content_locked()) return;
    if (active_num_field_ != NumericFieldId::None) {
        commit_numeric_field();
        if (active_num_field_ != NumericFieldId::None) return;
    }
    if (is_inline_text_editing_) {
        commit_inline_text_editing();
        if (is_inline_text_editing_) return;
    }
    cancel_canvas_interaction();

    const auto snapshot = root().document().current_snapshot();
    if (!snapshot) return;
    const auto* page = snapshot->find_page(snapshot->active_page_id());
    if (!page) return;
    if (!page->layout.color_profile_resource) {
        const auto message = vi
            ? L"Trang chưa gắn ICC nguồn đã resolve. Dùng Assign ICC để gắn profile trước khi chuyển màu."
            : L"The page has no resolved source ICC profile. Assign an ICC profile before converting colors.";
        MessageBoxW(hwnd_, message, L"PikDraw ICC", MB_OK | MB_ICONWARNING);
        return;
    }
    const auto page_id = page->id;
    const auto source_profile = page->layout.color_profile_resource;
    const auto path = choose_profile(hwnd_, false, vi, true);
    if (!path) return;
    std::string error;
    const auto destination_profile = load_process_profile(*path, error);
    if (!destination_profile) {
        const auto message = utf8_to_wide((vi ? "Profile đích không hợp lệ. " : "Invalid destination profile. ") + error);
        MessageBoxW(hwnd_, message.c_str(), L"PikDraw ICC", MB_OK | MB_ICONWARNING);
        return;
    }
    const auto intent = choose_conversion_intent(hwnd_, vi);
    if (!intent) return;
    const auto transform = color::ProfileColorTransform::create(
        source_profile, destination_profile, {*intent, false});
    if (!transform) {
        const auto message = utf8_to_wide(transform.diagnostic);
        MessageBoxW(hwnd_, message.c_str(), L"PikDraw ICC", MB_OK | MB_ICONWARNING);
        return;
    }

    std::wstring prompt = vi
        ? L"Chuyển RGB/CMYK process paints của trang và gắn profile đích?\n\n"
        : L"Convert the page's RGB/CMYK process paints and bind the destination profile?\n\n";
    prompt += (vi ? L"Nguồn: " : L"Source: ") + utf8_to_wide(source_profile->identity().name);
    prompt += L"\n" + std::wstring(vi ? L"Đích: " : L"Destination: ") +
        utf8_to_wide(destination_profile->identity().name);
    prompt += L"\n" + std::wstring(vi ? L"Rendering intent: " : L"Rendering intent: ") + intent_name(*intent);
    prompt += vi
        ? L"\n\nChannels sẽ thay đổi. Một Undo hoàn tác toàn bộ. Spot, grayscale và pixels ảnh giữ nguyên. Bù điểm đen chưa khả dụng."
        : L"\n\nChannels will change. One Undo restores the whole page. Spot, grayscale and raster pixels stay unchanged. Black point compensation is unavailable.";
    if (MessageBoxW(hwnd_, prompt.c_str(), L"PikDraw ICC", MB_OKCANCEL | MB_ICONWARNING) != IDOK)
        return;

    if (!workflow().convert_page_color_profile(page_id, transform.transform)) {
        const auto detail = workflow().last_color_conversion_error();
        const auto message = detail.empty()
            ? (vi ? L"Trang hoặc màu process đã thay đổi; chuyển đổi bị từ chối và tài liệu không đổi."
                  : L"The page or process paints changed; conversion was rejected and the document is unchanged.")
            : utf8_to_wide(detail);
        MessageBoxW(hwnd_, message.c_str(), L"PikDraw ICC", MB_OK | MB_ICONWARNING);
        return;
    }
    update_color_preview_options();
    const auto converted_count = workflow().last_color_conversion_summary()
        ? workflow().last_color_conversion_summary()->icc_managed : 0;
    status_msg_ = vi
        ? "Đã chuyển " + std::to_string(converted_count) + " màu process · " + intent_status_name(*intent) + " · Ctrl+Z hoàn tác"
        : "Converted " + std::to_string(converted_count) + " process paints · " + intent_status_name(*intent) + " · Ctrl+Z to undo";
    InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}
void NativeWindow::convert_document_color_profile() {
#ifdef _WIN32
    const bool vi = language_ == UILanguage::Vietnamese;
    if (root().document().is_content_locked()) return;
    if (active_num_field_ != NumericFieldId::None) {
        commit_numeric_field();
        if (active_num_field_ != NumericFieldId::None) return;
    }
    if (is_inline_text_editing_) {
        commit_inline_text_editing();
        if (is_inline_text_editing_) return;
    }
    cancel_canvas_interaction();

    const auto snapshot = root().document().current_snapshot();
    if (!snapshot || snapshot->pages().empty()) return;
    if (std::any_of(snapshot->pages().begin(), snapshot->pages().end(), [](const auto& page) {
            return !page.layout.color_profile_resource;
        })) {
        MessageBoxW(hwnd_, vi
            ? L"Mọi trang cần có profile ICC nguồn đã resolve. Gán ICC cho từng trang trước khi chuyển toàn tài liệu."
            : L"Every page needs a resolved source ICC profile. Assign ICC to each page before converting the document.",
            L"PikDraw ICC", MB_OK | MB_ICONWARNING);
        return;
    }

    const auto path = choose_profile(hwnd_, false, vi, true);
    if (!path) return;
    std::string error;
    const auto destination = load_process_profile(*path, error);
    if (!destination) {
        const auto message = utf8_to_wide((vi ? "Profile đích không hợp lệ. " : "Invalid destination profile. ") + error);
        MessageBoxW(hwnd_, message.c_str(), L"PikDraw ICC", MB_OK | MB_ICONWARNING);
        return;
    }
    const auto intent = choose_conversion_intent(hwnd_, vi);
    if (!intent) return;

    bool all_pages_already_bound = true;
    std::vector<std::shared_ptr<const color::ProfileColorTransform>> transforms;
    for (const auto& page : snapshot->pages()) {
        const auto& source = page.layout.color_profile_resource;
        if (source->identity() == destination->identity() &&
            page.layout.color_profile_name == destination->identity().name &&
            page.layout.color_mode_explicit)
            continue;
        all_pages_already_bound = false;
        if (source->identity() == destination->identity()) continue;
        const auto existing = std::find_if(transforms.begin(), transforms.end(), [&](const auto& item) {
            return item->source_profile().identity() == source->identity();
        });
        if (existing != transforms.end()) continue;
        const auto transform = color::ProfileColorTransform::create(
            source, destination, {*intent, false});
        if (!transform) {
            const auto message = utf8_to_wide(transform.diagnostic);
            MessageBoxW(hwnd_, message.c_str(), L"PikDraw ICC", MB_OK | MB_ICONWARNING);
            return;
        }
        transforms.push_back(transform.transform);
    }
    if (all_pages_already_bound) {
        status_msg_ = vi ? "Mọi trang đã dùng profile ICC đích · tài liệu không đổi"
                         : "Every page already uses the destination ICC profile · no changes made";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    std::wstring prompt = vi
        ? L"Chuyển RGB/CMYK process paints trên toàn bộ trang sang profile đích?\n\n"
        : L"Convert RGB/CMYK process paints on every page to the destination profile?\n\n";
    prompt += (vi ? L"Số trang: " : L"Pages: ") + std::to_wstring(snapshot->page_count());
    prompt += L"\n" + std::wstring(vi ? L"Đích: " : L"Destination: ") +
        utf8_to_wide(destination->identity().name);
    prompt += L"\nRendering intent: " + std::wstring(intent_name(*intent));
    prompt += vi
        ? L"\n\nMỗi trang dùng ICC nguồn riêng; một Undo hoàn tác toàn bộ. Spot, grayscale và pixels ảnh giữ nguyên. Bù điểm đen chưa khả dụng."
        : L"\n\nEach page uses its own source ICC; one Undo restores the whole document. Spot, grayscale and raster pixels remain unchanged. Black point compensation is unavailable.";
    if (MessageBoxW(hwnd_, prompt.c_str(), L"PikDraw ICC", MB_OKCANCEL | MB_ICONWARNING) != IDOK)
        return;

    if (!workflow().convert_document_color_profile(destination, std::move(transforms))) {
        const auto detail = workflow().last_color_conversion_error();
        const auto message = detail.empty()
            ? (vi ? L"Trang hoặc màu process đã thay đổi; chuyển đổi bị từ chối và tài liệu không đổi."
                  : L"A page or process paint changed; conversion was rejected and the document is unchanged.")
            : utf8_to_wide(detail);
        MessageBoxW(hwnd_, message.c_str(), L"PikDraw ICC", MB_OK | MB_ICONWARNING);
        return;
    }
    update_color_preview_options();
    const auto converted_count = workflow().last_color_conversion_summary()
        ? workflow().last_color_conversion_summary()->icc_managed : 0;
    status_msg_ = vi
        ? "Đã chuyển profile toàn tài liệu · " + std::to_string(converted_count) +
            " màu process · " + intent_status_name(*intent) + " · Ctrl+Z hoàn tác"
        : "Converted document ICC profile · " + std::to_string(converted_count) +
            " process paints · " + intent_status_name(*intent) + " · Ctrl+Z to undo";
    InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}
} // namespace pikdraw::desktop
