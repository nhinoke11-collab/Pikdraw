#include "pikdraw/desktop/shell/native_window.hpp"

#include <atomic>
#include <filesystem>
#include <iomanip>
#include <iterator>
#include <mutex>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <commctrl.h>
#include <commdlg.h>
#endif

namespace pikdraw::desktop {

#ifdef _WIN32
namespace {
constexpr int kImportChoiceApply = 4101;
constexpr int kImportChoiceCancel = 4102;

std::wstring FormatNumber(double value) {
    std::wostringstream text;
    text << std::fixed << std::setprecision(2) << value;
    return text.str();
}

}

struct ImportJobSession {
    std::jthread worker;
    document::AsyncProposalMetadata metadata;
    std::string target_tab_id;
    std::atomic<ImportProgressStage> stage{ImportProgressStage::Snapshotting};
    std::atomic<size_t> records_received{0};
    std::atomic<bool> finished{false};
    std::atomic<bool> cancel_requested{false};
    std::mutex result_mutex;
    std::optional<ImportWorkerResult> result;
    bool is_open_action{false};
    std::string source_filename;
};

bool NativeWindow::import_job_active() const noexcept {
    return import_job_ && !import_job_->finished.load(std::memory_order_acquire);
}

bool NativeWindow::import_file_to_tab(const std::wstring& path, bool is_open_action) {
    if (import_job_active()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đang nhập tệp khác · nhấn Esc để hủy"
            : "Another import is running · press Esc to cancel";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return false;
    }
    if (import_job_) {
        if (import_job_->worker.joinable()) import_job_->worker.join();
        import_job_.reset();
    }

    const std::wstring extension = std::filesystem::path(path).extension().wstring();
    ImportFormat format{};
    if (_wcsicmp(extension.c_str(), L".dxf") == 0) {
        format = ImportFormat::Dxf;
    } else if (_wcsicmp(extension.c_str(), L".svg") == 0 ||
               _wcsicmp(extension.c_str(), L".png") == 0 ||
               _wcsicmp(extension.c_str(), L".jpg") == 0 ||
               _wcsicmp(extension.c_str(), L".jpeg") == 0 ||
               _wcsicmp(extension.c_str(), L".tif") == 0 ||
               _wcsicmp(extension.c_str(), L".tiff") == 0 ||
               _wcsicmp(extension.c_str(), L".cdr") == 0 ||
               _wcsicmp(extension.c_str(), L".ai") == 0 ||
               _wcsicmp(extension.c_str(), L".pdf") == 0) {
        format = ImportFormat::Svg;
    } else {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Định dạng này chưa có bộ nhập. Hỗ trợ SVG, DXF 2D, CDR, AI, PDF và ảnh PNG/JPEG/TIFF."
            : "No reader is enabled for this format. SVG, 2D DXF, CDR, AI, PDF and PNG/JPEG/TIFF are supported.";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return false;
    }

    const auto u8_filename = std::filesystem::path(path).filename().u8string();
    const std::string filename(u8_filename.begin(), u8_filename.end());

    if (is_open_action) {
        const bool tab_has_content = root().document().is_dirty() ||
            root().document().current_snapshot()->object_count() > 0;
        if (tab_has_content) {
            create_new_tab(filename, 297.0, 210.0);
        } else {
            active_tab().title = filename;
        }
    }

    static std::atomic<uint64_t> next_job_id{1};
    auto session = std::make_shared<ImportJobSession>();
    const uint64_t job_id = next_job_id.fetch_add(1, std::memory_order_relaxed);
    if (job_id == 0) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không thể bắt đầu thêm tác vụ nhập tệp"
            : "No import job identifiers remain";
        return false;
    }
    try {
        session->metadata = workflow().begin_import_job(job_id);
    } catch (...) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không thể tạo phiên nhập cho tài liệu hiện tại"
            : "Could not start an import job for this document";
        return false;
    }
    session->target_tab_id = active_tab().id;
    session->is_open_action = is_open_action;
    session->source_filename = filename;
    const HWND notify_window = hwnd_;
    const auto metadata = session->metadata;
    session->worker = std::jthread([raw = session.get(), path_copy = path, format,
                                    metadata, notify_window](std::stop_token cancellation) {
        auto result = run_import_worker_job(path_copy, format, metadata, cancellation,
            [raw, notify_window](ImportProgress progress) {
                raw->stage.store(progress.stage, std::memory_order_release);
                raw->records_received.store(progress.records_received, std::memory_order_release);
                (void)PostMessageW(notify_window, kImportWorkerMessage, 0, 0);
            });
        {
            std::lock_guard lock(raw->result_mutex);
            raw->result.emplace(std::move(result));
        }
        raw->finished.store(true, std::memory_order_release);
        (void)PostMessageW(notify_window, kImportWorkerMessage, 1, 0);
    });
    import_job_ = std::move(session);
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Đang chuẩn bị tệp... nhấn Esc để hủy"
        : "Preparing source... press Esc to cancel";
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void NativeWindow::open_import_dialog() {
    if (import_job_active()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đang nhập tệp khác · nhấn Esc để hủy"
            : "Another import is running · press Esc to cancel";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    std::wstring path(32768, L'\0');
    const wchar_t* filter = language_ == UILanguage::Vietnamese
        ? L"Tất cả tệp hỗ trợ (*.svg;*.dxf;*.png;*.jpg;*.jpeg;*.tif;*.tiff;*.cdr;*.ai;*.pdf)\0*.svg;*.dxf;*.png;*.jpg;*.jpeg;*.tif;*.tiff;*.cdr;*.ai;*.pdf\0"
          L"Adobe Illustrator & PDF (*.ai;*.pdf)\0*.ai;*.pdf\0"
          L"Bản vẽ CorelDRAW (*.cdr)\0*.cdr\0"
          L"Ảnh PNG/JPEG/TIFF (*.png;*.jpg;*.jpeg;*.tif;*.tiff)\0*.png;*.jpg;*.jpeg;*.tif;*.tiff\0"
          L"Vector SVG (*.svg)\0*.svg\0"
          L"Tập con AutoCAD DXF 2D (*.dxf)\0*.dxf\0"
          L"Tất cả tệp\0*.*\0\0"
        : L"All Supported Files (*.svg;*.dxf;*.png;*.jpg;*.jpeg;*.tif;*.tiff;*.cdr;*.ai;*.pdf)\0*.svg;*.dxf;*.png;*.jpg;*.jpeg;*.tif;*.tiff;*.cdr;*.ai;*.pdf\0"
          L"Adobe Illustrator & PDF (*.ai;*.pdf)\0*.ai;*.pdf\0"
          L"CorelDRAW Files (*.cdr)\0*.cdr\0"
          L"PNG/JPEG/TIFF images (*.png;*.jpg;*.jpeg;*.tif;*.tiff)\0*.png;*.jpg;*.jpeg;*.tif;*.tiff\0"
          L"SVG vector (*.svg)\0*.svg\0"
          L"AutoCAD DXF 2D subset (*.dxf)\0*.dxf\0"
          L"All files\0*.*\0\0";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd_;
    dialog.lpstrFilter = filter;
    dialog.lpstrTitle = language_ == UILanguage::Vietnamese
        ? L"Chọn tệp cần nhập" : L"Choose a file to import";
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return;
    import_file_to_tab(path.c_str(), false);
}

void NativeWindow::request_cancel_import_job() {
    if (!import_job_active()) return;
    import_job_->cancel_requested.store(true, std::memory_order_release);
    import_job_->worker.request_stop();
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Đang hủy tác vụ nhập…"
        : "Cancelling import…";
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativeWindow::cancel_import_job() {
    if (!import_job_) return;
    import_job_->cancel_requested.store(true, std::memory_order_release);
    import_job_->worker.request_stop();
    if (import_job_->worker.joinable()) import_job_->worker.join();
    for (auto& tab : tabs_) {
        if (tab.id != import_job_->target_tab_id || !tab.workflow) continue;
        try { tab.workflow->cancel_import_job(import_job_->metadata.job_id); } catch (...) {}
        break;
    }
    import_job_.reset();
}

void NativeWindow::handle_import_worker_message(WPARAM kind) {
    if (!import_job_) return;
    if (kind == 0) {
        if (import_job_active()) {
            const auto stage = import_job_->stage.load(std::memory_order_acquire);
            const size_t count = import_job_->records_received.load(std::memory_order_acquire);
            switch (stage) {
            case ImportProgressStage::Snapshotting:
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Đang kiểm tra và chuẩn bị tệp nhập… · Esc để hủy"
                    : "Checking and staging import source… · Esc to cancel";
                break;
            case ImportProgressStage::StartingWorker:
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Đang khởi chạy worker cách ly… · Esc để hủy"
                    : "Starting isolated worker… · Esc to cancel";
                break;
            case ImportProgressStage::Reading:
                status_msg_ = (language_ == UILanguage::Vietnamese
                    ? "Đang nhập · đã nhận " : "Importing · received ") +
                    std::to_string(count) + (language_ == UILanguage::Vietnamese
                        ? " bản ghi · Esc để hủy" : " records · Esc to cancel");
                break;
            case ImportProgressStage::Validating:
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Đang kiểm tra cấu trúc và báo cáo độ trung thực… · Esc để hủy"
                    : "Validating structure and fidelity report… · Esc to cancel";
                break;
            case ImportProgressStage::Complete:
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Đã kiểm tra đề xuất nhập" : "Import proposal validated";
                break;
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }

    auto session = import_job_;
    if (session->worker.joinable()) session->worker.join();
    std::optional<ImportWorkerResult> result;
    {
        std::lock_guard lock(session->result_mutex);
        result = std::move(session->result);
    }
    const auto close_session = [&] {
        import_job_.reset();
        InvalidateRect(hwnd_, nullptr, FALSE);
    };
    DocumentTab* target = nullptr;
    size_t target_index = 0;
    for (size_t i = 0; i < tabs_.size(); ++i) {
        if (tabs_[i].id == session->target_tab_id) {
            target = &tabs_[i];
            target_index = i;
            break;
        }
    }
    if (!result || !target || !target->workflow || !target->root) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Tác vụ nhập đã kết thúc nhưng tài liệu đích không còn mở"
            : "The import ended after its target document was closed";
        close_session();
        return;
    }
    if (session->cancel_requested.load(std::memory_order_acquire) || result->cancelled) {
        try { target->workflow->cancel_import_job(session->metadata.job_id); } catch (...) {}
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đã hủy nhập; tài liệu không đổi"
                                                          : "Import cancelled; document unchanged";
        close_session();
        return;
    }
    if (!result->success) {
        try { target->workflow->cancel_import_job(session->metadata.job_id); } catch (...) {}
        const auto message = NativeWindow::utf8_to_wide(result->error_message);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Nhập tệp thất bại" : "Import failed";
        MessageBoxW(hwnd_, message.c_str(),
            language_ == UILanguage::Vietnamese ? L"Không thể nhập tệp" : L"Could not import file",
            MB_OK | MB_ICONWARNING);
        close_session();
        return;
    }

    const auto snapshot = target->root->document().current_snapshot();
    if (!snapshot || snapshot->document_id() != session->metadata.document_id ||
        snapshot->epoch() != session->metadata.epoch || snapshot->revision() != session->metadata.base_revision) {
        try { target->workflow->cancel_import_job(session->metadata.job_id); } catch (...) {}
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Tài liệu đã thay đổi trong lúc nhập · hãy chạy lại Import"
            : "The document changed during import · run Import again";
        close_session();
        return;
    }

    const size_t object_count = result->proposal.objects.size();
    const size_t layer_count = result->proposal.layers.size();
    const auto& proposal = result->proposal;
    const bool is_raster = proposal.profile_id == "raster-rgba8-v1";
    const bool is_preview = proposal.profile_id == "cdr-preview-v1" ||
                            proposal.profile_id == "pdf-ai-preview-v1";
    std::wostringstream content;
    content << (language_ == UILanguage::Vietnamese ? L"Nguồn: " : L"Source: ")
            << NativeWindow::utf8_to_wide(proposal.format_name).c_str() << L" · "
            << (language_ == UILanguage::Vietnamese ? L"trang nguồn " : L"source page ")
            << FormatNumber(proposal.source_page_width_mm) << L" × "
            << FormatNumber(proposal.source_page_height_mm) << L" mm\n"
            << object_count << (language_ == UILanguage::Vietnamese
                ? ((is_raster || is_preview) ? L" ảnh raster · " : L" đối tượng chỉnh sửa được · ")
                : ((is_raster || is_preview) ? L" raster image · " : L" editable objects · "))
            << layer_count << (language_ == UILanguage::Vietnamese ? L" layer nguồn\n" : L" source layers\n")
            << (language_ == UILanguage::Vietnamese ? L"Độ bao phủ: một phần · Hiển thị: chưa xác minh · Sẵn sàng sản xuất: chưa đánh giá."
                                                    : L"Coverage: partial · Appearance: unverified · Production: not assessed.");

    std::wstring details;
    if (is_preview) {
        details = language_ == UILanguage::Vietnamese
            ? L"Chỉ nhập ảnh xem trước nhúng trong tệp CDR/AI/PDF. Không có đường vector, chữ hoặc layer gốc để chỉnh sửa hay cắt CNC. Chất lượng phóng to phụ thuộc thumbnail nguồn.\n"
              L"Ảnh được đặt theo khổ trang nguồn. Dữ liệu thiết kế gốc không được lưu cùng tài liệu .pikdraw; hãy giữ tệp nguồn."
            : L"Only the embedded CDR/AI/PDF preview is imported. Original vector paths, text and layers are unavailable for editing or CNC. Enlargement is limited by the source thumbnail.\n"
              L"The image uses the source page dimensions. Original authoring data is not stored in the .pikdraw document; keep the source file.";
    } else if (is_raster) {
        details = language_ == UILanguage::Vietnamese
            ? L"Ảnh được giải mã thành pixel RGBA8 để hiển thị; byte nguồn, DPI, hướng EXIF và ICC được giữ trong tài liệu. Chuyển đổi màu chưa được chứng nhận giống tuyệt đối giữa các ứng dụng.\n"
              L"Ảnh raster không phải đường vector để cắt CNC. Độ phân giải nguồn và việc chuyển sang RGBA8 có thể giới hạn chất lượng phóng to. Khổ trang tài liệu hiện tại không đổi."
            : L"The image is decoded to RGBA8 pixels for display; source bytes, DPI, EXIF orientation and ICC data are retained in the document. Color conversion is not certified to match every application exactly.\n"
              L"A raster image is not vector geometry for CNC cutting. Source resolution and RGBA8 conversion can limit enlargement quality. The current document page size stays unchanged.";
    } else {
        details = language_ == UILanguage::Vietnamese
            ? L"Các đơn vị SVG được quy đổi sang mm. DXF dùng $INSUNITS của nguồn; thiếu đơn vị được hỗ trợ thì từ chối.\n"
              L"Khổ trang tài liệu hiện tại không đổi. Geometry và layer nguồn sẽ được thêm vào trang hiện tại.\n"
              L"File nguồn và báo cáo fidelity không được lưu cùng tài liệu .pikdraw. Import không chứng minh export ngược hoặc khả năng sản xuất."
            : L"SVG source units are converted to mm. DXF uses source $INSUNITS; unsupported or missing units are rejected.\n"
              L"The current document page size stays unchanged. Source geometry and layers are added to its current page.\n"
              L"The source file and fidelity report are not stored in the .pikdraw document. Import does not prove round-trip export or production readiness.";
    }
    for (const auto& issue : proposal.issues) {
        details += L"\n• ";
        if (issue.feature_code == "font_resolution") {
            details += language_ == UILanguage::Vietnamese
                ? L"Giữ tên font; hình thức chữ chưa được xác minh."
                : L"Font family is retained; text appearance is unverified.";
        } else if (issue.feature_code == "aci_color_7_context") {
            details += language_ == UILanguage::Vietnamese
                ? L"Màu DXF ACI 7 phụ thuộc nền CAD; preview dùng màu trắng."
                : L"DXF ACI 7 depends on CAD background; preview uses white.";
        } else {
            details += language_ == UILanguage::Vietnamese
                ? L"Có chẩn đoán giới hạn subset trong báo cáo nhập."
                : L"The import report contains an additional subset diagnostic.";
        }
    }

    const std::wstring title = language_ == UILanguage::Vietnamese ? L"Xem lại kết quả nhập" : L"Review import fidelity";
    const std::wstring instruction = language_ == UILanguage::Vietnamese
        ? (is_preview ? L"Nhập ảnh xem trước với các giới hạn nêu trên?"
           : is_raster ? L"Nhập ảnh raster đã chuyển đổi với các giới hạn hiển thị nêu trên?"
                     : L"Chỉ nhập các đối tượng được biểu diễn trong subset hiện tại?")
        : (is_preview ? L"Import the embedded preview with these limitations?"
           : is_raster ? L"Import the converted raster image with these display limitations?"
                     : L"Import only the objects represented by the current subset?");
    const std::wstring apply_label = language_ == UILanguage::Vietnamese
        ? ((is_raster || is_preview) ? L"Nhập ảnh" : L"Nhập đối tượng")
        : ((is_raster || is_preview) ? L"Import image" : L"Import editable subset");
    const std::wstring cancel_label = language_ == UILanguage::Vietnamese ? L"Hủy" : L"Cancel";
    const TASKDIALOG_BUTTON buttons[]{{kImportChoiceApply, apply_label.c_str()},
                                      {kImportChoiceCancel, cancel_label.c_str()}};
    const std::wstring content_text = content.str();
    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.hwndParent = hwnd_;
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_EXPAND_FOOTER_AREA;
    config.pszWindowTitle = title.c_str();
    config.pszMainInstruction = instruction.c_str();
    config.pszContent = content_text.c_str();
    config.pszExpandedInformation = details.c_str();
    config.cButtons = static_cast<UINT>(std::size(buttons));
    config.pButtons = buttons;
    config.nDefaultButton = kImportChoiceCancel;
    int choice = kImportChoiceCancel;
    if (session->is_open_action) {
        choice = kImportChoiceApply;
    } else {
        using TaskDialogFunction = HRESULT(WINAPI*)(const TASKDIALOGCONFIG*, int*, int*, BOOL*);
        HMODULE common_controls = LoadLibraryW(L"comctl32.dll");
        const auto task_dialog = common_controls ? reinterpret_cast<TaskDialogFunction>(
            GetProcAddress(common_controls, "TaskDialogIndirect")) : nullptr;
        const HRESULT dialog_result = task_dialog
            ? task_dialog(&config, &choice, nullptr, nullptr) : E_NOTIMPL;
        if (common_controls) FreeLibrary(common_controls);
        if (FAILED(dialog_result)) {
            const std::wstring fallback = content.str() + L"\n\n" + details;
            choice = MessageBoxW(hwnd_, fallback.c_str(), title.c_str(), MB_YESNO | MB_ICONWARNING) == IDYES
                ? kImportChoiceApply : kImportChoiceCancel;
        }
    }
    if (choice != kImportChoiceApply) {
        try { target->workflow->cancel_import_job(session->metadata.job_id); } catch (...) {}
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đã hủy nhập; tài liệu không đổi"
                                                          : "Import cancelled; document unchanged";
        close_session();
        return;
    }

    const double source_page_w = proposal.source_page_width_mm;
    const double source_page_h = proposal.source_page_height_mm;
    const auto committed = target->workflow->commit_import_proposal(
        session->metadata, std::move(result->proposal));
    if (committed == document::ProposalStatus::Accepted) {
        if (source_page_w > 0.0 && source_page_h > 0.0) {
            target->workflow->set_page_size(source_page_w, source_page_h);
            target->page_width_mm = source_page_w;
            target->page_height_mm = source_page_h;
        }
        const bool target_active = target_index == active_tab_index_;
        if (target_active) {
            fit_page_to_canvas();
            root().vector_controller().selection().clear();
            set_active_tool(0);
        }
        if (session->is_open_action) {
            target->file_path.clear();
            status_msg_ = (language_ == UILanguage::Vietnamese ? "Đã mở tệp: " : "Opened file: ") +
                session->source_filename;
        } else {
            const std::string count_text = std::to_string(object_count);
            if (target_active) {
                status_msg_ = (language_ == UILanguage::Vietnamese ? "Đã nhập " : "Imported ") +
                    count_text + (language_ == UILanguage::Vietnamese
                        ? " đối tượng · một bước Ctrl+Z · khổ trang giữ nguyên"
                        : " objects · one Ctrl+Z step · page size unchanged");
            }
        }
    } else {
        status_msg_ = committed == document::ProposalStatus::StaleRevision
            ? (language_ == UILanguage::Vietnamese ? "Tài liệu đã thay đổi · đề xuất nhập bị từ chối"
                                                  : "Document changed · import proposal rejected")
            : (language_ == UILanguage::Vietnamese ? "Không thể áp dụng đề xuất nhập"
                                                  : "Could not apply import proposal");
    }
    close_session();
}

#endif

} // namespace pikdraw::desktop
