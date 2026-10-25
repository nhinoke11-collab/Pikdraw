#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/document/storage/save_recovery.hpp"
#include "pikdraw/document/storage/native_file.hpp"
#include "pikdraw/raster/raster_source_restore.hpp"
#include "../utf8_path.hpp"
#include "../performance/canvas_trace.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <commdlg.h>
#include <objbase.h>
#endif

namespace pikdraw::desktop {

#ifdef _WIN32
struct AutosaveJobSession {
    std::string tab_id;
    std::string document_id;
    std::string recovery_directory;
    uint64_t epoch{0};
    uint64_t revision{0};
    std::atomic_bool completed{false};
    bool success{false};
    std::string error;
    std::thread worker;
};

struct NativeFileJobSession {
    enum class Kind { Save, Open, Recovery };
    Kind kind{Kind::Save};
    std::string path;
    document::RecoveryInfo recovery_info;
    std::string tab_id;
    std::string document_id;
    uint64_t epoch{0};
    uint64_t revision{0};
    uint64_t view_generation{0};
    uint64_t trace_event_id{0};
    document::SaveResult save_result;
    std::shared_ptr<document::DocumentSnapshot> loaded_snapshot;
    bool raster_provider_restore_warning{false};
    std::string error;
    std::atomic_bool cancel_requested{false};
    std::atomic_bool completed{false};
    std::thread worker;
};

namespace {

[[maybe_unused]] std::string wide_to_utf8(const wchar_t* value) {
    if (!value || !*value)
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 1)
        return {};
    std::string result(static_cast<size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
                            result.data(), size, nullptr, nullptr) == 0)
        return {};
    result.pop_back();
    return result;
}

constexpr wchar_t kPikdrawFilter[] =
    L"PikDraw document (*.pr)\0*.pr\0All files\0*.*\0\0";

void restore_runtime_raster_sources(NativeFileJobSession& job) {
    if (!job.loaded_snapshot ||
        job.cancel_requested.load(std::memory_order_acquire))
        return;
    const auto restored = raster::restore_embedded_source_providers(
        job.loaded_snapshot,
        [&job] {
            return job.cancel_requested.load(std::memory_order_acquire);
        });
    if (restored) {
        job.loaded_snapshot = restored.snapshot;
        return;
    }
    if (job.cancel_requested.load(std::memory_order_acquire)) {
        job.loaded_snapshot.reset();
        return;
    }
    // Embedded source bytes in the validated .pr remain authoritative. A failed
    // derived provider cache must not make an otherwise valid document unreadable.
    job.raster_provider_restore_warning = true;
}

}

std::string NativeWindow::recovery_directory_for_tab(DocumentTab& tab) {
    const auto epoch = tab.root->document().epoch();
    if (!tab.recovery_directory.empty() && tab.recovery_epoch == epoch)
        return tab.recovery_directory;
    const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (!size) return {};
    std::wstring local(size, L'\0');
    const auto copied = GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), size);
    if (!copied || copied >= size) return {};
    local.resize(copied);
    GUID token{};
    if (FAILED(CoCreateGuid(&token))) return {};
    wchar_t guid[40]{};
    if (!StringFromGUID2(token, guid, 40)) return {};
    const auto directory = std::filesystem::path(local) / L"PikDraw" / L"Recovery" / guid;
    const auto utf8 = directory.u8string();
    tab.recovery_directory.assign(utf8.begin(), utf8.end());
    tab.recovery_epoch = epoch;
    tab.recovery_written = false;
    return tab.recovery_directory;
}

void NativeWindow::autosave_documents() {
    using document::SaveRecoveryService;
    drain_autosave_jobs(false);
    for (auto& tab : tabs_) {
        auto& doc = tab.root->document();
        if (!doc.is_dirty() || (tab.recovery_written && tab.recovery_epoch == doc.epoch()
            && tab.recovery_revision == doc.revision())) continue;
        if (autosave_jobs_.size() >= 8) break;
        if (std::any_of(autosave_jobs_.begin(), autosave_jobs_.end(), [&](const auto& job) {
                return job->tab_id == tab.id && !job->completed.load(std::memory_order_acquire);
            })) continue;
        const auto directory = recovery_directory_for_tab(tab);
        if (directory.empty()) continue;
        const auto snapshot = doc.current_snapshot();
        if (!snapshot) continue;
        auto job = std::make_shared<AutosaveJobSession>();
        job->tab_id = tab.id;
        job->document_id = doc.id();
        job->recovery_directory = directory;
        job->epoch = snapshot->epoch();
        job->revision = snapshot->revision();
        autosave_jobs_.push_back(job);
        auto* job_state = job.get();
        const auto notify_window = hwnd_;
        try {
            job->worker = std::thread([job_state, snapshot, directory, document_id = doc.id(),
                                       notify_window] {
                (void)SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
                const auto previous = SaveRecoveryService::find_latest_checkpoint(directory, document_id);
                std::string error;
                const bool success = SaveRecoveryService::write_recovery_checkpoint(
                    directory, document_id, *snapshot, &error);
                if (success && previous) {
                    std::error_code ignored;
                    std::filesystem::remove(detail::path_from_utf8(previous->path), ignored);
                }
                job_state->success = success;
                job_state->error = std::move(error);
                job_state->completed.store(true, std::memory_order_release);
                if (notify_window)
                    (void)PostMessageW(notify_window, kAutosaveWorkerMessage, 0, 0);
            });
        } catch (const std::exception& error) {
            job->success = false;
            job->error = error.what();
            job->completed.store(true, std::memory_order_release);
            drain_autosave_jobs(false);
        }
    }
}

void NativeWindow::handle_autosave_worker_message() {
    drain_autosave_jobs(true);
}

void NativeWindow::drain_autosave_jobs(bool reschedule_latest) {
    bool needs_latest = false;
    for (auto it = autosave_jobs_.begin(); it != autosave_jobs_.end();) {
        const auto& job = *it;
        if (!job->completed.load(std::memory_order_acquire)) {
            ++it;
            continue;
        }
        if (job->worker.joinable()) job->worker.join();
        const auto tab = std::find_if(tabs_.begin(), tabs_.end(), [&](const auto& candidate) {
            return candidate.id == job->tab_id;
        });
        if (tab != tabs_.end() && tab->root) {
            auto& doc = tab->root->document();
            if (job->success) {
                if (doc.id() == job->document_id && doc.epoch() == job->epoch) {
                    if (!doc.is_dirty()) {
                        document::SaveRecoveryService::cleanup_checkpoints(
                            job->recovery_directory, job->document_id);
                        tab->recovery_written = false;
                    } else if (doc.revision() == job->revision) {
                        tab->recovery_epoch = job->epoch;
                        tab->recovery_revision = job->revision;
                        tab->recovery_written = true;
                    } else {
                        needs_latest = true;
                    }
                } else if (doc.is_dirty()) {
                    needs_latest = true;
                }
            } else {
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Không thể tạo bản phục hồi tự động" : "Could not write auto-recovery checkpoint";
            }
        }
        it = autosave_jobs_.erase(it);
        if (hwnd_ && IsWindow(hwnd_)) InvalidateRect(hwnd_, nullptr, FALSE);
    }
    if (reschedule_latest && needs_latest) autosave_documents();
}

void NativeWindow::join_autosave_jobs() noexcept {
    for (const auto& job : autosave_jobs_)
        if (job && job->worker.joinable()) job->worker.join();
    drain_autosave_jobs(false);
}

void NativeWindow::start_native_save(std::string path) {
    if (native_file_job_pending_) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đang xử lý một thao tác tệp; hãy chờ hoàn tất"
            : "A file operation is already in progress";
        return;
    }
    auto& tab = active_tab();
    const auto snapshot = tab.root->document().current_snapshot();
    if (!snapshot || path.empty()) return;

    auto job = std::make_shared<NativeFileJobSession>();
    job->kind = NativeFileJobSession::Kind::Save;
    job->path = std::move(path);
    job->tab_id = tab.id;
    job->document_id = tab.root->document().id();
    job->epoch = snapshot->epoch();
    job->revision = snapshot->revision();
    job->view_generation = view_generation_;
    job->trace_event_id = next_trace_event_id_++;
    native_file_job_ = job;
    native_file_job_pending_ = true;
    native_file_job_user_activity_ = false;
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Đang lưu snapshot tài liệu…" : "Saving document snapshot…";
    record_performance_event(CanvasTraceKind::NativeSaveBegin, job->trace_event_id);
    const auto notify_window = hwnd_;
    try {
        native_file_job_->worker = std::thread([this, job, snapshot, notify_window] {
            (void)SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            try {
                job->save_result = document::SaveRecoveryService::atomic_save(
                    job->path, *snapshot);
            } catch (const std::exception& error) {
                job->save_result.error_message = error.what();
            } catch (...) {
                job->save_result.error_message = "Unknown native save failure";
            }
            record_performance_event(CanvasTraceKind::NativeSaveEnd, job->trace_event_id);
            job->completed.store(true, std::memory_order_release);
            if (notify_window)
                (void)PostMessageW(notify_window, kNativeFileWorkerMessage, 0, 0);
        });
    } catch (const std::exception& error) {
        native_file_job_.reset();
        native_file_job_pending_ = false;
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không thể khởi chạy tác vụ lưu nền"
            : "Could not start the background save";
        record_performance_event(CanvasTraceKind::NativeSaveEnd, job->trace_event_id);
        (void)error;
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativeWindow::start_native_open(std::string path) {
    if (native_file_job_pending_) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đang xử lý một thao tác tệp; hãy chờ hoàn tất"
            : "A file operation is already in progress";
        return;
    }
    if (path.empty() ||
        !document::is_native_document_path(detail::path_from_utf8(path)))
        return;

    auto& tab = active_tab();
    const auto snapshot = tab.root->document().current_snapshot();
    if (!snapshot) return;
    auto job = std::make_shared<NativeFileJobSession>();
    job->kind = NativeFileJobSession::Kind::Open;
    job->path = std::move(path);
    job->tab_id = tab.id;
    job->document_id = tab.root->document().id();
    job->epoch = snapshot->epoch();
    job->revision = snapshot->revision();
    job->view_generation = view_generation_;
    job->trace_event_id = next_trace_event_id_++;
    native_file_job_ = job;
    native_file_job_pending_ = true;
    native_file_job_user_activity_ = false;
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Đang đọc và kiểm tra tệp .pr…" : "Reading and validating .pr file…";
    record_performance_event(CanvasTraceKind::NativeOpenBegin, job->trace_event_id);
    const auto notify_window = hwnd_;
    try {
        native_file_job_->worker = std::thread([this, job, notify_window] {
            (void)SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            try {
                job->loaded_snapshot = document::SaveRecoveryService::load(
                    job->path, &job->error, &job->cancel_requested);
            } catch (const std::exception& error) {
                job->error = error.what();
            } catch (...) {
                job->error = "Unknown native open failure";
            }
            restore_runtime_raster_sources(*job);
            record_performance_event(CanvasTraceKind::NativeOpenEnd, job->trace_event_id);
            job->completed.store(true, std::memory_order_release);
            if (notify_window)
                (void)PostMessageW(notify_window, kNativeFileWorkerMessage, 0, 0);
        });
    } catch (const std::exception& error) {
        native_file_job_.reset();
        native_file_job_pending_ = false;
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không thể khởi chạy tác vụ mở nền"
            : "Could not start the background open";
        record_performance_event(CanvasTraceKind::NativeOpenEnd, job->trace_event_id);
        (void)error;
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativeWindow::start_recovery_load(document::RecoveryInfo candidate) {
    if (native_file_job_pending_ || candidate.path.empty()) {
        if (native_file_job_pending_)
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đang xử lý một thao tác tệp; hãy chờ hoàn tất"
                : "A file operation is already in progress";
        return;
    }
    auto job = std::make_shared<NativeFileJobSession>();
    job->kind = NativeFileJobSession::Kind::Recovery;
    job->path = candidate.path;
    job->recovery_info = std::move(candidate);
    job->trace_event_id = next_trace_event_id_++;
    native_file_job_ = job;
    native_file_job_pending_ = true;
    native_file_job_user_activity_ = false;
    close_recovery_modal();
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Đang nạp bản tự lưu trong nền…"
        : "Loading recovery checkpoint in the background…";
    record_performance_event(CanvasTraceKind::NativeOpenBegin, job->trace_event_id);
    const auto notify_window = hwnd_;
    try {
        native_file_job_->worker = std::thread([this, job, notify_window] {
            (void)SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            try {
                job->loaded_snapshot = document::SaveRecoveryService::load(
                    job->path, &job->error, &job->cancel_requested);
            } catch (const std::exception& error) {
                job->error = error.what();
            } catch (...) {
                job->error = "Unknown recovery load failure";
            }
            restore_runtime_raster_sources(*job);
            record_performance_event(CanvasTraceKind::NativeOpenEnd,
                                     job->trace_event_id);
            job->completed.store(true, std::memory_order_release);
            if (notify_window)
                (void)PostMessageW(notify_window, kNativeFileWorkerMessage, 0, 0);
        });
    } catch (const std::exception& error) {
        native_file_job_.reset();
        native_file_job_pending_ = false;
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không thể khởi chạy tác vụ phục hồi nền"
            : "Could not start the background recovery load";
        record_performance_event(CanvasTraceKind::NativeOpenEnd, job->trace_event_id);
        (void)error;
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativeWindow::handle_native_file_worker_message() {
    const auto job = native_file_job_;
    if (!job || !job->completed.load(std::memory_order_acquire)) return;
    if (job->worker.joinable()) job->worker.join();
    native_file_job_.reset();
    native_file_job_pending_ = false;
    const bool user_activity_during_open = native_file_job_user_activity_;
    native_file_job_user_activity_ = false;

    auto tab = std::find_if(tabs_.begin(), tabs_.end(), [&](const auto& candidate) {
        return candidate.id == job->tab_id;
    });
    if (job->kind == NativeFileJobSession::Kind::Save) {
        if (!job->save_result.success) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Không thể lưu tài liệu .pr" : "Could not save the .pr document";
        } else if (tab == tabs_.end() || !tab->root ||
                   tab->root->document().id() != job->document_id ||
                   tab->root->document().epoch() != job->epoch) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đã ghi snapshot; tài liệu trong tab đã thay đổi"
                : "Snapshot saved; the tab document has since changed";
        } else {
            auto& doc = tab->root->document();
            doc.mark_saved(job->epoch, job->revision);
            tab->file_path = job->path;
            if (active_tab_index_ < tabs_.size() &&
                tab == tabs_.begin() + static_cast<std::ptrdiff_t>(active_tab_index_))
                document_path_ = job->path;
            if (!doc.is_dirty()) {
                const auto directory = recovery_directory_for_tab(*tab);
                if (!directory.empty())
                    document::SaveRecoveryService::cleanup_checkpoints(
                        directory, job->document_id);
                tab->recovery_written = false;
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Đã lưu tài liệu PikDraw" : "PikDraw document saved";
            } else {
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Đã lưu bản snapshot; các chỉnh sửa mới hơn vẫn chưa lưu"
                    : "Snapshot saved; newer document edits remain unsaved";
            }
        }
    } else if (job->kind == NativeFileJobSession::Kind::Recovery) {
        const auto& candidate = job->recovery_info;
        const auto& snapshot = job->loaded_snapshot;
        if (job->cancel_requested.load(std::memory_order_acquire)) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đã hủy nạp bản tự lưu"
                : "Recovery load cancelled";
        } else if (!snapshot || snapshot->document_id() != candidate.document_id ||
                   snapshot->revision() != candidate.revision ||
                   snapshot->epoch() != candidate.epoch) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Bản tự lưu không còn hợp lệ; hãy quét lại danh sách"
                : "This checkpoint is no longer valid; scan the list again";
            scan_recovery_checkpoints(false, true);
        } else {
            try {
                std::ostringstream title;
                title << (language_ == UILanguage::Vietnamese ? "Phục hồi " : "Recovered ")
                      << candidate.timestamp;
                create_new_tab(title.str(), snapshot->page().width_mm,
                               snapshot->page().height_mm);
                if (!root().document().reset_from_snapshot(snapshot)) {
                    close_tab(active_tab_index_);
                    status_msg_ = language_ == UILanguage::Vietnamese
                        ? "Không thể mở bản tự lưu" : "Could not open the recovery checkpoint";
                } else {
                    sync_display_unit_from_active_page();
                    root().document().mark_recovered_unsaved();
                    root().vector_controller().selection().clear();
                    const auto parent = detail::path_from_utf8(candidate.path)
                                            .parent_path().u8string();
                    active_tab().recovery_directory.assign(parent.begin(), parent.end());
                    active_tab().recovery_epoch = root().document().epoch();
                    active_tab().recovery_revision = root().document().revision();
                    active_tab().recovery_written = true;
                    active_tab().file_path.clear();
                    document_path_.clear();
                    restored_recovery_paths_.insert(candidate.path);
                    fit_page_to_canvas();
                    status_msg_ = job->raster_provider_restore_warning
                        ? (language_ == UILanguage::Vietnamese
                            ? "Đã phục hồi; nguồn ảnh dùng dữ liệu nhúng do cache chưa khôi phục"
                            : "Recovered; embedded image data is active because its cache could not be restored")
                        : (language_ == UILanguage::Vietnamese
                            ? "Đã phục hồi vào tab chưa lưu · dùng Lưu thành để chọn tệp chính thức"
                            : "Recovered to an unsaved tab · use Save As to choose its file");
                }
            } catch (...) {
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Không thể tạo tab phục hồi" : "Could not create a recovery tab";
            }
        }
    } else if (job->cancel_requested.load(std::memory_order_acquire)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã hủy thao tác mở tài liệu"
            : "Document open cancelled";
    } else if (!job->loaded_snapshot) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không thể mở và xác thực tệp .pr" : "Could not open and validate the .pr file";
    } else if (user_activity_during_open || tab == tabs_.end() ||
               tab->id != active_tab().id || !tab->root ||
               tab->root->document().id() != job->document_id ||
               tab->root->document().epoch() != job->epoch ||
               tab->root->document().revision() != job->revision ||
               view_generation_ != job->view_generation || is_dragging_ ||
               is_creating_shape_ || is_inline_text_editing_ ||
               active_num_field_ != NumericFieldId::None) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã bỏ kết quả mở vì tab hoặc tài liệu đã thay đổi trong lúc đọc"
            : "Open result discarded because the tab or document changed while loading";
    } else {
        auto& target = *tab;
        auto& doc = target.root->document();
        if (!doc.reset_from_snapshot(job->loaded_snapshot)) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Không thể áp dụng snapshot đã đọc" : "Could not apply the loaded snapshot";
        } else {
            target.root->vector_controller().on_escape();
            target.root->vector_controller().selection().clear();
            target.file_path = job->path;
            target.artboard_overview = settings_artboard_overview_choice_;
            target.active_artboard_id = document::kInvalidArtboardId;
            if (const auto snapshot = doc.current_snapshot();
                snapshot && !snapshot->active_page().artboards.empty())
                target.active_artboard_id = snapshot->active_page().artboards.front().id;
            target.page_width_mm = doc.page().width_mm;
            target.page_height_mm = doc.page().height_mm;
            document_path_ = job->path;
            target.workflow->sync_status_bar();
            sync_display_unit_from_active_page();
            set_active_tool(0);
            fit_page_to_canvas();
            update_color_preview_options();
            status_msg_ = job->raster_provider_restore_warning
                ? (language_ == UILanguage::Vietnamese
                    ? "Đã mở tài liệu; nguồn ảnh dùng dữ liệu nhúng do cache chưa khôi phục"
                    : "Document opened; embedded image data is active because its cache could not be restored")
                : (language_ == UILanguage::Vietnamese
                    ? "Đã mở tài liệu PikDraw" : "PikDraw document opened");
        }
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativeWindow::join_native_file_job() noexcept {
    if (native_file_job_ &&
        native_file_job_->kind != NativeFileJobSession::Kind::Save)
        native_file_job_->cancel_requested.store(true, std::memory_order_release);
    if (native_file_job_ && native_file_job_->worker.joinable())
        native_file_job_->worker.join();
    native_file_job_.reset();
    native_file_job_pending_ = false;
    native_file_job_user_activity_ = false;
}

void NativeWindow::request_cancel_native_open_for_tab(
    const std::string& tab_id) noexcept {
    if (native_file_job_ &&
        native_file_job_->kind == NativeFileJobSession::Kind::Open &&
        native_file_job_->tab_id == tab_id)
        native_file_job_->cancel_requested.store(true, std::memory_order_release);
}

void NativeWindow::open_document_dialog() {
    if (native_file_job_pending_) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đang xử lý một thao tác tệp; hãy chờ hoàn tất"
            : "A file operation is already in progress";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (is_inline_text_editing_) commit_inline_text_editing();
    std::wstring path(32768, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd_;
    dialog.lpstrFilter = language_ == UILanguage::Vietnamese
        ? L"Định dạng có adapter mở (*.pr;*.svg;*.dxf;*.cdr;*.ai;*.pdf;*.eps;*.png;*.jpg;*.jpeg;*.tif;*.tiff)\0*.pr;*.svg;*.dxf;*.cdr;*.ai;*.pdf;*.eps;*.png;*.jpg;*.jpeg;*.tif;*.tiff\0"
          L"PDF/AI: path và chữ của một trang (subset) (*.ai;*.pdf)\0*.ai;*.pdf\0"
          L"EPS: path vector PostScript trong subset an toàn (*.eps)\0*.eps\0"
          L"CorelDRAW CDR: path/chữ/layer subset (*.cdr)\0*.cdr\0"
          L"Tài liệu PikDraw (*.pr)\0*.pr\0"
          L"Đồ họa Vector (*.svg;*.dxf;*.cdr;*.ai;*.pdf;*.eps)\0*.svg;*.dxf;*.cdr;*.ai;*.pdf;*.eps\0"
          L"Hình ảnh raster (*.png;*.jpg;*.jpeg;*.tif;*.tiff)\0*.png;*.jpg;*.jpeg;*.tif;*.tiff\0"
          L"Tất cả tệp (*.*)\0*.*\0\0"
        : L"Open formats with adapters (*.pr;*.svg;*.dxf;*.cdr;*.ai;*.pdf;*.eps;*.png;*.jpg;*.jpeg;*.tif;*.tiff)\0*.pr;*.svg;*.dxf;*.cdr;*.ai;*.pdf;*.eps;*.png;*.jpg;*.jpeg;*.tif;*.tiff\0"
          L"PDF/AI: single-page path and text subset (*.ai;*.pdf)\0*.ai;*.pdf\0"
          L"EPS: safe editable PostScript vector path subset (*.eps)\0*.eps\0"
          L"CorelDRAW CDR path/text/layer subset (*.cdr)\0*.cdr\0"
          L"PikDraw Document (*.pr)\0*.pr\0"
          L"Vector Drawings (*.svg;*.dxf;*.cdr;*.ai;*.pdf;*.eps)\0*.svg;*.dxf;*.cdr;*.ai;*.pdf;*.eps\0"
          L"Raster Images (*.png;*.jpg;*.jpeg;*.tif;*.tiff)\0*.png;*.jpg;*.jpeg;*.tif;*.tiff\0"
          L"All Files (*.*)\0*.*\0\0";
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrDefExt = L"pr";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog))
        return;

    if (document::is_native_document_path(std::filesystem::path(path.c_str()))) {
        if (root().document().is_dirty() && root().document().current_snapshot()->object_count() > 0) {
            const auto prompt = utf8_to_wide(language_ == UILanguage::Vietnamese
                ? "Bỏ các thay đổi chưa lưu và mở tệp khác?"
                : "Discard unsaved changes and open another file?");
            const auto title = utf8_to_wide(language_ == UILanguage::Vietnamese
                ? "Mở tài liệu" : "Open document");
            if (MessageBoxW(hwnd_, prompt.c_str(), title.c_str(), MB_YESNO | MB_ICONWARNING) != IDYES)
                return;
        }

        const auto utf8_path = wide_to_utf8(path.c_str());
        if (utf8_path.empty()) return;
        cancel_canvas_interaction();
        start_native_open(utf8_path);
        return;
    }

    import_file_to_tab(path.c_str(), true);
}

void NativeWindow::save_document_dialog(bool force_save_as) {
    if (native_file_job_pending_) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đang xử lý một thao tác tệp; hãy chờ hoàn tất"
            : "A file operation is already in progress";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (is_inline_text_editing_) commit_inline_text_editing();
    std::string path = active_tab().file_path;
    if (path.empty() || force_save_as) {
        std::wstring wide_path(32768, L'\0');
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = hwnd_;
        dialog.lpstrFilter = kPikdrawFilter;
        dialog.lpstrFile = wide_path.data();
        dialog.nMaxFile = static_cast<DWORD>(wide_path.size());
        dialog.lpstrDefExt = L"pr";
        dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameW(&dialog))
            return;
        path = wide_to_utf8(wide_path.c_str());
    }

    if (!path.empty() && !document::is_native_document_path(detail::path_from_utf8(path))) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "File thiết kế phải có đuôi .pr" : "Design files must use the .pr extension";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (path.empty()) return;
    start_native_save(std::move(path));
}

void NativeWindow::export_svg_dialog(bool selected_objects, bool all_pages,
                                     bool active_artboard) {
    std::wstring path(32768, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd_;
    dialog.lpstrFilter = L"SVG vector (*.svg)\0*.svg\0\0";
    dialog.lpstrTitle = active_artboard
        ? (language_ == UILanguage::Vietnamese ? L"Xuất Artboard đang chọn thành SVG" : L"Export active Artboard as SVG")
        : all_pages
        ? (language_ == UILanguage::Vietnamese ? L"Xuất từng trang thành tệp SVG riêng" : L"Export each page to a separate SVG")
        : selected_objects
        ? (language_ == UILanguage::Vietnamese ? L"Xuất đối tượng đang chọn thành SVG" : L"Export selected objects as SVG")
        : (language_ == UILanguage::Vietnamese ? L"Xuất trang hiện tại thành SVG" : L"Export active page as SVG");
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrDefExt = L"svg";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
        (all_pages ? 0 : OFN_OVERWRITEPROMPT);
    if (GetSaveFileNameW(&dialog))
        export_svg_to(wide_to_utf8(path.c_str()), selected_objects,
                      !all_pages, all_pages, active_artboard);
}

#endif

} // namespace pikdraw::desktop
