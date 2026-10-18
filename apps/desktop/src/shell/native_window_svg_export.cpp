#include "pikdraw/desktop/shell/native_window.hpp"

#include "pikdraw/interop/adapters/svg_importer.hpp"
#include "pikdraw/interop/export/atomic_export.hpp"
#include "pikdraw/interop/export/export_options.hpp"
#include "pikdraw/interop/export/export_job.hpp"
#include "pikdraw/interop/writers/svg_exporter.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace pikdraw::desktop {

#ifdef _WIN32

struct ExportJobSession {
    std::jthread worker;
    uint64_t generation{0};
    std::string target_tab_id;
    std::atomic<bool> finished{false};
    std::atomic<bool> cancel_requested{false};
    std::atomic<uint64_t> bytes_written{0};
    std::atomic<size_t> target_index{0};
    std::atomic<size_t> target_count{0};
    std::mutex publish_mutex;
    std::mutex result_mutex;
    std::optional<interop::AtomicExportResult> result;
    std::string error_message;
};

namespace {

std::string format_svg_issue_code(interop::FidelitySeverity severity) {
    return severity == interop::FidelitySeverity::Warning
        ? "SVG_FIDELITY_CONVERSION" : "SVG_FIDELITY_UNSUPPORTED";
}

std::vector<std::string> make_svg_page_paths(const std::string& selected_path,
                                             size_t page_count) {
    if (page_count <= 1) return {selected_path};
    const auto separator = selected_path.find_last_of("/\\");
    const auto dot = selected_path.find_last_of('.');
    const size_t extension_start = dot != std::string::npos &&
        (separator == std::string::npos || dot > separator) ? dot : selected_path.size();
    const auto stem = selected_path.substr(0, extension_start);
    const auto extension = selected_path.substr(extension_start);
    std::vector<std::string> paths;
    paths.reserve(page_count);
    for (size_t index = 0; index < page_count; ++index) {
        std::ostringstream suffix;
        suffix << "-page-" << std::setw(3) << std::setfill('0') << (index + 1);
        paths.push_back(stem + suffix.str() + extension);
    }
    return paths;
}

std::wstring export_failure_text(const interop::AtomicExportResult* result,
                                 const std::string& fallback) {
    if (!result) return NativeWindow::utf8_to_wide(fallback);
    for (const auto& diagnostic : result->report.diagnostics)
        if (diagnostic.severity == interop::ExportDiagnosticSeverity::Error)
            return NativeWindow::utf8_to_wide(diagnostic.message);
    return L"SVG export did not commit an output file.";
}

} // namespace

bool NativeWindow::export_job_active() const noexcept {
    return export_job_ && !export_job_->finished.load(std::memory_order_acquire);
}

bool NativeWindow::start_svg_export(const std::string& file_path,
                                    bool selected_objects,
                                    bool allow_replace_existing,
                                    bool all_pages) {
    if (selected_objects) all_pages = false;
    if (export_job_active()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đang xuất SVG · nhấn Esc để hủy"
            : "SVG export is running · press Esc to cancel";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return false;
    }
    if (export_job_) {
        if (export_job_->worker.joinable()) export_job_->worker.join();
        export_job_.reset();
    }

    const auto snapshot = root().document().current_snapshot();
    if (!snapshot) return false;
    interop::ExportOptionsSchema schema;
    schema.adapter_id = "svg";
    schema.profile_id = "svg-design-v1";
    schema.version = 1;
    const auto validated_options = interop::build_export_options(schema, {});
    if (!validated_options) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Cấu hình xuất SVG không hợp lệ"
            : "The SVG export profile is invalid";
        return false;
    }

    static std::atomic<uint64_t> next_job_id{1};
    const uint64_t generation = next_job_id.fetch_add(1, std::memory_order_relaxed);
    if (generation == 0) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không thể bắt đầu thêm tác vụ xuất"
            : "No export job identifiers remain";
        return false;
    }
    interop::ExportJobRequest request;
    request.job_id = "desktop-svg-" + std::to_string(generation);
    request.adapter_id = schema.adapter_id;
    request.output_version = "SVG 1.1 static subset";
    request.profile_id = schema.profile_id;
    request.options = validated_options.snapshot;
    request.scope = selected_objects ? interop::ExportScope::Selection
        : all_pages ? interop::ExportScope::AllPages
                    : interop::ExportScope::ActivePage;
    request.bounds_policy = interop::ExportBoundsPolicy::Page;
    request.target_mode = all_pages && snapshot->page_count() > 1
        ? interop::ExportTargetMode::PerPageBatch
        : interop::ExportTargetMode::SingleFile;
    request.output_paths = all_pages
        ? make_svg_page_paths(file_path, snapshot->page_count())
        : std::vector<std::string>{file_path};
    request.cancellation_generation = generation;
    if (selected_objects)
        request.selected_object_ids = root().vector_controller().selection().selected_ids();

    bool replace_existing = allow_replace_existing;
    if (all_pages && hwnd_) {
        size_t existing_count = 0;
        for (const auto& path : request.output_paths) {
            std::error_code error;
            if (std::filesystem::exists(std::filesystem::u8path(path), error) && !error)
                ++existing_count;
        }
        std::wostringstream preview;
        preview << (language_ == UILanguage::Vietnamese
            ? L"Sẽ tạo " : L"This export will create ")
            << request.output_paths.size()
            << (language_ == UILanguage::Vietnamese ? L" tệp SVG:\n\n" : L" SVG files:\n\n");
        const size_t shown = (std::min)(request.output_paths.size(), size_t{8});
        for (size_t index = 0; index < shown; ++index)
            preview << L"• " << NativeWindow::utf8_to_wide(request.output_paths[index]) << L"\n";
        if (shown < request.output_paths.size())
            preview << (language_ == UILanguage::Vietnamese
                ? L"… và " : L"… and ") << (request.output_paths.size() - shown)
                << (language_ == UILanguage::Vietnamese ? L" tệp khác\n" : L" more files\n");
        if (existing_count > 0)
            preview << L"\n" << (language_ == UILanguage::Vietnamese
                ? L"Thay thế " : L"Replace ") << existing_count
                << (language_ == UILanguage::Vietnamese
                    ? L" tệp đã tồn tại? Các tệp khác sẽ được tạo mới."
                    : L" existing files? Other pages will be created.");
        else
            preview << L"\n" << (language_ == UILanguage::Vietnamese
                ? L"Tiếp tục xuất batch?" : L"Continue with this batch?");
        if (MessageBoxW(hwnd_, preview.str().c_str(),
                language_ == UILanguage::Vietnamese ? L"Xác nhận batch SVG" : L"Confirm SVG batch",
                MB_YESNO | MB_ICONQUESTION) != IDYES)
            return false;
        replace_existing = existing_count > 0;
    }

    auto built = interop::build_export_job(snapshot, std::move(request));
    if (!built) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Phạm vi xuất không hợp lệ hoặc chưa có đối tượng được chọn"
            : "The export scope is invalid or has no selected objects";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return false;
    }

    auto session = std::make_shared<ExportJobSession>();
    session->generation = generation;
    session->target_tab_id = active_tab().id;
    session->target_count.store(built.job->output_targets().size(),
                                std::memory_order_relaxed);
    const HWND notify_window = hwnd_;
    auto job = std::move(built.job);
    const auto protected_source = active_tab().file_path;
    session->worker = std::jthread(
        [raw = session.get(), notify_window, job = std::move(job),
         protected_source, allow_replace_existing = replace_existing](std::stop_token stop) mutable {
            std::vector<interop::SvgStreamExportResult> fidelity_results;
            std::string writer_error;
            interop::AtomicExportOptions options;
            options.writer_build = "pikdraw-svg-writer/1";
            // The matching importer is the validation oracle and currently accepts 16 MiB.
            options.max_output_bytes_per_file = interop::SvgImporter::kMaxSourceBytes;
            options.allowed_output_extensions = {".svg"};
            options.allow_replace_existing = allow_replace_existing;
            if (!protected_source.empty())
                options.protected_source_paths.push_back(protected_source);
            options.serialize = [job, &fidelity_results, &writer_error](
                const interop::ExportOutputTarget& target, std::ostream& output,
                std::string& diagnostic) {
                if (target.page_ids.size() != 1) {
                    diagnostic = "The SVG writer requires exactly one resolved page per file.";
                    return false;
                }
                const auto* source_page = job->snapshot()->find_page(target.page_ids.front());
                if (!source_page) {
                    diagnostic = "The SVG writer could not resolve a page in the captured snapshot.";
                    return false;
                }
                const document::DocumentSnapshot page_snapshot(
                    job->snapshot()->epoch(), job->snapshot()->revision(),
                    std::vector<document::DocumentPage>{*source_page},
                    source_page->id, job->snapshot()->document_id());
                auto written = interop::SvgExporter::write_to_stream(
                    page_snapshot, output, interop::SvgExportOptions{},
                    &target.object_ids);
                if (!written.success) {
                    diagnostic = written.error_message;
                    for (const auto& issue : written.report.issues()) {
                        if (issue.severity == interop::FidelitySeverity::Unsupported) {
                            diagnostic += ": ";
                            diagnostic += issue.description;
                            break;
                        }
                    }
                    writer_error = diagnostic;
                    return false;
                }
                fidelity_results.push_back(std::move(written));
                return true;
            };
            options.validate = [](const interop::ExportOutputTarget&,
                                 std::istream& staged, uint64_t byte_size,
                                 std::string& diagnostic) {
                if (byte_size == 0 ||
                    byte_size > interop::SvgImporter::kMaxSourceBytes) {
                    diagnostic = "The staged SVG exceeds the enabled reader validation budget.";
                    return false;
                }
                std::string bytes(static_cast<size_t>(byte_size), '\0');
                staged.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                if (staged.gcount() != static_cast<std::streamsize>(bytes.size()) ||
                    staged.bad()) {
                    diagnostic = "The staged SVG could not be read back for validation.";
                    return false;
                }
                const auto imported = interop::SvgImporter::import_from_string(bytes);
                if (!imported.success) {
                    diagnostic = imported.error_message.empty()
                        ? "The staged SVG failed the enabled SVG reader validation."
                        : imported.error_message;
                    return false;
                }
                return true;
            };
            options.is_cancelled = [raw, stop](uint64_t generation) {
                return stop.stop_requested() ||
                    raw->cancel_requested.load(std::memory_order_acquire) ||
                    generation != raw->generation;
            };
            options.publish_if_current = [raw](
                uint64_t generation, const std::function<bool()>& publish) {
                std::lock_guard lock(raw->publish_mutex);
                if (generation != raw->generation ||
                    raw->cancel_requested.load(std::memory_order_acquire))
                    return false;
                return publish();
            };
            auto last_progress = std::chrono::steady_clock::now();
            options.on_progress = [raw, notify_window, &last_progress](
                size_t target_index, size_t target_count, uint64_t bytes_written,
                uint64_t) {
                raw->target_index.store(target_index, std::memory_order_relaxed);
                raw->target_count.store(target_count, std::memory_order_relaxed);
                raw->bytes_written.store(bytes_written, std::memory_order_relaxed);
                const auto now = std::chrono::steady_clock::now();
                if (now - last_progress >= std::chrono::milliseconds(160)) {
                    last_progress = now;
                    (void)PostMessageW(notify_window, kExportWorkerMessage, 0, 0);
                }
            };

            std::optional<interop::AtomicExportResult> result;
            std::string error;
            try {
                result.emplace(interop::execute_atomic_export(*job, options));
                if (result->report.status == interop::ExportJobStatus::Complete &&
                    !fidelity_results.empty() &&
                    !fidelity_results.front().report.is_clean()) {
                    result->report.status = interop::ExportJobStatus::CompleteWithDiagnostics;
                    result->report.appearance = interop::ExportAppearanceStatus::Partial;
                    for (const auto& issue : fidelity_results.front().report.issues()) {
                        const auto severity = issue.severity == interop::FidelitySeverity::Info
                            ? interop::ExportDiagnosticSeverity::Info
                            : interop::ExportDiagnosticSeverity::Warning;
                        result->report.diagnostics.push_back({
                            format_svg_issue_code(issue.severity), severity, std::nullopt,
                            std::nullopt, issue.feature + ": " + issue.description});
                    }
                }
                result->report_validation =
                    interop::validate_export_job_report(result->report);
                if (!result->report_validation)
                    error = result->report_validation.message;
            } catch (const std::exception& exception) {
                error = exception.what();
            } catch (...) {
                error = "Unexpected SVG export failure";
            }
            {
                std::lock_guard lock(raw->result_mutex);
                raw->result = std::move(result);
                raw->error_message = !error.empty() ? std::move(error)
                    : std::move(writer_error);
            }
            raw->finished.store(true, std::memory_order_release);
            (void)PostMessageW(notify_window, kExportWorkerMessage, 1, 0);
        });
    export_job_ = std::move(session);
    status_msg_ = language_ == UILanguage::Vietnamese
        ? (selected_objects ? "Đang xuất đối tượng SVG… · nhấn Esc để hủy"
            : all_pages ? "Đang xuất từng trang SVG… · nhấn Esc để hủy"
                        : "Đang xuất trang SVG… · nhấn Esc để hủy")
        : (selected_objects ? "Exporting selected SVG objects… · press Esc to cancel"
            : all_pages ? "Exporting SVG pages… · press Esc to cancel"
                        : "Exporting SVG page… · press Esc to cancel");
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void NativeWindow::request_cancel_export_job() {
    if (!export_job_active()) return;
    {
        std::lock_guard lock(export_job_->publish_mutex);
        export_job_->cancel_requested.store(true, std::memory_order_release);
    }
    export_job_->worker.request_stop();
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Đang hủy tác vụ xuất…" : "Cancelling export…";
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativeWindow::cancel_export_job() {
    if (!export_job_) return;
    request_cancel_export_job();
    if (export_job_->worker.joinable()) export_job_->worker.join();
    export_job_.reset();
}

void NativeWindow::handle_export_worker_message(WPARAM kind) {
    if (!export_job_) return;
    if (kind == 0) {
        if (!export_job_active()) return;
        const auto bytes = export_job_->bytes_written.load(std::memory_order_relaxed);
        const auto index = export_job_->target_index.load(std::memory_order_relaxed) + 1;
        const auto count = export_job_->target_count.load(std::memory_order_relaxed);
        std::ostringstream message;
        if (language_ == UILanguage::Vietnamese)
            message << "Đang xuất SVG " << index << '/' << count << " · ";
        else
            message << "Exporting SVG " << index << '/' << count << " · ";
        if (bytes < 1024 * 1024)
            message << (bytes / 1024) << " KiB";
        else
            message << (bytes / (1024 * 1024)) << " MiB";
        message << (language_ == UILanguage::Vietnamese ? " · Esc để hủy" : " · Esc to cancel");
        status_msg_ = message.str();
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    auto session = export_job_;
    if (session->worker.joinable()) session->worker.join();
    std::optional<interop::AtomicExportResult> result;
    std::string error;
    {
        std::lock_guard lock(session->result_mutex);
        result = std::move(session->result);
        error = std::move(session->error_message);
    }
    export_job_.reset();
    size_t committed_count = 0;
    uint64_t committed_bytes = 0;
    if (result) {
        for (const auto& file : result->report.files) {
            if (file.status != interop::ExportFileStatus::Committed) continue;
            ++committed_count;
            committed_bytes += file.byte_size;
        }
    }
    if (!result || !result->has_valid_report()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Xuất SVG thất bại" : "SVG export failed";
        const auto details = export_failure_text(result ? &*result : nullptr, error);
        MessageBoxW(hwnd_, details.c_str(),
            language_ == UILanguage::Vietnamese ? L"Không thể xuất SVG" : L"SVG export failed",
            MB_OK | MB_ICONWARNING);
    } else if (result->report.status == interop::ExportJobStatus::Cancelled) {
        status_msg_ = committed_count == 0
            ? (language_ == UILanguage::Vietnamese
                ? "Đã hủy xuất SVG · chưa công bố tệp nào"
                : "SVG export cancelled · no files were committed")
            : (language_ == UILanguage::Vietnamese
                ? "Đã hủy xuất SVG · đã công bố " + std::to_string(committed_count) + " tệp; các tệp còn lại chưa đổi"
                : "SVG export cancelled · committed " + std::to_string(committed_count) + " files; remaining outputs unchanged");
    } else if (result->report.status == interop::ExportJobStatus::Complete ||
               result->report.status == interop::ExportJobStatus::CompleteWithDiagnostics) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã xuất nguyên tử " + std::to_string(committed_count) + " tệp SVG · " +
                std::to_string(committed_bytes) + " byte · SHA-256 đã xác minh"
            : "Committed " + std::to_string(committed_count) + " SVG file(s) atomically · " +
                std::to_string(committed_bytes) + " bytes · SHA-256 verified";
        if (!result->report.diagnostics.empty()) {
            std::wstring details = language_ == UILanguage::Vietnamese
                ? L"Đã xuất SVG với các chuyển đổi cần lưu ý:\n"
                : L"SVG was exported with fidelity conversions:\n";
            for (const auto& diagnostic : result->report.diagnostics) {
                details += L"• ";
                details += NativeWindow::utf8_to_wide(diagnostic.message);
                details += L"\n";
            }
            MessageBoxW(hwnd_, details.c_str(),
                language_ == UILanguage::Vietnamese ? L"Báo cáo xuất SVG" : L"SVG export report",
                MB_OK | MB_ICONINFORMATION);
        }
    } else {
        status_msg_ = committed_count == 0
            ? (language_ == UILanguage::Vietnamese
                ? "Xuất SVG thất bại · chưa công bố tệp nào"
                : "SVG export failed · no files were committed")
            : (language_ == UILanguage::Vietnamese
                ? "Batch SVG dừng sau " + std::to_string(committed_count) + " tệp; các tệp còn lại chưa đổi"
                : "SVG batch stopped after " + std::to_string(committed_count) + " file(s); remaining outputs unchanged");
        const auto details = export_failure_text(&*result, error);
        MessageBoxW(hwnd_, details.c_str(),
            language_ == UILanguage::Vietnamese ? L"Không thể xuất SVG" : L"SVG export failed",
            MB_OK | MB_ICONWARNING);
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

#endif

} // namespace pikdraw::desktop
