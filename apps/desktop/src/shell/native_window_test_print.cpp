#include "pikdraw/desktop/shell/native_window.hpp"

#include "pikdraw/color/preview/icc_preview.hpp"
#include "pikdraw/color/profiles/profile_resource.hpp"
#include "pikdraw/interop/writers/raster_image_writer.hpp"
#include "pikdraw/production/print/test_sheet_render.hpp"
#include "pikdraw/raster/edit_geometry.hpp"
#include "pikdraw/raster/evaluator.hpp"
#include "pikdraw/workers/job_object.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <bcrypt.h>
#include <commdlg.h>
#endif

namespace pikdraw::desktop {

#ifdef _WIN32
struct TestSheetJobSession {
    std::jthread worker;
    std::atomic<bool> finished{false};
    std::atomic<bool> cancel_requested{false};
    std::atomic<bool> waiting_for_memory{false};
    std::atomic<uint32_t> completed_elements{0};
    std::atomic<uint32_t> total_elements{0};
    bool success{false};
    bool cancelled{false};
    HDC printer_dc{nullptr};
    uint32_t width_px{0};
    uint32_t height_px{0};
    double ruler_mm{0.0};
    std::wstring output_path;
    std::string printer_name;
    std::string destination_profile_sha256;
    std::mutex result_mutex;
    std::string error;
};

namespace {
bool same_output_as_open_document(const std::wstring& output,
                                 const std::string& document_path) {
    if (document_path.empty()) return false;
    std::error_code left_error, right_error;
    auto left = std::filesystem::absolute(output, left_error).lexically_normal().wstring();
    auto right = std::filesystem::absolute(NativeWindow::utf8_to_wide(document_path),
                                            right_error).lexically_normal().wstring();
    if (left_error || right_error) return true;
    return CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()),
                                right.c_str(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

std::wstring choose_test_sheet_path(HWND owner, bool vietnamese) {
    std::vector<wchar_t> path(32768, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"TIFF test sheet (*.tif;*.tiff)\0*.tif;*.tiff\0\0";
    dialog.lpstrTitle = vietnamese ? L"Xuất bảng in thử" : L"Export Test Sheet";
    dialog.lpstrDefExt = L"tif";
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR |
                   OFN_HIDEREADONLY;
    return GetSaveFileNameW(&dialog) ? std::wstring(path.data()) : std::wstring{};
}

std::string job_identity(const document::DocumentSnapshot& snapshot,
                         document::ObjectId object_id, uint64_t revision) {
    return "test-" + snapshot.document_id() + "-object-" +
        std::to_string(object_id) + "-revision-" + std::to_string(revision);
}

std::optional<std::string> printer_destination_profile_sha256(HDC printer_dc) {
    DWORD path_chars = 0;
    if (!GetICMProfileW(printer_dc, &path_chars, nullptr) ||
        path_chars < 2 || path_chars > 32768)
        return std::nullopt;
    std::vector<wchar_t> path_buffer(path_chars, L'\0');
    if (!GetICMProfileW(printer_dc, &path_chars, path_buffer.data()))
        return std::nullopt;

    const std::filesystem::path profile_path(path_buffer.data());
    std::error_code file_error;
    const auto file_bytes = std::filesystem::file_size(profile_path, file_error);
    if (file_error || file_bytes == 0 || file_bytes > color::ProfileResource::kMaxProfileBytes)
        return std::nullopt;
    std::ifstream input(profile_path, std::ios::binary);
    if (!input) return std::nullopt;

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
            &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
        return std::nullopt;
    struct AlgorithmGuard {
        BCRYPT_ALG_HANDLE handle;
        ~AlgorithmGuard() { if (handle) BCryptCloseAlgorithmProvider(handle, 0); }
    } algorithm_guard{algorithm};
    ULONG object_bytes = 0;
    ULONG result_bytes = 0;
    if (!BCRYPT_SUCCESS(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&object_bytes), sizeof(object_bytes), &result_bytes, 0)) ||
        object_bytes == 0)
        return std::nullopt;
    std::vector<UCHAR> hash_object(object_bytes);
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (!BCRYPT_SUCCESS(BCryptCreateHash(
            algorithm, &hash, hash_object.data(), object_bytes, nullptr, 0, 0)))
        return std::nullopt;
    struct HashGuard {
        BCRYPT_HASH_HANDLE handle;
        ~HashGuard() { if (handle) BCryptDestroyHash(handle); }
    } hash_guard{hash};
    std::array<UCHAR, 64 * 1024> chunk{};
    uint64_t read_bytes = 0;
    while (input) {
        input.read(reinterpret_cast<char*>(chunk.data()),
                   static_cast<std::streamsize>(chunk.size()));
        const auto count = input.gcount();
        if (count <= 0) break;
        const auto chunk_bytes = static_cast<uint64_t>(count);
        if (chunk_bytes > color::ProfileResource::kMaxProfileBytes - read_bytes ||
            !BCRYPT_SUCCESS(BCryptHashData(hash, chunk.data(),
                static_cast<ULONG>(count), 0)))
            return std::nullopt;
        read_bytes += chunk_bytes;
    }
    if (!input.eof() || read_bytes != file_bytes) return std::nullopt;

    std::array<UCHAR, 32> digest{};
    if (!BCRYPT_SUCCESS(BCryptFinishHash(hash, digest.data(),
                                        static_cast<ULONG>(digest.size()), 0)))
        return std::nullopt;
    static constexpr char kHex[] = "0123456789abcdef";
    std::string result;
    result.reserve(digest.size() * 2);
    for (const auto byte : digest) {
        result.push_back(kHex[byte >> 4]);
        result.push_back(kHex[byte & 0x0f]);
    }
    return result;
}
}

bool NativeWindow::can_export_selected_bitmap_test_sheet() const {
    if (test_sheet_job_ && !test_sheet_job_->finished.load(std::memory_order_acquire)) return false;
    if (export_job_active() || raster_export_job_active() || raster_profile_conversion_active())
        return false;
    const auto& selection = root().vector_controller().selection().selected_ids();
    if (selection.size() != 1) return false;
    const auto snapshot = root().document().current_snapshot();
    if (!snapshot) return false;
    const auto* object = snapshot->objects().lookup_ref(selection.front());
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image || image->width_px <= 0 || image->height_px <= 0 || image->locked)
        return false;
    const auto basis = image->source.preview_pixel_color_basis;
    if (basis == document::RasterPixelColorBasis::LegacyUnknown ||
        basis == document::RasterPixelColorBasis::EmbeddedProfileUnapplied)
        return false;
    const uint64_t pixels = static_cast<uint64_t>(image->width_px) * image->height_px;
    if (pixels > std::numeric_limits<size_t>::max() / 8) return false;
    const auto u8 = image->rgba_pixels();
    const auto u16 = image->rgba16le_pixels();
    if (u8.size() != pixels || (!u16.empty() && u16.size() != pixels * 8)) return false;
    return true;
}

void NativeWindow::export_selected_bitmap_test_sheet() {
    if (!can_export_selected_bitmap_test_sheet()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chọn một ảnh RGB đã xác định profile để tạo bảng in thử"
            : "Select one raster with a resolved RGB profile to create a test sheet";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (test_sheet_job_) {
        if (test_sheet_job_->worker.joinable()) test_sheet_job_->worker.join();
        test_sheet_job_.reset();
    }
    const auto selected = root().vector_controller().selection().selected_ids().front();
    const auto snapshot = root().document().current_snapshot();
    const auto* object = snapshot ? snapshot->objects().lookup_ref(selected) : nullptr;
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!snapshot || !image) return;

    auto srgb = color::ProfileResource::load_file(
        color::IccPreview::system_srgb_profile(), color::ColorSpace::Rgb);
    if (!srgb || srgb.resource->bytes().size() > 1024u * 1024u) {
        MessageBoxW(hwnd_, language_ == UILanguage::Vietnamese
            ? L"Không nạp được profile sRGB hệ thống để xuất bảng in thử."
            : L"The system sRGB profile could not be loaded for test-sheet export.",
            language_ == UILanguage::Vietnamese ? L"In thử không khả dụng" : L"Test sheet unavailable",
            MB_OK | MB_ICONWARNING);
        return;
    }

    constexpr uint32_t target_ppi = 300;
    constexpr double crop_size_mm = 25.0;
    const auto [evaluated_width, evaluated_height] =
        raster::raster_edited_pixel_dimensions(*image);
    if (evaluated_width == 0 || evaluated_height == 0) return;
    const uint32_t crop_width = (std::min)(evaluated_width,
        (std::max)(1u, static_cast<uint32_t>(std::lround(crop_size_mm * target_ppi / 25.4))));
    const uint32_t crop_height = (std::min)(evaluated_height,
        (std::max)(1u, static_cast<uint32_t>(std::lround(crop_size_mm * target_ppi / 25.4))));
    const uint64_t source_revision = image->asset_source
        ? image->asset_source->revision : snapshot->revision();
    production::print::TestSheetRequest request;
    request.run_id = job_identity(*snapshot, selected, source_revision);
    request.kind = production::print::TestSheetKind::CropAndDiagnostic;
    request.source = {snapshot->document_id(), snapshot->revision(), selected,
        source_revision, static_cast<uint32_t>(image->width_px),
        static_cast<uint32_t>(image->height_px)};
    request.edit_graph = image->edit_graph;
    request.evaluated_width_px = evaluated_width;
    request.evaluated_height_px = evaluated_height;
    request.condition.id = "system-srgb-file-tiff";
    request.condition.profile = srgb.resource;
    request.condition.route = production::print::PrintRoute::RgbFileExport;
    request.condition.target_ppi_x = target_ppi;
    request.condition.target_ppi_y = target_ppi;
    request.page_width_mm = 210.0;
    request.page_height_mm = 297.0;
    request.margin_mm = 10.0;
    request.patch_size_mm = 10.0;
    request.patch_gap_mm = 2.0;
    request.crop = {(evaluated_width - crop_width) / 2,
        (evaluated_height - crop_height) / 2,
        crop_width, crop_height};
    auto planned = production::print::plan_test_sheet(request);
    if (!planned) {
        MessageBoxW(hwnd_, NativeWindow::utf8_to_wide(planned.diagnostic).c_str(),
            language_ == UILanguage::Vietnamese ? L"Không thể lập bảng in thử" : L"Could not plan test sheet",
            MB_OK | MB_ICONWARNING);
        return;
    }
    const auto output_path = choose_test_sheet_path(hwnd_,
        language_ == UILanguage::Vietnamese);
    if (output_path.empty()) return;
    if (same_output_as_open_document(output_path, document_path_)) {
        MessageBoxW(hwnd_, language_ == UILanguage::Vietnamese
            ? L"Không thể ghi đè tài liệu .pr đang mở bằng bảng in thử."
            : L"The open .pr document cannot be overwritten by the test sheet.",
            language_ == UILanguage::Vietnamese ? L"Đường dẫn không hợp lệ" : L"Invalid output path",
            MB_OK | MB_ICONWARNING);
        return;
    }

    auto session = std::make_shared<TestSheetJobSession>();
    session->output_path = output_path;
    session->total_elements.store(static_cast<uint32_t>(planned.plan->elements.size()),
                                  std::memory_order_release);
    const HWND notify_window = hwnd_;
    const bool vietnamese = language_ == UILanguage::Vietnamese;
    auto frozen_plan = std::move(*planned.plan);
    auto profile = srgb.resource;
    test_sheet_job_ = session;
    try {
        session->worker = std::jthread(
            [raw = session.get(), notify_window, snapshot, selected,
             plan = std::move(frozen_plan), profile = std::move(profile),
             vietnamese](std::stop_token stop) mutable {
                (void)SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
                workers::SystemMemoryPressureWaiter memory_waiter(
                    workers::kSystemMemoryPressureWaitLimit,
                    [raw, notify_window](bool waiting) {
                        raw->waiting_for_memory.store(waiting, std::memory_order_release);
                        (void)PostMessageW(notify_window, kTestSheetWorkerMessage, 0, 0);
                    });
                const auto wait_for_memory = [&memory_waiter, raw, stop](uint64_t bytes) {
                    if (bytes > SIZE_MAX || stop.stop_requested() ||
                        raw->cancel_requested.load(std::memory_order_acquire)) return false;
                    return memory_waiter.WaitForHeadroom(static_cast<size_t>(bytes), stop) ==
                        workers::SystemMemoryWaitResult::Ready;
                };
                const auto cancelled = [raw, stop] {
                    return stop.stop_requested() ||
                        raw->cancel_requested.load(std::memory_order_acquire);
                };
                try {
                    const auto* object = snapshot->objects().lookup_ref(selected);
                    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
                    if (!image) {
                        std::lock_guard lock(raw->result_mutex);
                        raw->error = "The selected raster is absent from the frozen document snapshot.";
                    } else {
                        std::vector<uint8_t> rgba8;
                        raster::RasterPixelView source_view;
                        const auto rgba16 = image->rgba16le_pixels();
                        if (!rgba16.empty()) {
                            source_view = {rgba16, static_cast<uint32_t>(image->width_px),
                                static_cast<uint32_t>(image->height_px),
                                static_cast<uint64_t>(image->width_px) * 8,
                                raster::RasterPixelEncoding::SRgbRgba16LEStraightAlpha};
                        } else {
                            const auto packed = image->rgba_pixels();
                            const uint64_t bytes = static_cast<uint64_t>(packed.size()) * 4;
                            if (bytes > SIZE_MAX || !wait_for_memory(bytes)) {
                                raw->cancelled = cancelled();
                                std::lock_guard lock(raw->result_mutex);
                                raw->error = raw->cancelled
                                    ? "Test-sheet export was cancelled."
                                    : "Waiting for system memory was cancelled or timed out.";
                            } else {
                                rgba8.resize(static_cast<size_t>(bytes));
                                for (size_t i = 0; i < packed.size(); ++i) {
                                    rgba8[i * 4] = static_cast<uint8_t>(packed[i] >> 24);
                                    rgba8[i * 4 + 1] = static_cast<uint8_t>(packed[i] >> 16);
                                    rgba8[i * 4 + 2] = static_cast<uint8_t>(packed[i] >> 8);
                                    rgba8[i * 4 + 3] = static_cast<uint8_t>(packed[i]);
                                }
                                source_view = {rgba8, static_cast<uint32_t>(image->width_px),
                                    static_cast<uint32_t>(image->height_px),
                                    static_cast<uint64_t>(image->width_px) * 4,
                                    raster::RasterPixelEncoding::SRgbRgba8StraightAlpha};
                            }
                        }
                        if (raw->error.empty() && !cancelled()) {
                            production::print::TestSheetRenderOptions render_options;
                            render_options.memory_limit_bytes = 512ull * 1024ull * 1024ull;
                            render_options.wait_for_memory_headroom = wait_for_memory;
                            render_options.cancelled = cancelled;
                            render_options.progress = [raw, notify_window](
                                const production::print::TestSheetRenderProgress& progress) {
                                    if (progress.total_elements != 0) {
                                        raw->completed_elements.store(progress.completed_elements,
                                            std::memory_order_release);
                                        raw->total_elements.store(progress.total_elements,
                                            std::memory_order_release);
                                        (void)PostMessageW(notify_window,
                                            kTestSheetWorkerMessage, 0, 0);
                                    }
                                };
                            auto rendered = production::print::render_test_sheet(
                                plan, source_view, render_options);
                            if (!rendered.succeeded()) {
                                raw->cancelled = rendered.status ==
                                    production::print::TestSheetRenderStatus::Cancelled;
                                std::lock_guard lock(raw->result_mutex);
                                raw->error = rendered.diagnostic;
                            } else if (!cancelled()) {
                                interop::RasterImageWriteOptions writer;
                                writer.format = interop::RasterImageOutputFormat::Tiff;
                                writer.dpi_x = plan.ppi_x;
                                writer.dpi_y = plan.ppi_y;
                                writer.embedded_icc_profile = profile->bytes();
                                writer.allow_replace_existing = true;
                                writer.wait_for_memory_headroom = wait_for_memory;
                                const auto written = interop::WriteRasterImageFile(
                                    raw->output_path, rendered.sheet.width_px,
                                    rendered.sheet.height_px, rendered.sheet.image.bytes, writer);
                                if (written.success) {
                                    raw->width_px = rendered.sheet.width_px;
                                    raw->height_px = rendered.sheet.height_px;
                                    raw->ruler_mm = rendered.sheet.one_hundred_mm_ruler_actual_mm;
                                    raw->success = true;
                                } else {
                                    std::lock_guard lock(raw->result_mutex);
                                    raw->error = written.diagnostic;
                                }
                            }
                        } else if (cancelled()) {
                            raw->cancelled = true;
                        }
                    }
                } catch (const std::exception& exception) {
                    std::lock_guard lock(raw->result_mutex);
                    raw->error = exception.what();
                } catch (...) {
                    std::lock_guard lock(raw->result_mutex);
                    raw->error = "Unexpected failure while rendering the raster test sheet.";
                }
                raw->finished.store(true, std::memory_order_release);
                (void)PostMessageW(notify_window, kTestSheetWorkerMessage, 1, 0);
            });
    } catch (const std::exception& exception) {
        test_sheet_job_.reset();
        MessageBoxW(hwnd_, NativeWindow::utf8_to_wide(exception.what()).c_str(),
            vietnamese ? L"Không khởi chạy được tác vụ in thử" : L"Could not start test-sheet export",
            MB_OK | MB_ICONERROR);
        return;
    }
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Đang dựng bảng in thử A4/300 PPI · nhấn Esc để hủy"
        : "Rendering A4/300 PPI test sheet · press Esc to cancel";
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativeWindow::print_selected_bitmap_test_sheet() {
    if (!can_export_selected_bitmap_test_sheet()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chọn một ảnh RGB đã xác định profile để in bảng thử"
            : "Select one raster with a resolved RGB profile to print a test sheet";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (test_sheet_job_) {
        if (test_sheet_job_->worker.joinable()) test_sheet_job_->worker.join();
        test_sheet_job_.reset();
    }

    PRINTDLGW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd_;
    dialog.Flags = PD_RETURNDC | PD_USEDEVMODECOPIESANDCOLLATE |
        PD_HIDEPRINTTOFILE | PD_DISABLEPRINTTOFILE;
    if (!PrintDlgW(&dialog)) {
        if (dialog.hDC) DeleteDC(dialog.hDC);
        if (dialog.hDevMode) GlobalFree(dialog.hDevMode);
        if (dialog.hDevNames) GlobalFree(dialog.hDevNames);
        return;
    }
    HDC printer_dc = dialog.hDC;
    std::string printer_name;
    DEVMODEW selected_mode{};
    bool has_selected_mode = false;
    if (dialog.hDevMode) {
        const auto* mode = static_cast<const DEVMODEW*>(GlobalLock(dialog.hDevMode));
        if (mode && mode->dmSize >= sizeof(DEVMODEW)) {
            selected_mode = *mode;
            has_selected_mode = true;
        }
        if (mode) GlobalUnlock(dialog.hDevMode);
    }
    if (dialog.hDevNames) {
        const auto* names = static_cast<const DEVNAMES*>(GlobalLock(dialog.hDevNames));
        if (names) {
            const auto* device = reinterpret_cast<const wchar_t*>(names) +
                names->wDeviceOffset;
            printer_name = NativeWindow::wide_to_utf8(device);
            GlobalUnlock(dialog.hDevNames);
        }
    }
    if (dialog.hDevMode) GlobalFree(dialog.hDevMode);
    if (dialog.hDevNames) GlobalFree(dialog.hDevNames);
    if (!printer_dc) {
        MessageBoxW(hwnd_, language_ == UILanguage::Vietnamese
            ? L"Không tạo được device context của máy in."
            : L"The selected printer did not provide a device context.",
            language_ == UILanguage::Vietnamese ? L"In thử không khả dụng" : L"Test print unavailable",
            MB_OK | MB_ICONWARNING);
        return;
    }
    const bool has_icm_method = has_selected_mode &&
        (selected_mode.dmFields & DM_ICMMETHOD) != 0;
    const bool system_managed_method = has_icm_method &&
        (selected_mode.dmICMMethod == DMICMMETHOD_NONE ||
         selected_mode.dmICMMethod == DMICMMETHOD_SYSTEM);
    if (!system_managed_method || SetICMMode(printer_dc, ICM_ON) == 0 ||
        SetICMMode(printer_dc, ICM_QUERY) != ICM_ON) {
        DeleteDC(printer_dc);
        MessageBoxW(hwnd_, language_ == UILanguage::Vietnamese
            ? L"Không xác nhận được Windows WCS hoặc driver vẫn tự chuyển màu. Chọn ICM do Windows quản lý và tắt ICM riêng của driver để tránh chuyển đổi kép."
            : L"Windows WCS could not be enabled, or the driver still owns color conversion. Select Windows-managed ICM and disable separate driver ICM to prevent double conversion.",
            language_ == UILanguage::Vietnamese ? L"Không xác định được chủ sở hữu màu" : L"Color owner is not supported",
            MB_OK | MB_ICONWARNING);
        return;
    }
    const int ppi_x = GetDeviceCaps(printer_dc, LOGPIXELSX);
    const int ppi_y = GetDeviceCaps(printer_dc, LOGPIXELSY);
    const int page_width = GetDeviceCaps(printer_dc, PHYSICALWIDTH);
    const int page_height = GetDeviceCaps(printer_dc, PHYSICALHEIGHT);
    if (printer_name.empty() || ppi_x <= 0 || ppi_y <= 0 ||
        ppi_x > 9600 || ppi_y > 9600 || page_width <= 0 || page_height <= 0 ||
        GetDeviceCaps(printer_dc, HORZRES) <= 0 ||
        GetDeviceCaps(printer_dc, VERTRES) <= 0) {
        DeleteDC(printer_dc);
        MessageBoxW(hwnd_, language_ == UILanguage::Vietnamese
            ? L"Máy in không cung cấp tên thiết bị, khổ giấy hoặc PPI hợp lệ."
            : L"The printer did not provide a valid device name, paper size, or PPI.",
            language_ == UILanguage::Vietnamese ? L"Máy in không hỗ trợ" : L"Unsupported printer",
            MB_OK | MB_ICONWARNING);
        return;
    }

    const auto selected = root().vector_controller().selection().selected_ids().front();
    const auto snapshot = root().document().current_snapshot();
    const auto* object = snapshot ? snapshot->objects().lookup_ref(selected) : nullptr;
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!snapshot || !image) {
        DeleteDC(printer_dc);
        return;
    }
    auto srgb = color::ProfileResource::load_file(
        color::IccPreview::system_srgb_profile(), color::ColorSpace::Rgb);
    if (!srgb || srgb.resource->bytes().size() > 1024u * 1024u) {
        DeleteDC(printer_dc);
        MessageBoxW(hwnd_, language_ == UILanguage::Vietnamese
            ? L"Không nạp được profile sRGB nguồn để gửi dữ liệu màu cho driver."
            : L"The system sRGB source profile could not be loaded for the printer driver.",
            language_ == UILanguage::Vietnamese ? L"In thử không khả dụng" : L"Test print unavailable",
            MB_OK | MB_ICONWARNING);
        return;
    }
    const auto [evaluated_width, evaluated_height] =
        raster::raster_edited_pixel_dimensions(*image);
    if (evaluated_width == 0 || evaluated_height == 0) {
        DeleteDC(printer_dc);
        return;
    }

    constexpr double kMmPerInch = 25.4;
    const double page_width_mm = page_width * kMmPerInch / ppi_x;
    const double page_height_mm = page_height * kMmPerInch / ppi_y;
    const uint32_t crop_width = (std::min)(evaluated_width,
        (std::max)(1u, static_cast<uint32_t>(std::lround(25.0 * ppi_x / kMmPerInch))));
    const uint32_t crop_height = (std::min)(evaluated_height,
        (std::max)(1u, static_cast<uint32_t>(std::lround(25.0 * ppi_y / kMmPerInch))));
    const uint64_t source_revision = image->asset_source
        ? image->asset_source->revision : snapshot->revision();
    production::print::TestSheetRequest request;
    request.run_id = job_identity(*snapshot, selected, source_revision);
    request.kind = production::print::TestSheetKind::CropAndDiagnostic;
    request.source = {snapshot->document_id(), snapshot->revision(), selected,
        source_revision, static_cast<uint32_t>(image->width_px),
        static_cast<uint32_t>(image->height_px)};
    request.edit_graph = image->edit_graph;
    request.evaluated_width_px = evaluated_width;
    request.evaluated_height_px = evaluated_height;
    request.condition.id = "windows-wcs-printer-rgb";
    request.condition.device_id = printer_name;
    request.condition.media_id = has_selected_mode &&
            (selected_mode.dmFields & DM_PAPERSIZE) != 0
        ? "paper-code-" + std::to_string(selected_mode.dmPaperSize)
        : has_selected_mode &&
            (selected_mode.dmFields & (DM_PAPERWIDTH | DM_PAPERLENGTH)) ==
                (DM_PAPERWIDTH | DM_PAPERLENGTH)
            ? "custom-" + std::to_string(selected_mode.dmPaperWidth) + "x" +
                std::to_string(selected_mode.dmPaperLength) + "-0.1mm"
            : "printer-default-paper";
    request.condition.ink_set_id = "driver-default-inks-unspecified";
    const bool has_orientation = has_selected_mode &&
        (selected_mode.dmFields & DM_ORIENTATION) != 0;
    const std::string orientation = !has_orientation ||
            (selected_mode.dmOrientation != DMORIENT_LANDSCAPE &&
             selected_mode.dmOrientation != DMORIENT_PORTRAIT)
        ? "driver-default"
        : selected_mode.dmOrientation == DMORIENT_LANDSCAPE
            ? "landscape" : "portrait";
    request.condition.print_mode_id = std::string("windows-wcs-on;driver-icm-disabled;method-") +
        (selected_mode.dmICMMethod == DMICMMETHOD_SYSTEM ? "system" : "none") +
        ";orientation-" +
        orientation +
        ";color-" + (has_selected_mode &&
            (selected_mode.dmFields & DM_COLOR) != 0 &&
            selected_mode.dmColor == DMCOLOR_MONOCHROME ? "monochrome" : "color-or-driver-default");
    if (has_selected_mode && (selected_mode.dmFields & DM_PRINTQUALITY) != 0)
        request.condition.print_mode_id += ";quality-" +
            std::to_string(selected_mode.dmPrintQuality);
    if (has_selected_mode && (selected_mode.dmFields & DM_YRESOLUTION) != 0)
        request.condition.print_mode_id += ";y-resolution-" +
            std::to_string(selected_mode.dmYResolution);
    if (has_selected_mode && (selected_mode.dmFields & DM_MEDIATYPE) != 0)
        request.condition.print_mode_id += ";media-type-" +
            std::to_string(selected_mode.dmMediaType);
    if (has_selected_mode && (selected_mode.dmFields & DM_ICMINTENT) != 0)
        request.condition.print_mode_id += ";icm-intent-" +
            std::to_string(selected_mode.dmICMIntent);
    if (has_selected_mode && (selected_mode.dmFields & DM_DITHERTYPE) != 0)
        request.condition.print_mode_id += ";dither-type-" +
            std::to_string(selected_mode.dmDitherType);
    request.condition.profile = srgb.resource;
    request.condition.route = production::print::PrintRoute::SystemManagedRgb;
    request.condition.driver_color_state = production::print::DriverColorState::Disabled;
    request.condition.target_ppi_x = static_cast<uint32_t>(ppi_x);
    request.condition.target_ppi_y = static_cast<uint32_t>(ppi_y);
    request.page_width_mm = page_width_mm;
    request.page_height_mm = page_height_mm;
    request.margin_mm = 10.0;
    request.patch_size_mm = 10.0;
    request.patch_gap_mm = 2.0;
    request.crop = {(evaluated_width - crop_width) / 2,
        (evaluated_height - crop_height) / 2, crop_width, crop_height};
    auto planned = production::print::plan_test_sheet(request);
    if (!planned) {
        DeleteDC(printer_dc);
        MessageBoxW(hwnd_, NativeWindow::utf8_to_wide(planned.diagnostic).c_str(),
            language_ == UILanguage::Vietnamese ? L"Không thể lập bảng in thử" : L"Could not plan test sheet",
            MB_OK | MB_ICONWARNING);
        return;
    }

    auto session = std::make_shared<TestSheetJobSession>();
    session->printer_dc = printer_dc;
    session->printer_name = printer_name;
    session->total_elements.store(static_cast<uint32_t>(planned.plan->elements.size()),
                                  std::memory_order_release);
    const HWND notify_window = hwnd_;
    const bool vietnamese = language_ == UILanguage::Vietnamese;
    auto frozen_plan = std::move(*planned.plan);
    test_sheet_job_ = session;
    try {
        session->worker = std::jthread(
            [raw = session.get(), notify_window, snapshot, selected,
             plan = std::move(frozen_plan),
             vietnamese](std::stop_token stop) mutable {
                (void)SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
                workers::SystemMemoryPressureWaiter memory_waiter(
                    workers::kSystemMemoryPressureWaitLimit,
                    [raw, notify_window](bool waiting) {
                        raw->waiting_for_memory.store(waiting, std::memory_order_release);
                        (void)PostMessageW(notify_window, kTestSheetWorkerMessage, 0, 0);
                    });
                const auto cancelled = [raw, stop] {
                    return stop.stop_requested() ||
                        raw->cancel_requested.load(std::memory_order_acquire);
                };
                const auto wait_for_memory = [&memory_waiter, stop, &cancelled](uint64_t bytes) {
                    return bytes <= SIZE_MAX && !cancelled() &&
                        memory_waiter.WaitForHeadroom(static_cast<size_t>(bytes), stop) ==
                            workers::SystemMemoryWaitResult::Ready;
                };
                try {
                    const auto destination_profile_hash =
                        printer_destination_profile_sha256(raw->printer_dc);
                    if (!destination_profile_hash) {
                        raw->error = "Windows did not provide a readable destination profile for the selected printer mode.";
                    } else {
                        plan.condition.destination_profile_sha256 = *destination_profile_hash;
                        plan.condition.require_destination_profile_identity = true;
                        plan.condition.id += "-" + *destination_profile_hash;
                        const auto output_condition =
                            production::print::validate_output_condition(plan.condition);
                        if (output_condition) {
                            raw->destination_profile_sha256 = *destination_profile_hash;
                        } else {
                            raw->error = output_condition.diagnostic;
                        }
                    }
                    const auto* object = snapshot->objects().lookup_ref(selected);
                    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
                    if (!raw->error.empty()) {
                        // The worker keeps the selected printer DC private and never submits
                        // a job without a stable destination-profile identity.
                    } else if (!image) {
                        raw->error = "The selected raster is absent from the frozen document snapshot.";
                    } else {
                        std::vector<uint8_t> rgba8;
                        raster::RasterPixelView source_view;
                        const auto rgba16 = image->rgba16le_pixels();
                        if (!rgba16.empty()) {
                            source_view = {rgba16, static_cast<uint32_t>(image->width_px),
                                static_cast<uint32_t>(image->height_px),
                                static_cast<uint64_t>(image->width_px) * 8,
                                raster::RasterPixelEncoding::SRgbRgba16LEStraightAlpha};
                        } else {
                            const auto packed = image->rgba_pixels();
                            const uint64_t byte_count = static_cast<uint64_t>(packed.size()) * 4;
                            if (byte_count > SIZE_MAX || !wait_for_memory(byte_count)) {
                                raw->cancelled = cancelled();
                                raw->error = raw->cancelled
                                    ? "Direct test printing was cancelled."
                                    : "Waiting for system memory was cancelled or timed out.";
                            } else {
                                rgba8.resize(static_cast<size_t>(byte_count));
                                for (size_t i = 0; i < packed.size(); ++i) {
                                    if ((i & 0x3fffu) == 0 && cancelled()) {
                                        raw->cancelled = true;
                                        break;
                                    }
                                    rgba8[i * 4] = static_cast<uint8_t>(packed[i] >> 24);
                                    rgba8[i * 4 + 1] = static_cast<uint8_t>(packed[i] >> 16);
                                    rgba8[i * 4 + 2] = static_cast<uint8_t>(packed[i] >> 8);
                                    rgba8[i * 4 + 3] = static_cast<uint8_t>(packed[i]);
                                }
                                source_view = {rgba8, static_cast<uint32_t>(image->width_px),
                                    static_cast<uint32_t>(image->height_px),
                                    static_cast<uint64_t>(image->width_px) * 4,
                                    raster::RasterPixelEncoding::SRgbRgba8StraightAlpha};
                            }
                        }
                        if (raw->error.empty() && !cancelled()) {
                            production::print::TestSheetRenderOptions options;
                            options.memory_limit_bytes = 512ull * 1024ull * 1024ull;
                            options.wait_for_memory_headroom = wait_for_memory;
                            options.cancelled = cancelled;
                            options.progress = [raw, notify_window](
                                const production::print::TestSheetRenderProgress& progress) {
                                    raw->completed_elements.store(progress.completed_elements,
                                        std::memory_order_release);
                                    raw->total_elements.store(progress.total_elements,
                                        std::memory_order_release);
                                    (void)PostMessageW(notify_window,
                                        kTestSheetWorkerMessage, 0, 0);
                                };
                            auto rendered = production::print::render_test_sheet(
                                plan, source_view, options);
                            if (!rendered.succeeded()) {
                                raw->cancelled = rendered.status ==
                                    production::print::TestSheetRenderStatus::Cancelled;
                                raw->error = rendered.diagnostic;
                            } else if (!cancelled()) {
                                const auto& sheet = rendered.sheet;
                                const uint64_t pixels =
                                    static_cast<uint64_t>(sheet.width_px) * sheet.height_px;
                                const bool dimensions_supported = sheet.width_px != 0 &&
                                    sheet.height_px != 0 &&
                                    sheet.width_px <= static_cast<uint32_t>(
                                        (std::numeric_limits<int>::max)()) &&
                                    sheet.height_px <= static_cast<uint32_t>(
                                        (std::numeric_limits<int>::max)()) &&
                                    sheet.width_px <= static_cast<uint32_t>(
                                        (std::numeric_limits<long>::max)()) &&
                                    sheet.height_px <= static_cast<uint32_t>(
                                        (std::numeric_limits<long>::max)());
                                const bool pixel_count_supported = pixels <= SIZE_MAX / 4 &&
                                    pixels <= (std::numeric_limits<uint64_t>::max)() / 4 &&
                                    sheet.image.row_stride ==
                                        static_cast<uint64_t>(sheet.width_px) * 4 &&
                                    sheet.image.encoding ==
                                        raster::RasterPixelEncoding::SRgbRgba8StraightAlpha &&
                                    sheet.image.bytes.size() == static_cast<size_t>(pixels * 4);
                                const uint64_t bytes = pixel_count_supported ? pixels * 4 : 0;
                                if (!dimensions_supported || !pixel_count_supported ||
                                    !wait_for_memory(bytes)) {
                                    raw->cancelled = cancelled();
                                    raw->error = raw->cancelled
                                        ? "Direct test printing was cancelled."
                                        : "The printer staging surface has invalid dimensions or exceeds available memory.";
                                } else {
                                    std::vector<uint8_t> bgra(static_cast<size_t>(bytes));
                                    for (size_t i = 0; i < static_cast<size_t>(pixels); ++i) {
                                        if ((i & 0x3fffu) == 0 && cancelled()) break;
                                        bgra[i * 4] = sheet.image.bytes[i * 4 + 2];
                                        bgra[i * 4 + 1] = sheet.image.bytes[i * 4 + 1];
                                        bgra[i * 4 + 2] = sheet.image.bytes[i * 4];
                                        bgra[i * 4 + 3] = 255;
                                    }
                                    if (cancelled()) {
                                        raw->cancelled = true;
                                    } else if (SetICMMode(raw->printer_dc, ICM_QUERY) != ICM_ON) {
                                        raw->error = "Printer color management was disabled before the job was submitted.";
                                    } else {
                                        BITMAPV5HEADER bitmap{};
                                        bitmap.bV5Size = sizeof(bitmap);
                                        bitmap.bV5Width = static_cast<LONG>(sheet.width_px);
                                        bitmap.bV5Height = -static_cast<LONG>(sheet.height_px);
                                        bitmap.bV5Planes = 1;
                                        bitmap.bV5BitCount = 32;
                                        bitmap.bV5Compression = BI_BITFIELDS;
                                        bitmap.bV5RedMask = 0x00ff0000;
                                        bitmap.bV5GreenMask = 0x0000ff00;
                                        bitmap.bV5BlueMask = 0x000000ff;
                                        bitmap.bV5AlphaMask = 0xff000000;
                                        bitmap.bV5CSType = LCS_sRGB;
                                        bitmap.bV5Intent = LCS_GM_IMAGES;
                                        DOCINFOW document_info{};
                                        document_info.cbSize = sizeof(document_info);
                                        document_info.lpszDocName = vietnamese
                                            ? L"PikDraw - Bang in thu"
                                            : L"PikDraw - Raster Test Sheet";
                                        const int job = StartDocW(raw->printer_dc,
                                                                  &document_info);
                                        if (job <= 0) {
                                            raw->error = "Windows could not start the printer job.";
                                        } else {
                                            const int page_started = cancelled()
                                                ? 0 : StartPage(raw->printer_dc);
                                            const bool page_color_ready = page_started > 0 &&
                                                SetICMMode(raw->printer_dc, ICM_ON) != 0 &&
                                                SetICMMode(raw->printer_dc, ICM_QUERY) == ICM_ON;
                                            if (!page_color_ready) {
                                                (void)AbortDoc(raw->printer_dc);
                                                raw->cancelled = cancelled();
                                                if (!raw->cancelled)
                                                    raw->error = page_started <= 0
                                                        ? "Windows could not start the printer page."
                                                        : "Windows could not re-enable ICM after StartPage; the page was not submitted.";
                                            } else {
                                                const int full_width = GetDeviceCaps(
                                                    raw->printer_dc, PHYSICALWIDTH);
                                                const int full_height = GetDeviceCaps(
                                                    raw->printer_dc, PHYSICALHEIGHT);
                                                const int offset_x = GetDeviceCaps(
                                                    raw->printer_dc, PHYSICALOFFSETX);
                                                const int offset_y = GetDeviceCaps(
                                                    raw->printer_dc, PHYSICALOFFSETY);
                                                bool page_failed = full_width <= 0 ||
                                                    full_height <= 0 || offset_x < 0 ||
                                                    offset_y < 0 || offset_x >= full_width ||
                                                    offset_y >= full_height || cancelled();
                                                int copied = 0;
                                                if (!page_failed) {
                                                    auto* bitmap_info =
                                                        reinterpret_cast<BITMAPINFO*>(&bitmap);
                                                    copied = StretchDIBits(raw->printer_dc,
                                                        -offset_x, -offset_y, full_width,
                                                        full_height, 0, 0,
                                                        static_cast<int>(sheet.width_px),
                                                        static_cast<int>(sheet.height_px),
                                                        bgra.data(), bitmap_info,
                                                        DIB_RGB_COLORS, SRCCOPY);
                                                    page_failed = cancelled() ||
                                                        copied == static_cast<int>(GDI_ERROR) || copied == 0;
                                                }
                                                if (!page_failed && EndPage(raw->printer_dc) <= 0)
                                                    page_failed = true;
                                                if (!page_failed && cancelled())
                                                    page_failed = true;
                                                if (!page_failed && EndDoc(raw->printer_dc) <= 0)
                                                    page_failed = true;
                                                if (page_failed) {
                                                    (void)AbortDoc(raw->printer_dc);
                                                    raw->cancelled = cancelled();
                                                    if (!raw->cancelled)
                                                        raw->error = "The Windows printer driver rejected the test-sheet page.";
                                                } else {
                                                    raw->success = true;
                                                    raw->width_px = sheet.width_px;
                                                    raw->height_px = sheet.height_px;
                                                    raw->ruler_mm = sheet.one_hundred_mm_ruler_actual_mm;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        } else if (cancelled()) {
                            raw->cancelled = true;
                        }
                    }
                } catch (const std::exception& exception) {
                    std::lock_guard lock(raw->result_mutex);
                    raw->error = exception.what();
                } catch (...) {
                    std::lock_guard lock(raw->result_mutex);
                    raw->error = "Unexpected failure while preparing direct test printing.";
                }
                if (raw->printer_dc) {
                    DeleteDC(raw->printer_dc);
                    raw->printer_dc = nullptr;
                }
                raw->finished.store(true, std::memory_order_release);
                (void)PostMessageW(notify_window, kTestSheetWorkerMessage, 1, 0);
            });
    } catch (const std::exception& exception) {
        DeleteDC(printer_dc);
        session->printer_dc = nullptr;
        test_sheet_job_.reset();
        MessageBoxW(hwnd_, NativeWindow::utf8_to_wide(exception.what()).c_str(),
            language_ == UILanguage::Vietnamese ? L"Không khởi chạy được tác vụ in thử" : L"Could not start test-print job",
            MB_OK | MB_ICONERROR);
        return;
    }
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Đang dựng và gửi bảng thử qua Windows WCS · Esc để hủy · chưa xác nhận màu bản in"
        : "Rendering and sending the test sheet through Windows WCS · Esc cancels · physical color is not verified";
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativeWindow::request_cancel_test_sheet_job() {
    if (!test_sheet_job_ || test_sheet_job_->finished.load(std::memory_order_acquire)) return;
    test_sheet_job_->cancel_requested.store(true, std::memory_order_release);
    test_sheet_job_->worker.request_stop();
    const bool direct_print = !test_sheet_job_->printer_name.empty();
    status_msg_ = language_ == UILanguage::Vietnamese
        ? (direct_print ? "Đang hủy job in thử…" : "Đang hủy dựng bảng in thử…")
        : (direct_print ? "Cancelling test-print job…" : "Cancelling test-sheet render…");
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativeWindow::cancel_test_sheet_job() {
    if (!test_sheet_job_) return;
    request_cancel_test_sheet_job();
    if (test_sheet_job_->worker.joinable()) test_sheet_job_->worker.join();
    test_sheet_job_.reset();
}

void NativeWindow::handle_test_sheet_worker_message(WPARAM kind) {
    if (!test_sheet_job_) return;
    auto session = test_sheet_job_;
    if (kind == 0 && !session->finished.load(std::memory_order_acquire)) {
        if (session->waiting_for_memory.load(std::memory_order_acquire)) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Bảng in thử đang chờ RAM xuống dưới 95% · Esc để hủy"
                : "Test sheet waiting for RAM below 95% · Esc cancels";
        } else {
            const auto completed = session->completed_elements.load(std::memory_order_acquire);
            const auto total = session->total_elements.load(std::memory_order_acquire);
            status_msg_ = (language_ == UILanguage::Vietnamese
                ? "Đang dựng bảng in thử " : "Rendering test sheet ") +
                std::to_string(completed) + "/" + std::to_string(total);
        }
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (!session->finished.load(std::memory_order_acquire)) return;
    if (session->worker.joinable()) session->worker.join();
    test_sheet_job_.reset();
    std::string error;
    {
        std::lock_guard lock(session->result_mutex);
        error = session->error;
    }
    if (session->success) {
        if (session->printer_name.empty()) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đã xuất TIFF bảng in thử · thước 100 mm = " +
                    std::to_string(session->ruler_mm) + " mm"
                : "Test sheet TIFF exported · 100 mm ruler renders as " +
                    std::to_string(session->ruler_mm) + " mm";
        } else {
            const auto profile_label = session->destination_profile_sha256.empty()
                ? std::string{} : " · output profile SHA-256 " +
                    session->destination_profile_sha256.substr(0, 16) + "…";
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đã gửi bảng thử tới printer spooler · profile đích " +
                    (session->destination_profile_sha256.empty() ? "chưa xác định" :
                     session->destination_profile_sha256.substr(0, 16) + "…") +
                    " · màu vật lý chưa được đo"
                : "Test sheet sent to the printer spooler" + profile_label +
                    " · physical color has not been measured";
        }
    } else if (session->cancelled) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? (session->printer_name.empty()
                ? "Đã hủy xuất bảng in thử" : "Đã hủy job in thử")
            : (session->printer_name.empty()
                ? "Test-sheet export cancelled" : "Test-print job cancelled");
    } else {
        const bool direct_print = !session->printer_name.empty();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? (direct_print ? "Gửi bảng in thử thất bại" : "Xuất bảng in thử thất bại")
            : (direct_print ? "Test-print job failed" : "Test-sheet export failed");
        std::wstring message = NativeWindow::utf8_to_wide(
            error.empty()
                ? (direct_print ? "The test sheet could not be sent to the printer."
                                : "The test sheet could not be written.")
                : error);
        MessageBoxW(hwnd_, message.c_str(),
            language_ == UILanguage::Vietnamese
                ? (direct_print ? L"Lỗi gửi bảng in thử" : L"Lỗi xuất bảng in thử")
                : (direct_print ? L"Test-print error" : L"Test-sheet export error"),
            MB_OK | MB_ICONWARNING);
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}
#endif

} // namespace pikdraw::desktop
