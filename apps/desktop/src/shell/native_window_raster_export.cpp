#include "pikdraw/desktop/shell/native_window.hpp"

#include "pikdraw/color/preview/icc_preview.hpp"
#include "pikdraw/color/preview/profile_color_transform.hpp"
#include "pikdraw/color/profiles/profile_resource.hpp"
#include "pikdraw/desktop/shell/raster_bitmap_export.hpp"
#include "pikdraw/interop/writers/raster_image_writer.hpp"
#include "pikdraw/raster/edit_geometry.hpp"
#include "pikdraw/raster/evaluator.hpp"
#include "pikdraw/rendering/software_rasterizer.hpp"
#include "pikdraw/workers/job_object.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <exception>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <commdlg.h>
#include <windowsx.h>
#endif

namespace pikdraw::desktop {

#ifdef _WIN32
namespace {
constexpr int kFormatId = 101;
constexpr int kScopeId = 102;
constexpr int kDpiId = 103;
constexpr int kQualityId = 104;
constexpr int kProfileLabelId = 105;
constexpr int kBrowseProfileId = 106;
constexpr int kEmbedProfileId = 107;
constexpr int kHelpId = 108;
constexpr int kQualityLabelId = 204;

struct RasterExportSettings {
    interop::RasterImageOutputFormat format{interop::RasterImageOutputFormat::Tiff};
    uint32_t dpi{300};
    float jpeg_quality{1.0f};
    bool all_pages{false};
    bool active_artboard{false};
    bool embed_profile{true};
    std::shared_ptr<const color::ProfileResource> output_profile;
};

struct RasterExportDialogState {
    bool vietnamese{false};
    bool accepted{false};
    bool has_active_artboard{false};
    uint32_t default_dpi{300};
    std::shared_ptr<const color::ProfileResource> cmyk_preview_profile;
    RasterExportSettings settings;
};

HWND add_dialog_control(HWND dialog, const wchar_t* type, const std::wstring& text,
                        DWORD style, int x, int y, int width, int height,
                        int id, HFONT font) {
    const HWND control = CreateWindowExW(0, type, text.c_str(),
        WS_CHILD | WS_VISIBLE | style, x, y, width, height, dialog,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    if (control && font)
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return control;
}

void add_combo_item(HWND dialog, int id, const std::wstring& label, LPARAM value) {
    const auto combo = GetDlgItem(dialog, id);
    const auto row = SendMessageW(combo, CB_ADDSTRING, 0,
                                  reinterpret_cast<LPARAM>(label.c_str()));
    SendMessageW(combo, CB_SETITEMDATA, row, value);
}

void set_combo_value(HWND dialog, int id, LPARAM value) {
    const auto combo = GetDlgItem(dialog, id);
    for (LRESULT row = 0; row < SendMessageW(combo, CB_GETCOUNT, 0, 0); ++row) {
        if (SendMessageW(combo, CB_GETITEMDATA, row, 0) == value) {
            SendMessageW(combo, CB_SETCURSEL, row, 0);
            return;
        }
    }
}

void refresh_quality_enabled(HWND dialog) {
    const auto selected = SendDlgItemMessageW(dialog, kFormatId, CB_GETCURSEL, 0, 0);
    const bool jpeg = selected != CB_ERR &&
        SendDlgItemMessageW(dialog, kFormatId, CB_GETITEMDATA, selected, 0) == 1;
    EnableWindow(GetDlgItem(dialog, kQualityId), jpeg);
    EnableWindow(GetDlgItem(dialog, kQualityLabelId), jpeg);
}

bool choose_rgb_profile(HWND dialog, RasterExportDialogState& state) {
    std::vector<wchar_t> path(32768, L'\0');
    OPENFILENAMEW picker{};
    picker.lStructSize = sizeof(picker);
    picker.hwndOwner = dialog;
    picker.lpstrFilter = L"ICC RGB profiles (*.icc;*.icm)\0*.icc;*.icm\0All files (*.*)\0*.*\0\0";
    picker.lpstrTitle = state.vietnamese
        ? L"Chọn profile ICC RGB đầu ra" : L"Choose output RGB ICC profile";
    picker.lpstrFile = path.data();
    picker.nMaxFile = static_cast<DWORD>(path.size());
    picker.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&picker)) return false;

    auto loaded = color::ProfileResource::load_file(path.data(), color::ColorSpace::Rgb);
    if (!loaded || loaded.resource->bytes().size() > 1024u * 1024u) {
        MessageBoxW(dialog,
            state.vietnamese
                ? L"Profile phải hợp lệ, dùng không gian RGB và không quá 1 MiB."
                : L"The profile must be valid RGB and no larger than 1 MiB.",
            state.vietnamese ? L"Profile ICC không dùng được" : L"Unsupported ICC profile",
            MB_OK | MB_ICONWARNING);
        return false;
    }
    std::string preview_error;
    if (!color::IccPreview::create_from_resources(
            loaded.resource, state.cmyk_preview_profile, &preview_error)) {
        MessageBoxW(dialog,
            state.vietnamese
                ? L"Windows không tạo được phép chuyển màu với profile này. Chọn profile RGB khác."
                : L"Windows could not create a color transform for this profile. Choose another RGB profile.",
            state.vietnamese ? L"Profile ICC không dùng được" : L"ICC profile unavailable",
            MB_OK | MB_ICONWARNING);
        return false;
    }
    state.settings.output_profile = std::move(loaded.resource);
    const auto display_name = NativeWindow::utf8_to_wide(
        state.settings.output_profile->identity().name);
    SetDlgItemTextW(dialog, kProfileLabelId, display_name.c_str());
    return true;
}

void populate_raster_export_dialog(HWND dialog, RasterExportDialogState& state) {
    SetWindowTextW(dialog, state.vietnamese
        ? L"Xuất Raster" : L"Export Raster");
    const HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    const auto label = [&](int id, const wchar_t* vi, const wchar_t* en,
                           int y, int width = 104) {
        add_dialog_control(dialog, L"STATIC", state.vietnamese ? vi : en,
            SS_LEFT | WS_GROUP, 16, y, width, 18, id, font);
    };
    label(201, L"Định dạng", L"Format", 14);
    label(202, L"Phạm vi", L"Scope", 44);
    label(203, L"Độ phân giải", L"Resolution", 74);
    label(kQualityLabelId, L"Chất lượng JPEG", L"JPEG quality", 104, 112);
    label(205, L"Profile đầu ra", L"Output profile", 134, 112);

    add_dialog_control(dialog, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
        132, 10, 230, 120, kFormatId, font);
    add_dialog_control(dialog, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
        132, 40, 230, 90, kScopeId, font);
    add_dialog_control(dialog, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
        132, 70, 230, 110, kDpiId, font);
    add_dialog_control(dialog, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
        132, 100, 230, 90, kQualityId, font);
    add_dialog_control(dialog, L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS,
        132, 132, 154, 20, kProfileLabelId, font);
    add_dialog_control(dialog, L"BUTTON", state.vietnamese ? L"Chọn…" : L"Browse…",
        BS_PUSHBUTTON | WS_TABSTOP, 290, 129, 72, 24, kBrowseProfileId, font);
    add_dialog_control(dialog, L"BUTTON",
        state.vietnamese ? L"Nhúng profile ICC đầu ra" : L"Embed output ICC profile",
        BS_AUTOCHECKBOX | WS_TABSTOP, 132, 158, 230, 20, kEmbedProfileId, font);
    add_dialog_control(dialog, L"STATIC", state.vietnamese
        ? L"Phạm vi trang hoặc Artboard trên nền trắng ở RGB. Màu chuyển sang profile đầu ra; artwork không đổi."
        : L"Export the active page or Artboard on white in RGB. Colors convert to the output profile; artwork stays unchanged.",
        SS_LEFT | SS_NOPREFIX, 16, 184, 346, 38, kHelpId, font);
    add_dialog_control(dialog, L"BUTTON", state.vietnamese ? L"Xuất…" : L"Export…",
        BS_DEFPUSHBUTTON | WS_TABSTOP, 198, 232, 78, 25, IDOK, font);
    add_dialog_control(dialog, L"BUTTON", state.vietnamese ? L"Hủy" : L"Cancel",
        BS_PUSHBUTTON | WS_TABSTOP, 284, 232, 78, 25, IDCANCEL, font);

    add_combo_item(dialog, kFormatId, state.vietnamese ? L"TIFF · không mất dữ liệu" : L"TIFF · lossless", 0);
    add_combo_item(dialog, kFormatId, state.vietnamese ? L"JPEG · nén mất dữ liệu" : L"JPEG · lossy", 1);
    add_combo_item(dialog, kScopeId, state.vietnamese ? L"Trang hiện tại" : L"Active page", 0);
    add_combo_item(dialog, kScopeId, state.vietnamese ? L"Tất cả trang · mỗi trang một tệp" : L"All pages · one file per page", 1);
    if (state.has_active_artboard)
        add_combo_item(dialog, kScopeId,
            state.vietnamese ? L"Artboard đang chọn" : L"Active Artboard", 2);
    for (const uint32_t dpi : {72u, 150u, 300u, 600u}) {
        const auto item = std::to_wstring(dpi) + L" DPI";
        add_combo_item(dialog, kDpiId, item, static_cast<LPARAM>(dpi));
    }
    for (const int quality : {80, 90, 100}) {
        const auto item = std::to_wstring(quality) + L"%";
        add_combo_item(dialog, kQualityId, item, static_cast<LPARAM>(quality));
    }

    set_combo_value(dialog, kFormatId, 0);
    set_combo_value(dialog, kScopeId, 0);
    set_combo_value(dialog, kDpiId,
        state.default_dpi == 72 || state.default_dpi == 150 ||
            state.default_dpi == 300 || state.default_dpi == 600
            ? state.default_dpi : 300);
    set_combo_value(dialog, kQualityId, 100);
    SendDlgItemMessageW(dialog, kEmbedProfileId, BM_SETCHECK, BST_CHECKED, 0);
    const auto name = NativeWindow::utf8_to_wide(state.settings.output_profile->identity().name);
    SetDlgItemTextW(dialog, kProfileLabelId, name.c_str());
    refresh_quality_enabled(dialog);
    SetFocus(GetDlgItem(dialog, kFormatId));
}

INT_PTR CALLBACK raster_export_dialog_proc(HWND dialog, UINT message,
                                           WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<RasterExportDialogState*>(
        GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<RasterExportDialogState*>(lparam);
        SetWindowLongPtrW(dialog, DWLP_USER, lparam);
        populate_raster_export_dialog(dialog, *state);
        return FALSE;
    }
    if (!state) return FALSE;
    if (message == WM_COMMAND) {
        const int id = LOWORD(wparam);
        if (id == kFormatId && HIWORD(wparam) == CBN_SELCHANGE) {
            refresh_quality_enabled(dialog);
            return TRUE;
        }
        if (id == kBrowseProfileId && HIWORD(wparam) == BN_CLICKED) {
            (void)choose_rgb_profile(dialog, *state);
            return TRUE;
        }
        if (id == IDOK) {
            const auto selected = [&](int control) -> LRESULT {
                const auto row = SendDlgItemMessageW(dialog, control, CB_GETCURSEL, 0, 0);
                return row == CB_ERR ? CB_ERR
                    : SendDlgItemMessageW(dialog, control, CB_GETITEMDATA, row, 0);
            };
            const auto format = selected(kFormatId);
            const auto scope = selected(kScopeId);
            const auto dpi = selected(kDpiId);
            const auto quality = selected(kQualityId);
            if (format == CB_ERR || scope == CB_ERR || dpi == CB_ERR || quality == CB_ERR ||
                !state->settings.output_profile) {
                MessageBoxW(dialog,
                    state->vietnamese ? L"Chọn đầy đủ định dạng, phạm vi, DPI và profile RGB."
                                      : L"Choose a format, scope, resolution, and RGB profile.",
                    state->vietnamese ? L"Thiếu tùy chọn xuất" : L"Export options missing",
                    MB_OK | MB_ICONWARNING);
                return TRUE;
            }
            state->settings.format = format == 0
                ? interop::RasterImageOutputFormat::Tiff
                : interop::RasterImageOutputFormat::Jpeg;
            state->settings.all_pages = scope == 1;
            state->settings.active_artboard = scope == 2;
            state->settings.dpi = static_cast<uint32_t>(dpi);
            state->settings.jpeg_quality = static_cast<float>(quality) / 100.0f;
            state->settings.embed_profile =
                SendDlgItemMessageW(dialog, kEmbedProfileId, BM_GETCHECK, 0, 0) == BST_CHECKED;
            state->accepted = true;
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        if (id == IDCANCEL) {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
    }
    if (message == WM_CLOSE) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

bool same_windows_path(const std::wstring& left, const std::wstring& right) {
    std::error_code left_error, right_error;
    auto a = std::filesystem::absolute(left, left_error).lexically_normal().wstring();
    auto b = std::filesystem::absolute(right, right_error).lexically_normal().wstring();
    if (left_error || right_error) {
        a = left;
        b = right;
    }
    return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}

std::wstring raster_output_dialog(bool vietnamese,
                                  interop::RasterImageOutputFormat format,
                                  bool all_pages, HWND owner,
                                  bool bitmap_master = false) {
    std::vector<wchar_t> path(32768, L'\0');
    OPENFILENAMEW picker{};
    picker.lStructSize = sizeof(picker);
    picker.hwndOwner = owner;
    picker.lpstrFilter = format == interop::RasterImageOutputFormat::Tiff
        ? L"TIFF image (*.tif;*.tiff)\0*.tif;*.tiff\0\0"
        : L"JPEG image (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0\0";
    picker.lpstrTitle = bitmap_master
        ? (vietnamese ? L"Xuất bản gốc ảnh đã chọn thành TIFF" : L"Export selected bitmap master as TIFF")
        : all_pages
            ? (vietnamese ? L"Chọn tên gốc cho các trang Raster" : L"Choose the base name for raster pages")
            : (vietnamese ? L"Xuất trang thành ảnh Raster" : L"Export page as raster image");
    picker.lpstrFile = path.data();
    picker.nMaxFile = static_cast<DWORD>(path.size());
    picker.lpstrDefExt = format == interop::RasterImageOutputFormat::Tiff ? L"tif" : L"jpg";
    picker.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
        (all_pages ? 0 : OFN_OVERWRITEPROMPT);
    if (!GetSaveFileNameW(&picker)) return {};
    return path.data();
}

std::shared_ptr<const color::ProfileResource> choose_raster_cmyk_profile(
    HWND owner, bool vietnamese) {
    std::vector<wchar_t> path(32768, L'\0');
    OPENFILENAMEW picker{};
    picker.lStructSize = sizeof(picker);
    picker.hwndOwner = owner;
    picker.lpstrFilter = L"CMYK ICC profiles (*.icc;*.icm)\0*.icc;*.icm\0All files (*.*)\0*.*\0\0";
    picker.lpstrTitle = vietnamese
        ? L"Chọn profile ICC CMYK đích" : L"Choose destination CMYK ICC profile";
    picker.lpstrFile = path.data();
    picker.nMaxFile = static_cast<DWORD>(path.size());
    picker.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&picker)) return {};
    auto loaded = color::ProfileResource::load_file(path.data(), color::ColorSpace::Cmyk);
    if (!loaded || loaded.resource->bytes().size() > 1024u * 1024u) {
        MessageBoxW(owner, vietnamese
            ? L"Profile phải hợp lệ, có hệ CMYK và không quá 1 MiB."
            : L"The profile must be valid CMYK and no larger than 1 MiB.",
            vietnamese ? L"Profile ICC không dùng được" : L"Unsupported ICC profile",
            MB_OK | MB_ICONWARNING);
        return {};
    }
    return std::move(loaded.resource);
}

std::optional<color::IccRenderingIntent> choose_raster_cmyk_intent(
    HWND owner, bool vietnamese) {
    const auto menu = CreatePopupMenu();
    if (!menu) return std::nullopt;
    AppendMenuW(menu, MF_STRING, 1, vietnamese ? L"Cảm nhận (Perceptual)" : L"Perceptual");
    AppendMenuW(menu, MF_STRING | MF_CHECKED, 2,
        vietnamese ? L"Tương đối đo màu (Relative colorimetric)" : L"Relative colorimetric");
    AppendMenuW(menu, MF_STRING, 3, vietnamese ? L"Bão hòa (Saturation)" : L"Saturation");
    AppendMenuW(menu, MF_STRING, 4,
        vietnamese ? L"Tuyệt đối đo màu (Absolute colorimetric)" : L"Absolute colorimetric");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 5,
        vietnamese ? L"Bù điểm đen · Windows CMM chưa hỗ trợ"
                   : L"Black point compensation · unavailable in Windows CMM");
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

std::wstring raster_error_to_wide(const std::string& error) {
    return NativeWindow::utf8_to_wide(error);
}

} // namespace

struct RasterExportJobSession {
    std::jthread worker;
    std::atomic<bool> finished{false};
    std::atomic<bool> cancel_requested{false};
    std::atomic<bool> waiting_for_memory{false};
    std::atomic<size_t> completed_files{0};
    size_t total_files{0};
    bool success{false};
    bool cancelled{false};
    std::mutex result_mutex;
    std::string error;
};

bool NativeWindow::raster_export_job_active() const noexcept {
    return raster_export_job_ &&
        !raster_export_job_->finished.load(std::memory_order_acquire);
}

bool NativeWindow::can_export_selected_bitmap_master() const {
    if (export_job_active() || raster_export_job_active() ||
        raster_profile_conversion_active()) return false;
    const auto& selection = root().vector_controller().selection().selected_ids();
    if (selection.size() != 1) return false;
    const auto snapshot = root().document().current_snapshot();
    if (!snapshot) return false;
    const auto* object = snapshot->objects().lookup_ref(selection.front());
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image || image->width_px <= 0 || image->height_px <= 0) return false;
    if (image->source.preview_pixel_color_basis ==
        document::RasterPixelColorBasis::LegacyUnknown) return false;
    if (image->source.preview_pixel_color_basis ==
            document::RasterPixelColorBasis::EmbeddedProfileUnapplied &&
        image->source.icc_profile.empty()) return false;
    const uint64_t pixel_count = static_cast<uint64_t>(image->width_px) *
                                 static_cast<uint64_t>(image->height_px);
    const auto pixels16 = image->rgba16le_pixels();
    const auto bytes_per_pixel = pixels16.empty() ? 4u : 8u;
    if (pixel_count > SIZE_MAX / bytes_per_pixel ||
        image->rgba_pixels().size() != pixel_count ||
        (!pixels16.empty() && pixels16.size() != pixel_count * bytes_per_pixel))
        return false;
    return true;
}

bool NativeWindow::can_export_selected_bitmap_cmyk_master() const {
    if (!can_export_selected_bitmap_master()) return false;
    const auto& selection = root().vector_controller().selection().selected_ids();
    const auto snapshot = root().document().current_snapshot();
    if (selection.size() != 1 || !snapshot) return false;
    const auto* object = snapshot->objects().lookup_ref(selection.front());
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return false;
    if (image->source.preview_pixel_color_basis ==
        document::RasterPixelColorBasis::EmbeddedProfileUnapplied) {
        const auto& profile = image->source.icc_profile;
        return profile.size() >= 128 && profile.size() <= 1024u * 1024u &&
            profile[16] == 'R' && profile[17] == 'G' &&
            profile[18] == 'B' && profile[19] == ' ';
    }
    return image->source.preview_pixel_color_basis ==
            document::RasterPixelColorBasis::AssumedSRgb ||
        image->source.preview_pixel_color_basis ==
            document::RasterPixelColorBasis::ConvertedToSRgb;
}

namespace {
bool evaluate_raster_edits_for_export(
    document::DocumentPage& page,
    const std::function<bool(uint64_t)>& wait_for_memory_headroom,
    const std::function<bool()>& cancelled,
    std::string& error) {
    constexpr uint64_t scratch_reserve = 16ULL * 1024ULL * 1024ULL;
    for (const auto& [object_id, object] : page.objects.all()) {
        const auto* image = std::get_if<document::ImageObject>(&object);
        if (!image || image->edit_graph.nodes.empty() ||
            std::none_of(image->edit_graph.nodes.begin(), image->edit_graph.nodes.end(),
                [](const document::RasterEditNode& node) { return node.enabled; }))
            continue;
        if (raster::raster_edit_requires_srgb_processing(image->edit_graph) &&
            (image->source.preview_pixel_color_basis ==
                document::RasterPixelColorBasis::EmbeddedProfileUnapplied ||
            image->source.preview_pixel_color_basis ==
                document::RasterPixelColorBasis::LegacyUnknown)) {
            error = "This image's pixel profile has not been resolved; apply or convert its source profile before export.";
            return false;
        }
        if (image->width_px <= 0 || image->height_px <= 0) {
            error = "An edited image has invalid pixel dimensions.";
            return false;
        }
        const auto pixels = image->rgba_pixels();
        const auto pixels16 = image->rgba16le_pixels();
        const uint64_t pixel_count = static_cast<uint64_t>(image->width_px) *
                                     static_cast<uint64_t>(image->height_px);
        const bool high_precision = !pixels16.empty();
        const uint64_t bytes_per_pixel = high_precision ? 8u : 4u;
        if (pixel_count != pixels.size() ||
            pixel_count > UINT64_MAX / bytes_per_pixel ||
            pixel_count > SIZE_MAX / bytes_per_pixel ||
            (high_precision && pixels16.size() != pixel_count * bytes_per_pixel)) {
            error = "An edited image's dimensions do not match its pixel buffer.";
            return false;
        }
        const auto source_bytes = static_cast<size_t>(pixel_count * bytes_per_pixel);
        if (cancelled && cancelled()) {
            error = "Raster export was cancelled.";
            return false;
        }
        if (!wait_for_memory_headroom ||
            !wait_for_memory_headroom(static_cast<uint64_t>(source_bytes) + scratch_reserve)) {
            error = "Raster edit evaluation was cancelled or timed out waiting for system memory.";
            return false;
        }

        try {
            std::vector<uint8_t> rgba8;
            std::span<const uint8_t> source_samples;
            if (high_precision) {
                source_samples = pixels16;
            } else {
                rgba8.resize(source_bytes);
                for (size_t index = 0; index < pixels.size(); ++index) {
                    const uint32_t packed = pixels[index];
                    const size_t offset = index * 4;
                    rgba8[offset] = static_cast<uint8_t>(packed >> 24);
                    rgba8[offset + 1] = static_cast<uint8_t>(packed >> 16);
                    rgba8[offset + 2] = static_cast<uint8_t>(packed >> 8);
                    rgba8[offset + 3] = static_cast<uint8_t>(packed);
                }
                source_samples = rgba8;
            }

            std::unique_ptr<raster::RasterMemoryBudget> budget;
            budget = std::make_unique<raster::RasterMemoryBudget>(
                std::numeric_limits<uint64_t>::max(),
                [&](uint64_t requested_bytes) {
                    const uint64_t reserved = budget ? budget->reserved_bytes() : 0;
                    if (requested_bytes > UINT64_MAX - reserved ||
                        requested_bytes + reserved > UINT64_MAX - scratch_reserve)
                        return false;
                    return wait_for_memory_headroom(
                        requested_bytes + reserved + scratch_reserve);
                });
            raster::RasterEvaluationOptions options;
            options.cancelled = cancelled;
            const raster::RasterPixelView view{
                source_samples, static_cast<uint32_t>(image->width_px),
                static_cast<uint32_t>(image->height_px),
                static_cast<uint64_t>(image->width_px) * bytes_per_pixel,
                high_precision
                    ? raster::RasterPixelEncoding::SRgbRgba16LEStraightAlpha
                    : raster::RasterPixelEncoding::SRgbRgba8StraightAlpha};
            auto evaluated = raster::RasterEditEvaluator::evaluate(
                view, image->edit_graph, *budget, options);
            if (!evaluated.succeeded()) {
                error = evaluated.diagnostic.empty()
                    ? "The image edit graph could not be evaluated."
                    : evaluated.diagnostic;
                return false;
            }
            if (evaluated.image.width > static_cast<uint32_t>(
                    std::numeric_limits<int32_t>::max()) ||
                evaluated.image.height > static_cast<uint32_t>(
                    std::numeric_limits<int32_t>::max())) {
                error = "Edited image dimensions exceed the document's supported range.";
                return false;
            }
            const uint64_t clone_pixel_bytes = image->shared_pixels_rgba
                ? 0u : static_cast<uint64_t>(pixels.size()) * 4u;
            const uint64_t clone_source_bytes = image->source.shared_original_bytes
                ? 0u : static_cast<uint64_t>(image->source.original_data().size());
            const uint64_t packed_bytes =
                static_cast<uint64_t>(evaluated.image.bytes.size());
            const uint64_t preview_bytes = static_cast<uint64_t>(evaluated.image.width) *
                evaluated.image.height * 4u;
            constexpr uint64_t maximum = std::numeric_limits<uint64_t>::max();
            if (clone_pixel_bytes > maximum - clone_source_bytes ||
                clone_pixel_bytes + clone_source_bytes > maximum - scratch_reserve ||
                packed_bytes > maximum - preview_bytes ||
                packed_bytes + preview_bytes > maximum - clone_pixel_bytes -
                    clone_source_bytes - scratch_reserve ||
                !wait_for_memory_headroom(packed_bytes + preview_bytes + clone_pixel_bytes +
                    clone_source_bytes + scratch_reserve)) {
                error = "Raster export was cancelled or timed out while reserving edited image pixels.";
                return false;
            }
            const size_t output_pixels = evaluated.image.bytes.size() /
                (high_precision ? 8u : 4u);
            auto packed_pixels = std::make_shared<std::vector<uint32_t>>(output_pixels);
            for (size_t index = 0; index < packed_pixels->size(); ++index) {
                const size_t offset = index * (high_precision ? 8u : 4u);
                (*packed_pixels)[index] =
                    (static_cast<uint32_t>(evaluated.image.bytes[offset + (high_precision ? 1 : 0)]) << 24) |
                    (static_cast<uint32_t>(evaluated.image.bytes[offset + (high_precision ? 3 : 1)]) << 16) |
                    (static_cast<uint32_t>(evaluated.image.bytes[offset + (high_precision ? 5 : 2)]) << 8) |
                    static_cast<uint32_t>(evaluated.image.bytes[offset + (high_precision ? 7 : 3)]);
            }
            auto updated = *image;
            updated.width_px = static_cast<int32_t>(evaluated.image.width);
            updated.height_px = static_cast<int32_t>(evaluated.image.height);
            updated.bounds_rect = raster::raster_edited_bounds(
                *image, evaluated.image.width, evaluated.image.height);
            updated.pixels_rgba.clear();
            updated.shared_pixels_rgba = std::move(packed_pixels);
            if (high_precision) {
                updated.shared_pixels_rgba16le =
                    std::make_shared<const std::vector<uint8_t>>(
                        std::move(evaluated.image.bytes));
            }
            page.objects = page.objects.set(object_id,
                document::ObjectVariant{std::move(updated)});
        } catch (const std::exception& exception) {
            error = std::string("Raster edit evaluation failed: ") + exception.what();
            return false;
        }
    }
    return true;
}

bool render_raster_page(const document::DocumentSnapshot& source,
                        const document::DocumentPage& page,
                        std::optional<geometry::Rect2D> explicit_bounds,
                        uint32_t dpi,
                        std::shared_ptr<const color::ProfileResource> rgb_profile,
                        std::shared_ptr<const color::ProfileResource> cmyk_profile,
                        std::vector<uint8_t>& pixels,
                        uint32_t& width_px, uint32_t& height_px,
                        const std::function<bool(uint64_t)>& wait_for_memory_headroom,
                        const std::function<bool()>& cancelled,
                        std::string& error) {
    const auto bounds = explicit_bounds.value_or(
        geometry::Rect2D{0.0, 0.0, page.layout.width_mm, page.layout.height_mm});
    if (!bounds.is_finite() || bounds.is_empty() || dpi == 0) {
        error = "The selected page or Artboard has invalid physical dimensions or DPI.";
        return false;
    }
    const double scale = static_cast<double>(dpi) / 25.4;
    const double width = std::ceil(bounds.width * scale);
    const double height = std::ceil(bounds.height * scale);
    if (!std::isfinite(width) || !std::isfinite(height) || width < 1.0 || height < 1.0 ||
        width > 16384.0 || height > 16384.0) {
        error = "The raster output exceeds the renderer's 16,384 px per-side limit.";
        return false;
    }
    width_px = static_cast<uint32_t>(width);
    height_px = static_cast<uint32_t>(height);
    const uint64_t output_bytes = static_cast<uint64_t>(width_px) * height_px * 4u;
    constexpr uint64_t reserve = 16ULL * 1024ULL * 1024ULL;
    if (output_bytes > SIZE_MAX || output_bytes > UINT64_MAX - reserve) {
        error = "The RGBA render buffer size exceeds this process's addressable range.";
        return false;
    }
    if (!wait_for_memory_headroom ||
        !wait_for_memory_headroom(output_bytes + reserve)) {
        error = "Raster rendering was cancelled or stopped after five minutes waiting for system RAM below 95%.";
        return false;
    }

    if (page.layout.color_profile_resource) {
        const auto& resource = page.layout.color_profile_resource;
        if (resource->identity().color_space == color::ColorSpace::Rgb)
            rgb_profile = resource;
        else if (resource->identity().color_space == color::ColorSpace::Cmyk)
            cmyk_profile = resource;
    }
    if (page.layout.color_mode == color::DocumentColorMode::Cmyk && !cmyk_profile) {
        error = "The page uses CMYK but no source ICC profile is available for color-managed raster export.";
        return false;
    }
    std::string color_error;
    auto preview = color::IccPreview::create_from_resources(
        std::move(rgb_profile), std::move(cmyk_profile), &color_error);
    if (!preview) {
        error = color_error.empty()
            ? "The page's assigned ICC profiles could not be opened by the Windows color engine."
            : "The page's assigned ICC profile could not be used: " + color_error;
        return false;
    }

    try {
        pixels.resize(static_cast<size_t>(output_bytes));
    } catch (...) {
        error = "Could not allocate the raster render buffer after RAM headroom became available.";
        return false;
    }
    auto evaluated_page = page;
    if (!evaluate_raster_edits_for_export(
            evaluated_page, wait_for_memory_headroom, cancelled, error))
        return false;
    document::DocumentSnapshot page_snapshot(
        source.epoch(), source.revision(),
        std::vector<document::DocumentPage>{std::move(evaluated_page)},
        page.id, source.document_id(), source.content_mode());
    rendering::SoftwareRasterizer renderer;
    (void)renderer.set_color_preview(std::move(preview));
    (void)renderer.set_canvas_background({255, 255, 255, 255});
    rendering::PixelBufferTarget target{
        pixels.data(), static_cast<int32_t>(width_px), static_cast<int32_t>(height_px),
        static_cast<int32_t>(width_px * 4u)};
    rendering::Camera2D camera(
        {bounds.x + bounds.width / 2.0, bounds.y + bounds.height / 2.0}, scale,
        static_cast<int32_t>(width_px), static_cast<int32_t>(height_px));
    rendering::RenderMetric metric;
    if (!renderer.render(page_snapshot, camera, target, &metric)) {
        error = "The CPU raster renderer rejected this page.";
        if (!metric.diagnostics.empty()) error += " " + metric.diagnostics.front().message;
        return false;
    }
    return true;
}

std::vector<std::wstring> raster_output_paths(const std::wstring& selected,
                                              size_t count, bool all_pages) {
    if (!all_pages) return {selected};
    const std::filesystem::path path(selected);
    const auto base = path.parent_path() / path.stem();
    const auto extension = path.extension().wstring();
    std::vector<std::wstring> outputs;
    outputs.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        auto number = std::to_wstring(index + 1);
        if (number.size() < 3) number.insert(number.begin(), 3 - number.size(), L'0');
        outputs.push_back(base.wstring() + L"-page-" + number + extension);
    }
    return outputs;
}

} // namespace

void NativeWindow::export_selected_bitmap_master() {
#ifdef _WIN32
    if (!can_export_selected_bitmap_master()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chọn một ảnh có pixel đầy đủ và profile màu đã xác định"
            : "Select one image with complete pixels and a resolved color profile";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (raster_export_job_) {
        if (raster_export_job_->worker.joinable()) raster_export_job_->worker.join();
        raster_export_job_.reset();
    }
    const auto& selected = root().vector_controller().selection().selected_ids();
    const auto snapshot = root().document().current_snapshot();
    if (selected.size() != 1 || !snapshot) return;
    const auto object_id = selected.front();
    const auto* object = snapshot->objects().lookup_ref(object_id);
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return;

    const bool vietnamese = language_ == UILanguage::Vietnamese;
    std::wstring output_path;
    if (raster_bitmap_master_file_picker_) {
        const auto selected_path = raster_bitmap_master_file_picker_();
        if (!selected_path) return;
        output_path = selected_path->wstring();
    } else {
        output_path = raster_output_dialog(
            vietnamese, interop::RasterImageOutputFormat::Tiff, false, hwnd_, true);
    }
    if (output_path.empty()) return;
    if (!document_path_.empty() &&
        same_windows_path(output_path, utf8_to_wide(document_path_))) {
        MessageBoxW(hwnd_, vietnamese
            ? L"Không thể ghi đè tệp tài liệu đang mở bằng ảnh Raster."
            : L"The open document cannot be overwritten by a raster image.",
            vietnamese ? L"Đường dẫn không hợp lệ" : L"Invalid output path",
            MB_OK | MB_ICONWARNING);
        return;
    }

    std::shared_ptr<const color::ProfileResource> srgb_profile;
    if (image->source.preview_pixel_color_basis !=
        document::RasterPixelColorBasis::EmbeddedProfileUnapplied) {
        const auto srgb_path = color::IccPreview::system_srgb_profile();
        const auto loaded = srgb_path.empty()
            ? color::ProfileResourceResult{}
            : color::ProfileResource::load_file(srgb_path, color::ColorSpace::Rgb);
        if (!loaded || loaded.resource->bytes().size() > 1024u * 1024u) {
            MessageBoxW(hwnd_, vietnamese
                ? L"Không thể tải profile sRGB để nhúng vào tệp TIFF."
                : L"The system sRGB profile could not be loaded for TIFF export.",
                vietnamese ? L"Không thể xuất ảnh" : L"Raster export unavailable",
                MB_OK | MB_ICONWARNING);
            return;
        }
        srgb_profile = loaded.resource;
    }

    auto session = std::make_shared<RasterExportJobSession>();
    session->total_files = 1;
    const HWND notify_window = hwnd_;
    auto protected_snapshot = snapshot;
    raster_export_job_ = session;
    try {
        session->worker = std::jthread(
            [raw = session.get(), notify_window,
             protected_snapshot = std::move(protected_snapshot), object_id,
             output_path, srgb_profile = std::move(srgb_profile)](std::stop_token stop) {
                (void)SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
                workers::SystemMemoryPressureWaiter memory_waiter(
                    workers::kSystemMemoryPressureWaitLimit,
                    [raw, notify_window](bool waiting) {
                        raw->waiting_for_memory.store(waiting, std::memory_order_release);
                        (void)PostMessageW(notify_window, kRasterExportWorkerMessage, 0, 0);
                    });
                const auto wait_for_memory_headroom = [&memory_waiter, stop](uint64_t bytes) {
                    if (bytes > SIZE_MAX) return false;
                    return memory_waiter.WaitForHeadroom(
                        static_cast<size_t>(bytes), stop) ==
                        workers::SystemMemoryWaitResult::Ready;
                };
                const auto cancelled = [raw, stop] {
                    return stop.stop_requested() ||
                        raw->cancel_requested.load(std::memory_order_acquire);
                };
                try {
                    const auto* object = protected_snapshot->objects().lookup_ref(object_id);
                    const auto* bitmap = object
                        ? std::get_if<document::ImageObject>(object) : nullptr;
                    if (!bitmap) {
                        raw->error = "The selected image is no longer present in the captured document.";
                    } else {
                        RasterBitmapExportOptions options;
                        if (srgb_profile) options.srgb_profile = srgb_profile->bytes();
                        options.allow_replace_existing = true;
                        options.wait_for_memory_headroom = wait_for_memory_headroom;
                        options.cancelled = cancelled;
                        const auto result = export_raster_bitmap_master_tiff(
                            *bitmap, output_path, options);
                        raw->success = result.success;
                        if (result.success) {
                            raw->completed_files.store(1, std::memory_order_release);
                        } else {
                            raw->cancelled = cancelled();
                            std::lock_guard lock(raw->result_mutex);
                            raw->error = result.diagnostic;
                        }
                    }
                } catch (const std::exception& exception) {
                    std::lock_guard lock(raw->result_mutex);
                    raw->error = exception.what();
                } catch (...) {
                    std::lock_guard lock(raw->result_mutex);
                    raw->error = "Unexpected exception during bitmap master export.";
                }
                raw->finished.store(true, std::memory_order_release);
                (void)PostMessageW(notify_window, kRasterExportWorkerMessage, 1, 0);
            });
    } catch (const std::exception& exception) {
        raster_export_job_.reset();
        MessageBoxW(hwnd_, utf8_to_wide(exception.what()).c_str(),
            vietnamese ? L"Lỗi xuất ảnh" : L"Raster export error",
            MB_OK | MB_ICONERROR);
        return;
    }
    status_msg_ = vietnamese
        ? "Đang xuất ảnh gốc đủ pixel thành TIFF · Esc hủy sau tệp hiện tại"
        : "Exporting full-resolution TIFF bitmap master · Esc cancels after this file";
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::export_selected_bitmap_master_cmyk() {
#ifdef _WIN32
    if (!can_export_selected_bitmap_cmyk_master()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chọn một ảnh có pixel đầy đủ và profile màu đã xác định"
            : "Select one image with complete pixels and a resolved color profile";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (raster_export_job_) {
        if (raster_export_job_->worker.joinable()) raster_export_job_->worker.join();
        raster_export_job_.reset();
    }
    const auto& selected = root().vector_controller().selection().selected_ids();
    const auto snapshot = root().document().current_snapshot();
    if (selected.size() != 1 || !snapshot) return;
    const auto object_id = selected.front();
    const auto* object = snapshot->objects().lookup_ref(object_id);
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return;

    const bool vietnamese = language_ == UILanguage::Vietnamese;
    const auto cmyk_profile = choose_raster_cmyk_profile(hwnd_, vietnamese);
    if (!cmyk_profile) return;
    const auto intent = choose_raster_cmyk_intent(hwnd_, vietnamese);
    if (!intent) return;

    std::shared_ptr<const color::ProfileResource> source_profile;
    if (image->source.preview_pixel_color_basis ==
        document::RasterPixelColorBasis::EmbeddedProfileUnapplied) {
        auto loaded = color::ProfileResource::create(
            "Raster source ICC", color::ColorSpace::Rgb, image->source.icc_profile);
        if (loaded) source_profile = std::move(loaded.resource);
    } else {
        const auto srgb_path = color::IccPreview::system_srgb_profile();
        auto loaded = srgb_path.empty()
            ? color::ProfileResourceResult{}
            : color::ProfileResource::load_file(srgb_path, color::ColorSpace::Rgb);
        if (loaded && loaded.resource->bytes().size() <= 1024u * 1024u)
            source_profile = std::move(loaded.resource);
    }
    if (!source_profile) {
        MessageBoxW(hwnd_, vietnamese
            ? L"Không thể chuyển ảnh: cần profile nguồn RGB đã resolve. Ảnh CMYK chưa có buffer kênh gốc trong model hiện tại."
            : L"This image cannot be converted: a resolved RGB source profile is required. Native CMYK source channels are not retained by the current image model.",
            vietnamese ? L"Không thể xuất CMYK" : L"CMYK export unavailable",
            MB_OK | MB_ICONWARNING);
        return;
    }
    const auto working_path = color::IccPreview::system_srgb_profile();
    auto working_loaded = working_path.empty()
        ? color::ProfileResourceResult{}
        : color::ProfileResource::load_file(working_path, color::ColorSpace::Rgb);
    if (!working_loaded || working_loaded.resource->bytes().size() > 1024u * 1024u) {
        MessageBoxW(hwnd_, vietnamese
            ? L"Không thể nạp profile sRGB làm miền làm việc cho chuyển đổi CMYK."
            : L"The sRGB working profile required for CMYK output is unavailable.",
            vietnamese ? L"Không thể xuất CMYK" : L"CMYK export unavailable",
            MB_OK | MB_ICONWARNING);
        return;
    }
    const auto& source_bytes = source_profile->bytes();
    const auto& working_bytes = working_loaded.resource->bytes();
    if (source_bytes.size() != working_bytes.size() ||
        !std::equal(source_bytes.begin(), source_bytes.end(), working_bytes.begin())) {
        const auto source_transform = color::ProfileColorTransform::create(
            source_profile, working_loaded.resource,
            {color::IccRenderingIntent::RelativeColorimetric, false});
        if (!source_transform) {
            MessageBoxW(hwnd_, utf8_to_wide(source_transform.diagnostic).c_str(),
                vietnamese ? L"Không thể resolve ICC nguồn" : L"Source ICC conversion unavailable",
                MB_OK | MB_ICONWARNING);
            return;
        }
    }
    const auto output_transform = color::ProfileColorTransform::create(
        working_loaded.resource, cmyk_profile, {*intent, false});
    if (!output_transform) {
        MessageBoxW(hwnd_, utf8_to_wide(output_transform.diagnostic).c_str(),
            vietnamese ? L"Không thể tạo chuyển đổi ICC" : L"ICC conversion unavailable",
            MB_OK | MB_ICONWARNING);
        return;
    }

    const auto output_path = raster_output_dialog(
        vietnamese, interop::RasterImageOutputFormat::Tiff, false, hwnd_, true);
    if (output_path.empty()) return;
    if (!document_path_.empty() &&
        same_windows_path(output_path, utf8_to_wide(document_path_))) {
        MessageBoxW(hwnd_, vietnamese
            ? L"Không thể ghi đè tệp tài liệu đang mở bằng ảnh Raster."
            : L"The open document cannot be overwritten by a raster image.",
            vietnamese ? L"Đường dẫn không hợp lệ" : L"Invalid output path",
            MB_OK | MB_ICONWARNING);
        return;
    }

    auto session = std::make_shared<RasterExportJobSession>();
    session->total_files = 1;
    const HWND notify_window = hwnd_;
    auto protected_snapshot = snapshot;
    raster_export_job_ = session;
    try {
        session->worker = std::jthread(
            [raw = session.get(), notify_window,
             protected_snapshot = std::move(protected_snapshot), object_id,
             output_path, working_profile = std::move(working_loaded.resource),
             cmyk_profile, intent = *intent](std::stop_token stop) {
                (void)SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
                workers::SystemMemoryPressureWaiter memory_waiter(
                    workers::kSystemMemoryPressureWaitLimit,
                    [raw, notify_window](bool waiting) {
                        raw->waiting_for_memory.store(waiting, std::memory_order_release);
                        (void)PostMessageW(notify_window, kRasterExportWorkerMessage, 0, 0);
                    });
                const auto wait_for_memory_headroom = [&memory_waiter, stop](uint64_t bytes) {
                    if (bytes > SIZE_MAX) return false;
                    return memory_waiter.WaitForHeadroom(
                        static_cast<size_t>(bytes), stop) ==
                        workers::SystemMemoryWaitResult::Ready;
                };
                const auto cancelled = [raw, stop] {
                    return stop.stop_requested() ||
                        raw->cancel_requested.load(std::memory_order_acquire);
                };
                try {
                    const auto* object = protected_snapshot->objects().lookup_ref(object_id);
                    const auto* bitmap = object
                        ? std::get_if<document::ImageObject>(object) : nullptr;
                    if (!bitmap) {
                        raw->error = "The selected image is no longer present in the captured document.";
                    } else {
                        RasterBitmapExportOptions options;
                        options.srgb_profile = working_profile->bytes();
                        options.cmyk_output_profile = cmyk_profile->bytes();
                        options.cmyk_rendering_intent = intent;
                        options.allow_replace_existing = true;
                        options.wait_for_memory_headroom = wait_for_memory_headroom;
                        options.cancelled = cancelled;
                        const auto result = export_raster_bitmap_master_tiff(
                            *bitmap, output_path, options);
                        raw->success = result.success;
                        if (result.success) {
                            raw->completed_files.store(1, std::memory_order_release);
                        } else {
                            raw->cancelled = cancelled();
                            std::lock_guard lock(raw->result_mutex);
                            raw->error = result.diagnostic;
                        }
                    }
                } catch (const std::exception& exception) {
                    std::lock_guard lock(raw->result_mutex);
                    raw->error = exception.what();
                } catch (...) {
                    std::lock_guard lock(raw->result_mutex);
                    raw->error = "Unexpected exception during CMYK bitmap export.";
                }
                raw->finished.store(true, std::memory_order_release);
                (void)PostMessageW(notify_window, kRasterExportWorkerMessage, 1, 0);
            });
    } catch (const std::exception& exception) {
        raster_export_job_.reset();
        MessageBoxW(hwnd_, utf8_to_wide(exception.what()).c_str(),
            vietnamese ? L"Lỗi xuất ảnh" : L"Raster export error",
            MB_OK | MB_ICONERROR);
        return;
    }
    status_msg_ = vietnamese
        ? "Đang chuyển ảnh RGB sang CMYK16 TIFF · " + std::string(
            *intent == color::IccRenderingIntent::Perceptual ? "Perceptual" :
            *intent == color::IccRenderingIntent::RelativeColorimetric ? "Relative" :
            *intent == color::IccRenderingIntent::Saturation ? "Saturation" : "Absolute") +
            " · Esc để hủy"
        : "Converting RGB bitmap to CMYK16 TIFF · Esc cancels";
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
#endif
}

void NativeWindow::export_raster_dialog() {
    const auto snapshot = root().document().current_snapshot();
    if (!snapshot || snapshot->page_count() == 0) return;
    if (export_job_active() || raster_export_job_active()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đang có tác vụ xuất tệp" : "An export task is already running";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (raster_export_job_) {
        if (raster_export_job_->worker.joinable()) raster_export_job_->worker.join();
        raster_export_job_.reset();
    }

    RasterExportDialogState state;
    state.vietnamese = language_ == UILanguage::Vietnamese;
    state.default_dpi = static_cast<uint32_t>(snapshot->page_raster_dpi(snapshot->active_page_id()));
    state.has_active_artboard = std::any_of(
        snapshot->active_page().artboards.begin(), snapshot->active_page().artboards.end(),
        [&](const auto& board) { return board.id == active_tab().active_artboard_id; });
    state.cmyk_preview_profile = cmyk_preview_resource_;
    const auto srgb_path = color::IccPreview::system_srgb_profile();
    auto srgb = color::ProfileResource::load_file(srgb_path, color::ColorSpace::Rgb);
    if (!srgb || srgb.resource->bytes().size() > 1024u * 1024u) {
        MessageBoxW(hwnd_,
            state.vietnamese ? L"Không thể nạp profile sRGB hệ thống để xuất Raster."
                             : L"The system sRGB profile could not be loaded for raster export.",
            state.vietnamese ? L"Xuất Raster không khả dụng" : L"Raster export unavailable",
            MB_OK | MB_ICONWARNING);
        return;
    }
    state.settings.output_profile = std::move(srgb.resource);

    struct EmptyDialog { DLGTEMPLATE dialog; WORD menu{0}, klass{0}, title{0}; } layout{};
    layout.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME |
        DS_CENTER | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    layout.dialog.cx = 380;
    layout.dialog.cy = 272;
    if (DialogBoxIndirectParamW(GetModuleHandleW(nullptr), &layout.dialog, hwnd_,
            raster_export_dialog_proc, reinterpret_cast<LPARAM>(&state)) != IDOK ||
        !state.accepted)
        return;

    const bool all_pages = state.settings.all_pages;
    const bool active_artboard = state.settings.active_artboard;
    const auto selected_path = raster_output_dialog(state.vietnamese,
        state.settings.format, all_pages, hwnd_);
    if (selected_path.empty()) return;
    std::vector<size_t> page_indices;
    if (all_pages) {
        page_indices.reserve(snapshot->pages().size());
        for (size_t index = 0; index < snapshot->pages().size(); ++index)
            page_indices.push_back(index);
    } else {
        const auto active = std::find_if(snapshot->pages().begin(), snapshot->pages().end(),
            [&](const auto& page) { return page.id == snapshot->active_page_id(); });
        if (active == snapshot->pages().end()) {
            MessageBoxW(hwnd_, state.vietnamese
                ? L"Không tìm thấy trang đang hoạt động trong ảnh chụp tài liệu."
                : L"The active page is missing from the document snapshot.",
                state.vietnamese ? L"Không thể xuất Raster" : L"Raster export unavailable",
                MB_OK | MB_ICONWARNING);
            return;
        }
        page_indices.push_back(static_cast<size_t>(active - snapshot->pages().begin()));
    }
    const auto outputs = raster_output_paths(
        selected_path, page_indices.size(), all_pages);
    std::optional<geometry::Rect2D> artboard_bounds;
    if (active_artboard) {
        const auto& boards = snapshot->active_page().artboards;
        const auto board = std::find_if(boards.begin(), boards.end(), [&](const auto& candidate) {
            return candidate.id == active_tab().active_artboard_id;
        });
        if (board == boards.end()) {
            MessageBoxW(hwnd_, state.vietnamese
                ? L"Artboard đang chọn không còn trong ảnh chụp tài liệu."
                : L"The selected Artboard is missing from the captured document snapshot.",
                state.vietnamese ? L"Không thể xuất Raster" : L"Raster export unavailable",
                MB_OK | MB_ICONWARNING);
            return;
        }
        artboard_bounds = geometry::Rect2D{
            board->x_mm, board->y_mm, board->width_mm, board->height_mm};
    }
    if (!document_path_.empty()) {
        for (const auto& output : outputs) {
            if (same_windows_path(output, utf8_to_wide(document_path_))) {
                MessageBoxW(hwnd_, state.vietnamese
                    ? L"Không thể ghi đè tệp tài liệu đang mở bằng ảnh xuất."
                    : L"The open document cannot be overwritten by raster export.",
                    state.vietnamese ? L"Đường dẫn không hợp lệ" : L"Invalid output path",
                    MB_OK | MB_ICONWARNING);
                return;
            }
        }
    }
    size_t existing_outputs = 0;
    for (const auto& output : outputs) {
        std::error_code error;
        if (std::filesystem::exists(output, error) && !error) ++existing_outputs;
    }
    bool replace_existing = false;
    if (existing_outputs > 0) {
        if (all_pages) {
            std::wstring prompt = state.vietnamese
                ? L"Thay thế " : L"Replace ";
            prompt += std::to_wstring(existing_outputs);
            prompt += state.vietnamese
                ? L" tệp Raster đã tồn tại? Các tệp được ghi riêng từng bước."
                : L" existing raster file(s)? Files are committed one at a time.";
            if (MessageBoxW(hwnd_, prompt.c_str(),
                    state.vietnamese ? L"Xác nhận ghi đè" : L"Confirm replacement",
                    MB_YESNO | MB_ICONQUESTION) != IDYES)
                return;
        }
        // The single-page picker already performed its standard overwrite prompt.
        replace_existing = true;
    }

    auto session = std::make_shared<RasterExportJobSession>();
    session->total_files = outputs.size();
    const HWND notify_window = hwnd_;
    auto protected_snapshot = snapshot;
    auto output_profile = state.settings.output_profile;
    auto source_rgb_profile = rgb_preview_resource_;
    auto source_cmyk_profile = cmyk_preview_resource_;
    const auto settings = state.settings;
    raster_export_job_ = session;
    try {
    session->worker = std::jthread(
        [raw = session.get(), notify_window, protected_snapshot = std::move(protected_snapshot),
         outputs, settings, output_profile = std::move(output_profile),
         source_rgb_profile = std::move(source_rgb_profile),
         source_cmyk_profile = std::move(source_cmyk_profile),
         page_indices = std::move(page_indices), artboard_bounds,
         active_artboard, replace_existing](std::stop_token stop) mutable {
            (void)SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            workers::SystemMemoryPressureWaiter memory_waiter(
                workers::kSystemMemoryPressureWaitLimit,
                [raw, notify_window](bool waiting) {
                    raw->waiting_for_memory.store(waiting, std::memory_order_release);
                    (void)PostMessageW(notify_window, kRasterExportWorkerMessage, 0, 0);
                });
            const auto wait_for_memory_headroom = [&memory_waiter, stop](uint64_t bytes) {
                if (bytes > SIZE_MAX) return false;
                return memory_waiter.WaitForHeadroom(
                    static_cast<size_t>(bytes), stop) ==
                    workers::SystemMemoryWaitResult::Ready;
            };
            const auto cancelled = [raw, stop] {
                return stop.stop_requested() ||
                    raw->cancel_requested.load(std::memory_order_acquire);
            };
            size_t written_count = 0;
            try {
            for (size_t index = 0; index < outputs.size(); ++index) {
                if (stop.stop_requested() ||
                    raw->cancel_requested.load(std::memory_order_acquire)) {
                    raw->cancelled = true;
                    break;
                }
                const auto& page = protected_snapshot->pages()[page_indices[index]];
                std::vector<uint8_t> pixels;
                uint32_t width_px = 0, height_px = 0;
                std::string error;
                if (!render_raster_page(*protected_snapshot, page,
                        active_artboard ? artboard_bounds : std::nullopt, settings.dpi,
                        source_rgb_profile, source_cmyk_profile, pixels,
                        width_px, height_px, wait_for_memory_headroom,
                        cancelled, error)) {
                    if (stop.stop_requested() ||
                        raw->cancel_requested.load(std::memory_order_acquire)) {
                        raw->cancelled = true;
                        break;
                    }
                    std::lock_guard lock(raw->result_mutex);
                    raw->error = std::move(error);
                    break;
                }
                if (stop.stop_requested() ||
                    raw->cancel_requested.load(std::memory_order_acquire)) {
                    raw->cancelled = true;
                    break;
                }
                interop::RasterImageWriteOptions options;
                options.format = settings.format;
                options.dpi_x = settings.dpi;
                options.dpi_y = settings.dpi;
                options.jpeg_quality = settings.jpeg_quality;
                options.wait_for_memory_headroom = wait_for_memory_headroom;
                options.destination_icc_profile = output_profile->bytes();
                options.embedded_icc_profile = settings.embed_profile
                    ? std::span<const uint8_t>(output_profile->bytes())
                    : std::span<const uint8_t>{};
                options.convert_pixels_to_destination_profile = true;
                options.allow_replace_existing = replace_existing;
                const auto written = interop::WriteRasterImageFile(
                    outputs[index], width_px, height_px, pixels, options);
                if (!written.success) {
                    if (stop.stop_requested() ||
                        raw->cancel_requested.load(std::memory_order_acquire)) {
                        raw->cancelled = true;
                        break;
                    }
                    std::lock_guard lock(raw->result_mutex);
                    raw->error = written.diagnostic;
                    break;
                }
                ++written_count;
                raw->completed_files.store(written_count, std::memory_order_release);
                (void)PostMessageW(notify_window, kRasterExportWorkerMessage, 0, 0);
            }
            } catch (const std::exception& exception) {
                std::lock_guard lock(raw->result_mutex);
                raw->error = exception.what();
            } catch (...) {
                std::lock_guard lock(raw->result_mutex);
                raw->error = "Unexpected exception during raster export.";
            }
            raw->success = written_count == outputs.size();
            raw->finished.store(true, std::memory_order_release);
            (void)PostMessageW(notify_window, kRasterExportWorkerMessage, 1, 0);
        });
    } catch (const std::exception& exception) {
        raster_export_job_.reset();
        status_msg_ = state.vietnamese ? "Không thể khởi chạy tác vụ xuất Raster"
                                       : "Could not start the raster export worker";
        MessageBoxW(hwnd_, NativeWindow::utf8_to_wide(exception.what()).c_str(),
            state.vietnamese ? L"Lỗi xuất Raster" : L"Raster export error",
            MB_OK | MB_ICONERROR);
        return;
    }
    status_msg_ = state.vietnamese
        ? "Đang xuất Raster · nhấn Esc để hủy sau tệp hiện tại"
        : "Exporting raster · press Esc to cancel after the current file";
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativeWindow::request_cancel_raster_export_job() {
    if (!raster_export_job_ || raster_export_job_->finished.load(std::memory_order_acquire)) return;
    raster_export_job_->cancel_requested.store(true, std::memory_order_release);
    raster_export_job_->worker.request_stop();
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Đang hủy xuất Raster sau tệp hiện tại…"
        : "Cancelling raster export after the current file…";
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativeWindow::cancel_raster_export_job() {
    if (!raster_export_job_) return;
    request_cancel_raster_export_job();
    if (raster_export_job_->worker.joinable()) raster_export_job_->worker.join();
    raster_export_job_.reset();
}

void NativeWindow::handle_raster_export_worker_message(WPARAM kind) {
    if (!raster_export_job_) return;
    auto session = raster_export_job_;
    if (kind == 0 && !session->finished.load(std::memory_order_acquire)) {
        if (session->waiting_for_memory.load(std::memory_order_acquire)) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đang chờ RAM xuống dưới 95% · tự tiếp tục, tối đa 5 phút · Esc để hủy"
                : "Waiting for RAM below 95% · resumes automatically, up to 5 min · Esc cancels";
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        const auto complete = session->completed_files.load(std::memory_order_acquire);
        status_msg_ = (language_ == UILanguage::Vietnamese
            ? "Đã xuất Raster " : "Exported raster ") +
            std::to_string(complete) + "/" + std::to_string(session->total_files) +
            (language_ == UILanguage::Vietnamese ? " · Esc hủy sau tệp hiện tại"
                                                  : " · Esc cancels after current file");
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (session->worker.joinable()) session->worker.join();
    raster_export_job_.reset();
    const auto complete = session->completed_files.load(std::memory_order_acquire);
    std::string error;
    {
        std::lock_guard lock(session->result_mutex);
        error = session->error;
    }
    if (session->success) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã xuất " + std::to_string(complete) + " tệp Raster"
            : "Exported " + std::to_string(complete) + " raster file(s)";
    } else if (session->cancelled) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã hủy xuất Raster sau " + std::to_string(complete) + " tệp"
            : "Raster export cancelled after " + std::to_string(complete) + " file(s)";
    } else {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Xuất Raster thất bại" : "Raster export failed";
    }
    if (!session->success && hwnd_) {
        std::wstring report = session->cancelled
            ? (language_ == UILanguage::Vietnamese
                ? L"Đã hủy sau khi hoàn tất " : L"Cancelled after completing ")
            : (language_ == UILanguage::Vietnamese
                ? L"Đã hoàn tất " : L"Completed ");
        report += std::to_wstring(complete);
        report += language_ == UILanguage::Vietnamese
            ? L" tệp.\n\n" : L" file(s).\n\n";
        report += raster_error_to_wide(error.empty()
            ? "The raster export did not complete." : error);
        MessageBoxW(hwnd_, report.c_str(),
            language_ == UILanguage::Vietnamese ? L"Báo cáo xuất Raster" : L"Raster export report",
            MB_OK | (session->cancelled ? MB_ICONINFORMATION : MB_ICONWARNING));
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

#endif

} // namespace pikdraw::desktop
