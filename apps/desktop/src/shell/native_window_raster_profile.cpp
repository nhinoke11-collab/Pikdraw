#include "pikdraw/desktop/shell/native_window.hpp"

#include "pikdraw/color/preview/icc_preview.hpp"
#include "pikdraw/color/preview/profile_color_transform.hpp"
#include "pikdraw/color/profiles/profile_resource.hpp"
#include "pikdraw/document/commands/raster_profile_commands.hpp"
#include "pikdraw/raster/profile_conversion.hpp"
#include "pikdraw/workers/job_object.hpp"
#include "pikdraw/color/profiles/profile_registry.hpp"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#ifdef _WIN32
#include <commdlg.h>
#include <windows.h>
#endif

namespace pikdraw::desktop {

#ifdef _WIN32

namespace {

std::optional<std::filesystem::path> choose_raster_rgb_profile(HWND owner,
                                                               bool vi) {
    wchar_t path[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"RGB ICC profiles (*.icc;*.icm)\0*.icc;*.icm\0All files (*.*)\0*.*\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = static_cast<DWORD>(sizeof(path) / sizeof(path[0]));
    dialog.lpstrTitle = vi ? L"Chọn profile ICC RGB cho ảnh" : L"Choose RGB ICC Profile for Bitmap";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetOpenFileNameW(&dialog)) return std::nullopt;
    return std::filesystem::path(path);
}

}  // namespace

bool NativeWindow::can_assign_selected_bitmap_profile() const {
    if (root().document().is_content_locked() || raster_export_job_active() ||
        raster_profile_conversion_active())
        return false;
    const auto& selected = root().vector_controller().selection().selected_ids();
    if (selected.size() != 1) return false;
    const auto snapshot = root().document().current_snapshot();
    if (!snapshot) return false;
    const auto* value = snapshot->objects().lookup_ref(selected.front());
    const auto* image = value ? std::get_if<document::ImageObject>(value) : nullptr;
    if (!image || image->locked || image->source.preview_pixel_color_basis ==
            document::RasterPixelColorBasis::LegacyUnknown ||
        (image->source.original_data().empty() && !image->asset_source))
        return false;
    if (image->width_px <= 0 || image->height_px <= 0) return false;
    const uint64_t pixels = static_cast<uint64_t>(image->width_px) *
                            static_cast<uint64_t>(image->height_px);
    if (pixels > std::numeric_limits<size_t>::max() / sizeof(uint32_t) ||
        image->rgba_pixels().size() != static_cast<size_t>(pixels))
        return false;
    const auto rgba16 = image->rgba16le_pixels();
    return rgba16.empty() || (pixels <= std::numeric_limits<size_t>::max() / 8u &&
        rgba16.size() == static_cast<size_t>(pixels) * 8u);
}

void NativeWindow::assign_selected_bitmap_profile() {
    const bool vi = language_ == UILanguage::Vietnamese;
    if (!can_assign_selected_bitmap_profile()) {
        status_msg_ = vi
            ? "Chọn một ảnh raster có nguồn và mẫu pixel hợp lệ để gán ICC"
            : "Select one raster with a preserved source and valid pixel samples";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto request_snapshot = root().document().current_snapshot();
    const auto& selected_at_start = root().vector_controller().selection().selected_ids();
    if (!request_snapshot || selected_at_start.size() != 1) return;
    const auto image_id = selected_at_start.front();
    const auto path = raster_profile_file_picker_
        ? raster_profile_file_picker_(vi)
        : choose_raster_rgb_profile(hwnd_, vi);
    if (!path) return;
    auto loaded = color::ProfileResource::load_file(*path, color::ColorSpace::Rgb);
    if (!loaded) {
        status_msg_ = vi
            ? "Profile ICC không hợp lệ hoặc không thuộc hệ RGB"
            : "The ICC profile is invalid or is not an RGB profile";
        if (hwnd_) MessageBoxW(hwnd_, vi
            ? L"Profile ICC không hợp lệ hoặc không thuộc hệ RGB."
            : L"The ICC profile is invalid or is not an RGB profile.",
            L"PikDraw ICC", MB_OK | MB_ICONWARNING);
        return;
    }
    const auto preview_profile = color::ProfileResource::load_file(
        color::IccPreview::system_srgb_profile(), color::ColorSpace::Rgb);
    bool preview_available = preview_profile &&
        loaded.resource->identity() == preview_profile.resource->identity();
    if (preview_profile && !preview_available) {
        preview_available = static_cast<bool>(color::ProfileColorTransform::create(
            loaded.resource, preview_profile.resource,
            {color::IccRenderingIntent::RelativeColorimetric, false}));
    }
    if (!preview_available) {
        status_msg_ = vi
            ? "Windows CMM không tạo được preview ICC sang sRGB; ảnh chưa thay đổi"
            : "The Windows CMM cannot preview this ICC profile in sRGB; the image was not changed";
        if (hwnd_) MessageBoxW(hwnd_, vi
            ? L"Windows CMM không tạo được preview từ profile này sang sRGB. Ảnh chưa thay đổi."
            : L"The Windows CMM cannot preview this profile in sRGB. The image was not changed.",
            L"PikDraw ICC", MB_OK | MB_ICONWARNING);
        return;
    }

    const auto snapshot = root().document().current_snapshot();
    const auto& selected = root().vector_controller().selection().selected_ids();
    if (!snapshot || snapshot.get() != request_snapshot.get() ||
        selected.size() != 1 || selected.front() != image_id) {
        status_msg_ = vi
            ? "Đã bỏ thao tác vì tài liệu hoặc vùng chọn thay đổi"
            : "Assignment was discarded because the document or selection changed";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto* value = snapshot->objects().lookup_ref(image_id);
    const auto* image = value ? std::get_if<document::ImageObject>(value) : nullptr;
    if (!image) return;
    if (image->source.preview_pixel_color_basis ==
            document::RasterPixelColorBasis::EmbeddedProfileUnapplied &&
        image->source.icc_profile == loaded.resource->bytes()) {
        status_msg_ = vi ? "Ảnh đã dùng profile ICC này" : "The image already uses this ICC profile";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    const auto result = root().document().execute_command(
        std::make_unique<document::AssignRasterImageProfileCommand>(
            image->id, loaded.resource));
    if (!result.success) {
        status_msg_ = (vi ? "Không gán được ICC ảnh: " : "Could not assign bitmap ICC: ") +
            result.error_message;
        const auto report = utf8_to_wide(result.error_message);
        if (hwnd_) MessageBoxW(hwnd_, report.c_str(), L"PikDraw ICC", MB_OK | MB_ICONWARNING);
        return;
    }
    (void)color::ProfileRegistry::instance().register_resource(loaded.resource);
    canvas_cache_valid_ = false;
    status_msg_ = (vi ? "Đã gán ICC RGB: " : "Assigned RGB ICC: ") +
        loaded.resource->identity().name + (vi
            ? " · số kênh giữ nguyên · preview cập nhật nền · Ctrl+Z hoàn tác"
            : " · channel values preserved · preview updating · Ctrl+Z to undo");
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

struct RasterProfileConversionJobSession {
    std::jthread worker;
    std::shared_ptr<const document::DocumentSnapshot> source_snapshot;
    document::ObjectId image_id{document::kInvalidObjectId};
    std::atomic<bool> finished{false};
    std::atomic<bool> cancel_requested{false};
    std::atomic<bool> waiting_for_memory{false};
    std::atomic<uint64_t> completed_pixels{0};
    uint64_t total_pixels{0};
    std::atomic<size_t> memory_requirement{0};
    bool success{false};
    bool cancelled{false};
    std::shared_ptr<const std::vector<uint32_t>> rgba8;
    std::shared_ptr<const std::vector<uint8_t>> rgba16le;
    std::mutex result_mutex;
    std::string error;
};

bool NativeWindow::raster_profile_conversion_active() const noexcept {
    return raster_profile_conversion_job_ &&
        !raster_profile_conversion_job_->finished.load(std::memory_order_acquire);
}

bool NativeWindow::can_convert_selected_bitmap_profile_to_srgb() const {
    if (root().document().is_content_locked() || raster_export_job_active() ||
        raster_profile_conversion_active())
        return false;
    const auto& selected = root().vector_controller().selection().selected_ids();
    if (selected.size() != 1) return false;
    const auto snapshot = root().document().current_snapshot();
    if (!snapshot) return false;
    const auto* value = snapshot->objects().lookup_ref(selected.front());
    const auto* image = value ? std::get_if<document::ImageObject>(value) : nullptr;
    if (!image || image->locked || image->width_px <= 0 || image->height_px <= 0 ||
        image->source.preview_pixel_color_basis !=
            document::RasterPixelColorBasis::EmbeddedProfileUnapplied)
        return false;
    const auto& profile = image->source.icc_profile;
    if (profile.size() < 128 ||
        profile.size() > document::RasterSourceMetadata::kMaxIccProfileBytes ||
        profile[16] != 'R' || profile[17] != 'G' || profile[18] != 'B' ||
        profile[19] != ' ')
        return false;
    if (image->source.original_data().empty() && !image->asset_source) return false;

    const uint64_t pixels = static_cast<uint64_t>(image->width_px) *
                            static_cast<uint64_t>(image->height_px);
    if (pixels > std::numeric_limits<size_t>::max() / sizeof(uint32_t) ||
        image->rgba_pixels().size() != static_cast<size_t>(pixels))
        return false;
    const auto high_precision = image->rgba16le_pixels();
    if (!high_precision.empty() &&
        (pixels > std::numeric_limits<size_t>::max() / 8u ||
         high_precision.size() != static_cast<size_t>(pixels) * 8u))
        return false;
    return true;
}

void NativeWindow::convert_selected_bitmap_profile_to_srgb() {
    if (!can_convert_selected_bitmap_profile_to_srgb()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chọn một ảnh đơn có ICC RGB nguồn chưa áp dụng; nguồn phải được giữ lại"
            : "Select one image with an unapplied embedded RGB profile and a preserved source";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    const auto snapshot = root().document().current_snapshot();
    const auto& selected = root().vector_controller().selection().selected_ids();
    const auto* object = snapshot->objects().lookup_ref(selected.front());
    const auto* image = object ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) return;

    auto session = std::make_shared<RasterProfileConversionJobSession>();
    session->source_snapshot = snapshot;
    session->image_id = image->id;
    session->total_pixels = static_cast<uint64_t>(image->width_px) *
                            static_cast<uint64_t>(image->height_px);
    raster_profile_conversion_job_ = session;
    const HWND notify_window = hwnd_;
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Chuyển ICC ảnh sang sRGB · Relative · BPC tắt · Esc để hủy"
        : "Converting bitmap ICC to sRGB · Relative · BPC off · Esc cancels";
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);

    session->worker = std::jthread([session, notify_window](std::stop_token stop) {
        workers::SystemMemoryPressureWaiter memory_waiter(
            workers::kSystemMemoryPressureWaitLimit,
            [&](bool is_waiting) {
                session->waiting_for_memory.store(is_waiting, std::memory_order_release);
                if (notify_window)
                    (void)PostMessageW(notify_window, kRasterProfileWorkerMessage, 0, 0);
            }, {}, {}, [&](size_t requirement) {
                session->memory_requirement.store(requirement, std::memory_order_release);
            });
        const auto cancelled = [&] {
            return stop.stop_requested() ||
                session->cancel_requested.load(std::memory_order_acquire);
        };
        const auto fail = [&](std::string message, bool was_cancelled = false) {
            std::lock_guard lock(session->result_mutex);
            session->error = std::move(message);
            session->cancelled = was_cancelled;
        };

        try {
            const auto* value = session->source_snapshot->objects().lookup_ref(session->image_id);
            const auto* source = value ? std::get_if<document::ImageObject>(value) : nullptr;
            if (!source) {
                fail("The selected image is no longer present in the captured document snapshot.");
            } else {
                const auto source16 = source->rgba16le_pixels();
                const uint64_t bytes_per_pixel = source16.empty() ? 4u : 12u;
                constexpr uint64_t scratch_reserve = 16ULL * 1024ULL * 1024ULL;
                const size_t source_metadata_bytes =
                    source->source.original_bytes.size() + source->source.icc_profile.size();
                const size_t available_for_pixels = std::numeric_limits<size_t>::max() -
                    static_cast<size_t>(scratch_reserve) - source_metadata_bytes;
                if (source_metadata_bytes > std::numeric_limits<size_t>::max() - scratch_reserve ||
                    session->total_pixels > available_for_pixels / bytes_per_pixel) {
                    fail("The required conversion memory exceeds this process address space.");
                } else {
                    const size_t required = static_cast<size_t>(session->total_pixels) *
                        static_cast<size_t>(bytes_per_pixel) + source_metadata_bytes +
                        static_cast<size_t>(scratch_reserve);
                    const auto memory = memory_waiter.WaitForHeadroom(required, stop);
                    if (memory == workers::SystemMemoryWaitResult::Cancelled || cancelled()) {
                        fail("Raster profile conversion was cancelled.", true);
                    } else if (memory == workers::SystemMemoryWaitResult::TimedOut) {
                        fail("Timed out waiting for memory headroom; no image data was changed.");
                    } else {
                        const auto srgb = color::ProfileResource::load_file(
                            color::IccPreview::system_srgb_profile(), color::ColorSpace::Rgb);
                        if (!srgb) {
                            fail("The system sRGB ICC profile failed validation.");
                        } else {
                            raster::RasterProfileConversionOptions options;
                            options.transform = {
                                color::IccRenderingIntent::RelativeColorimetric, false};
                            options.cancelled = cancelled;
                            uint64_t last_percent = 0;
                            options.progress = [&](uint64_t completed, uint64_t total) {
                                session->completed_pixels.store(completed, std::memory_order_release);
                                const uint64_t percent = total == 0 ? 100 :
                                    static_cast<uint64_t>((static_cast<long double>(completed) * 100.0L) / total);
                                if (percent != last_percent) {
                                    last_percent = percent;
                                    if (notify_window)
                                        (void)PostMessageW(notify_window,
                                            kRasterProfileWorkerMessage, 0, 0);
                                }
                            };
                            auto result = raster::convert_image_profile_to_rgb(
                                *source, srgb.resource, options);
                            if (!result.succeeded()) {
                                fail(result.diagnostic,
                                    result.status == raster::RasterProfileConversionStatus::Cancelled);
                            } else {
                                session->rgba8 = std::move(result.rgba8_preview);
                                session->rgba16le = std::move(result.rgba16le_master);
                                session->completed_pixels.store(session->total_pixels,
                                                                std::memory_order_release);
                                session->success = true;
                            }
                        }
                    }
                }
            }
        } catch (const std::exception& error) {
            fail(error.what());
        } catch (...) {
            fail("Unexpected error while converting the raster profile.");
        }
        session->waiting_for_memory.store(false, std::memory_order_release);
        session->finished.store(true, std::memory_order_release);
        if (notify_window)
            (void)PostMessageW(notify_window, kRasterProfileWorkerMessage, 1, 0);
    });
}

void NativeWindow::request_cancel_raster_profile_conversion() {
    if (!raster_profile_conversion_job_ ||
        raster_profile_conversion_job_->finished.load(std::memory_order_acquire)) return;
    raster_profile_conversion_job_->cancel_requested.store(true, std::memory_order_release);
    raster_profile_conversion_job_->worker.request_stop();
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Đang hủy chuyển profile ảnh…" : "Cancelling raster profile conversion…";
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativeWindow::cancel_raster_profile_conversion() {
    if (!raster_profile_conversion_job_) return;
    request_cancel_raster_profile_conversion();
    if (raster_profile_conversion_job_->worker.joinable())
        raster_profile_conversion_job_->worker.join();
    raster_profile_conversion_job_.reset();
}

void NativeWindow::handle_raster_profile_worker_message(WPARAM kind) {
    if (!raster_profile_conversion_job_) return;
    auto session = raster_profile_conversion_job_;
    if (kind == 0 && !session->finished.load(std::memory_order_acquire)) {
        if (session->waiting_for_memory.load(std::memory_order_acquire)) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đang chờ bộ nhớ · tự tiếp tục, tối đa 5 phút · Esc để hủy"
                : "Waiting for memory · resumes automatically, up to 5 min · Esc cancels";
        } else {
            const auto done = session->completed_pixels.load(std::memory_order_acquire);
            const auto total = session->total_pixels;
            const auto percent = total == 0 ? 0 : static_cast<uint64_t>(
                (static_cast<long double>(done) * 100.0L) / total);
            status_msg_ = (language_ == UILanguage::Vietnamese
                ? "Chuyển profile ảnh " : "Converting bitmap profile ") +
                std::to_string(percent) + "% · Esc " +
                (language_ == UILanguage::Vietnamese ? "để hủy" : "to cancel");
        }
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (session->worker.joinable()) session->worker.join();
    raster_profile_conversion_job_.reset();
    std::string error;
    {
        std::lock_guard lock(session->result_mutex);
        error = session->error;
    }
    if (!session->success) {
        status_msg_ = session->cancelled
            ? (language_ == UILanguage::Vietnamese ? "Đã hủy chuyển profile ảnh" : "Raster profile conversion cancelled")
            : (language_ == UILanguage::Vietnamese ? "Chuyển profile ảnh thất bại" : "Raster profile conversion failed");
        if (!session->cancelled && hwnd_) {
            const auto report = utf8_to_wide(error.empty()
                ? "The ICC conversion did not complete." : error);
            MessageBoxW(hwnd_, report.c_str(),
                language_ == UILanguage::Vietnamese ? L"Chuyển profile ảnh" : L"Bitmap profile conversion",
                MB_OK | MB_ICONWARNING);
        }
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    const auto current = root().document().current_snapshot();
    const auto& selected = root().vector_controller().selection().selected_ids();
    if (root().document().is_content_locked() || !current ||
        current.get() != session->source_snapshot.get() || selected.size() != 1 ||
        selected.front() != session->image_id) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã bỏ kết quả vì tài liệu hoặc vùng chọn thay đổi trong lúc chuyển"
            : "Discarded conversion because the document or selection changed";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    auto command = std::make_unique<document::ConvertRasterImageToSrgbCommand>(
        session->image_id, std::move(session->rgba8), std::move(session->rgba16le));
    const auto result = root().document().execute_command(std::move(command));
    if (!result.success) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không áp dụng được chuyển đổi ICC ảnh" : "Could not apply raster ICC conversion";
        if (hwnd_) {
            const auto report = utf8_to_wide(result.error_message);
            MessageBoxW(hwnd_, report.c_str(),
                language_ == UILanguage::Vietnamese ? L"Chuyển profile ảnh" : L"Bitmap profile conversion",
                MB_OK | MB_ICONWARNING);
        }
    } else {
        canvas_cache_valid_ = false;
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã chuyển ảnh sang sRGB · giữ ICC và nguồn gốc · Undo để hoàn tác"
            : "Bitmap converted to sRGB · source ICC and bytes retained · Undo to revert";
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

#endif

}  // namespace pikdraw::desktop
