#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/desktop/shell/open_document_policy.hpp"
#include "native_window_length_input.hpp"

#include <atomic>
#include <cwchar>
#include <filesystem>
#include <iterator>
#include <mutex>
#include <optional>
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
constexpr int kTiffPageEdit = 4103;

struct TiffPageDialogState {
    bool vietnamese{false};
    uint32_t zero_based_page{0};
};

INT_PTR CALLBACK tiff_page_dialog_proc(HWND dialog, UINT message,
                                       WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<TiffPageDialogState*>(
        GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<TiffPageDialogState*>(lparam);
        SetWindowLongPtrW(dialog, DWLP_USER, lparam);
        SetWindowTextW(dialog, state->vietnamese
            ? L"Chọn trang TIFF" : L"Select TIFF page");
        const HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const wchar_t* prompt = state->vietnamese
            ? L"Chọn số trang TIFF cần nhập (1–256):"
            : L"Choose the TIFF page to import (1–256):";
        const wchar_t* hint = state->vietnamese
            ? L"Mặc định trang 1; worker sẽ kiểm tra số trang trong tệp."
            : L"Page 1 is default; the worker checks the file's page count.";
        HWND label = CreateWindowExW(0, L"STATIC", prompt, WS_CHILD | WS_VISIBLE,
            10, 10, 250, 16, dialog, nullptr, GetModuleHandleW(nullptr), nullptr);
        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
            10, 31, 250, 22, dialog, reinterpret_cast<HMENU>(kTiffPageEdit),
            GetModuleHandleW(nullptr), nullptr);
        HWND help = CreateWindowExW(0, L"STATIC", hint, WS_CHILD | WS_VISIBLE,
            10, 57, 250, 16, dialog, nullptr, GetModuleHandleW(nullptr), nullptr);
        HWND ok = CreateWindowExW(0, L"BUTTON",
            state->vietnamese ? L"Chọn" : L"Select",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            139, 83, 58, 23, dialog, reinterpret_cast<HMENU>(IDOK),
            GetModuleHandleW(nullptr), nullptr);
        HWND cancel = CreateWindowExW(0, L"BUTTON",
            state->vietnamese ? L"Hủy" : L"Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            202, 83, 58, 23, dialog, reinterpret_cast<HMENU>(IDCANCEL),
            GetModuleHandleW(nullptr), nullptr);
        for (HWND control : {label, edit, help, ok, cancel})
            if (control) SendMessageW(control, WM_SETFONT,
                reinterpret_cast<WPARAM>(font), TRUE);
        SetFocus(edit);
        SendMessageW(edit, EM_SETSEL, 0, -1);
        return FALSE;
    }
    if (!state) return FALSE;
    if (message == WM_COMMAND) {
        if (LOWORD(wparam) == IDOK) {
            wchar_t text[32]{};
            GetDlgItemTextW(dialog, kTiffPageEdit, text,
                            static_cast<int>(std::size(text)));
            wchar_t* end = nullptr;
            const unsigned long page = std::wcstoul(text, &end, 10);
            if (end == text || *end != L'\0' || page == 0 || page > 256) {
                MessageBoxW(dialog,
                    state->vietnamese ? L"Nhập số trang từ 1 đến 256." :
                                        L"Enter a page number from 1 to 256.",
                    state->vietnamese ? L"Số trang không hợp lệ" : L"Invalid page number",
                    MB_OK | MB_ICONWARNING);
                return TRUE;
            }
            state->zero_based_page = static_cast<uint32_t>(page - 1);
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        if (LOWORD(wparam) == IDCANCEL) {
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

std::optional<uint32_t> choose_tiff_page(HWND owner, bool vietnamese) {
    struct EmptyDialog { DLGTEMPLATE dialog; WORD menu{0}, klass{0}, title{0}; } layout{};
    layout.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME |
        DS_CENTER | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    layout.dialog.cx = 280;
    layout.dialog.cy = 119;
    layout.dialog.cdit = 0;
    TiffPageDialogState state{vietnamese, 0};
    const INT_PTR result = DialogBoxIndirectParamW(GetModuleHandleW(nullptr),
        &layout.dialog, owner, tiff_page_dialog_proc,
        reinterpret_cast<LPARAM>(&state));
    return result == IDOK ? std::optional<uint32_t>{state.zero_based_page}
                          : std::nullopt;
}

}

struct ImportJobSession {
    std::jthread worker;
    document::AsyncProposalMetadata metadata;
    std::string target_tab_id;
    std::atomic<ImportProgressStage> stage{ImportProgressStage::Snapshotting};
    std::atomic<size_t> records_received{0};
    std::atomic<uint64_t> source_bytes_copied{0};
    std::atomic<uint64_t> source_bytes_total{0};
    std::atomic<bool> finished{false};
    std::atomic<bool> cancel_requested{false};
    std::atomic<bool> progress_message_pending{false};
    std::mutex result_mutex;
    std::optional<ImportWorkerResult> result;
    bool is_open_action{false};
    bool created_open_tab{false};
    uint64_t view_generation{0};
    std::string previous_tab_title;
    std::string previous_file_path;
    std::string source_filename;
};

bool NativeWindow::import_job_active() const noexcept {
    return import_job_ && !import_job_->finished.load(std::memory_order_acquire);
}

bool NativeWindow::import_file_to_tab(const std::wstring& path, bool is_open_action) {
    if (import_placement_active()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Hãy đặt hoặc hủy ảnh đang nhập trước khi nhập tệp khác"
            : "Place or cancel the pending image before starting another import";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return false;
    }
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
    last_import_succeeded_ = false;
    close_recovery_modal();

    const std::wstring extension = std::filesystem::path(path).extension().wstring();
    ImportFormat format{};
    if (_wcsicmp(extension.c_str(), L".dxf") == 0) {
        format = ImportFormat::Dxf;
    } else if (_wcsicmp(extension.c_str(), L".svg") == 0 ||
               _wcsicmp(extension.c_str(), L".ai") == 0 ||
               _wcsicmp(extension.c_str(), L".pdf") == 0 ||
               _wcsicmp(extension.c_str(), L".cdr") == 0 ||
               _wcsicmp(extension.c_str(), L".eps") == 0 ||
               _wcsicmp(extension.c_str(), L".png") == 0 ||
               _wcsicmp(extension.c_str(), L".jpg") == 0 ||
               _wcsicmp(extension.c_str(), L".jpeg") == 0 ||
               _wcsicmp(extension.c_str(), L".tif") == 0 ||
               _wcsicmp(extension.c_str(), L".tiff") == 0 ||
               _wcsicmp(extension.c_str(), L".bmp") == 0) {
        format = ImportFormat::Svg;
    } else {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Chưa có bộ nhập cho định dạng này. Có thể mở PikDraw, SVG/DXF, subset CDR/PDF/AI/EPS và PNG/JPEG/TIFF/BMP."
            : "No reader is enabled for this format. Supported: PikDraw, SVG/DXF, CDR/PDF/AI/EPS subsets, and PNG/JPEG/TIFF/BMP.";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return false;
    }

    uint32_t raster_frame_index = 0;
    if (_wcsicmp(extension.c_str(), L".tif") == 0 ||
        _wcsicmp(extension.c_str(), L".tiff") == 0) {
        const auto selected_page = choose_tiff_page(hwnd_,
            language_ == UILanguage::Vietnamese);
        if (!selected_page) return false;
        raster_frame_index = *selected_page;
    }

    const auto u8_filename = std::filesystem::path(path).filename().u8string();
    const std::string filename(u8_filename.begin(), u8_filename.end());

    bool created_open_tab = false;
    std::string previous_tab_title;
    std::string previous_file_path;
    if (is_open_action) {
        const auto current_snapshot = root().document().current_snapshot();
        const bool tab_has_content = root().document().is_dirty() ||
            detail::snapshot_has_openable_content(*current_snapshot);
        if (tab_has_content) {
            create_new_tab(filename, 297.0, 210.0);
            created_open_tab = true;
        } else {
            previous_tab_title = active_tab().title;
            previous_file_path = active_tab().file_path;
            active_tab().title = filename;
        }
    }
    const auto restore_open_tab = [&] {
        if (!is_open_action) return;
        if (created_open_tab) {
            if (tabs_.size() > 1) close_tab(active_tab_index_);
        } else {
            active_tab().title = previous_tab_title;
            active_tab().file_path = previous_file_path;
            document_path_ = active_tab().file_path;
        }
    };

    static std::atomic<uint64_t> next_job_id{1};
    auto session = std::make_shared<ImportJobSession>();
    const uint64_t job_id = next_job_id.fetch_add(1, std::memory_order_relaxed);
    if (job_id == 0) {
        restore_open_tab();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không thể bắt đầu thêm tác vụ nhập tệp"
            : "No import job identifiers remain";
        return false;
    }
    try {
        session->metadata = workflow().begin_import_job(job_id);
    } catch (...) {
        restore_open_tab();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không thể tạo phiên nhập cho tài liệu hiện tại"
            : "Could not start an import job for this document";
        return false;
    }
    session->target_tab_id = active_tab().id;
    session->is_open_action = is_open_action;
    session->created_open_tab = created_open_tab;
    session->view_generation = view_generation_;
    session->previous_tab_title = previous_tab_title;
    session->previous_file_path = previous_file_path;
    session->source_filename = filename;
    const HWND notify_window = hwnd_;
    const auto metadata = session->metadata;
    session->worker = std::jthread([raw = session.get(), path_copy = path, format,
                                    metadata, notify_window, raster_frame_index](std::stop_token cancellation) {
        (void)SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        auto result = run_import_worker_job(path_copy, format, metadata, cancellation,
            [raw, notify_window](ImportProgress progress) {
                raw->stage.store(progress.stage, std::memory_order_release);
                raw->records_received.store(progress.records_received, std::memory_order_release);
                raw->source_bytes_copied.store(progress.source_bytes_copied,
                                                std::memory_order_release);
                raw->source_bytes_total.store(progress.source_bytes_total,
                                              std::memory_order_release);
                if (!raw->progress_message_pending.exchange(true,
                        std::memory_order_acq_rel) &&
                    !PostMessageW(notify_window, kImportWorkerMessage, 0, 0))
                    raw->progress_message_pending.store(false, std::memory_order_release);
            }, PdfOpenIntent::EditableObjects, raster_frame_index);
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
    if (import_placement_active()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Hãy đặt hoặc hủy ảnh đang nhập trước khi nhập tệp khác"
            : "Place or cancel the pending image before starting another import";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (import_job_active()) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đang nhập tệp khác · nhấn Esc để hủy"
            : "Another import is running · press Esc to cancel";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    std::wstring path(32768, L'\0');
    const wchar_t* filter = language_ == UILanguage::Vietnamese
        ? L"Định dạng có adapter nhập (*.svg;*.dxf;*.cdr;*.ai;*.pdf;*.eps;*.png;*.jpg;*.jpeg;*.tif;*.tiff;*.bmp)\0*.svg;*.dxf;*.cdr;*.ai;*.pdf;*.eps;*.png;*.jpg;*.jpeg;*.tif;*.tiff;*.bmp\0"
          L"PDF/AI: nhập path, chữ và ảnh theo subset (PDF chỉ trang 1) (*.ai;*.pdf)\0*.ai;*.pdf\0"
          L"EPS: path vector PostScript trong subset an toàn (*.eps)\0*.eps\0"
          L"CorelDRAW CDR: path/chữ/layer subset (*.cdr)\0*.cdr\0"
          L"Ảnh PNG/JPEG/TIFF/BMP (*.png;*.jpg;*.jpeg;*.tif;*.tiff;*.bmp)\0*.png;*.jpg;*.jpeg;*.tif;*.tiff;*.bmp\0"
          L"Vector SVG (*.svg)\0*.svg\0"
          L"Tập con AutoCAD DXF 2D (*.dxf)\0*.dxf\0"
          L"Tất cả tệp\0*.*\0\0"
        : L"Import formats with adapters (*.svg;*.dxf;*.cdr;*.ai;*.pdf;*.eps;*.png;*.jpg;*.jpeg;*.tif;*.tiff;*.bmp)\0*.svg;*.dxf;*.cdr;*.ai;*.pdf;*.eps;*.png;*.jpg;*.jpeg;*.tif;*.tiff;*.bmp\0"
          L"PDF/AI: path, text and image subset (PDF page 1 only) (*.ai;*.pdf)\0*.ai;*.pdf\0"
          L"EPS: safe editable PostScript vector path subset (*.eps)\0*.eps\0"
          L"CorelDRAW CDR path/text/layer subset (*.cdr)\0*.cdr\0"
          L"PNG/JPEG/TIFF/BMP images (*.png;*.jpg;*.jpeg;*.tif;*.tiff;*.bmp)\0*.png;*.jpg;*.jpeg;*.tif;*.tiff;*.bmp\0"
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

bool NativeWindow::import_placement_active() const noexcept {
    return import_placement_ != nullptr;
}

void NativeWindow::cancel_import_placement() {
    if (!import_placement_) return;
    for (auto& tab : tabs_) {
        if (tab.id != import_placement_->target_tab_id || !tab.workflow) continue;
        try { tab.workflow->cancel_import_job(import_placement_->metadata.job_id); } catch (...) {}
        break;
    }
    import_placement_.reset();
    status_msg_ = language_ == UILanguage::Vietnamese
        ? "Đã hủy đặt ảnh · tài liệu không đổi"
        : "Image placement cancelled · document unchanged";
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

bool NativeWindow::handle_import_placement_escape(WPARAM key) {
    if (key != VK_ESCAPE || !import_placement_active()) return false;
    cancel_import_placement();
    return true;
}

bool NativeWindow::place_pending_import_at(int x, int y) {
    if (!import_placement_) return false;
    if (active_tab().id != import_placement_->target_tab_id) return false;
    if (!is_canvas_point(x, y)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Nhấp trong canvas để đặt ảnh · Esc để hủy"
            : "Click inside the canvas to place the image · Esc cancels";
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    DocumentTab* target = nullptr;
    for (auto& tab : tabs_) {
        if (tab.id == import_placement_->target_tab_id) {
            target = &tab;
            break;
        }
    }
    if (!target || !target->workflow || !target->root) {
        cancel_import_placement();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không còn tài liệu đích để đặt ảnh"
            : "The target document is no longer available for image placement";
        return true;
    }
    const auto world = canvas_to_world(x, y);
    document::ImageObject* image = nullptr;
    for (auto& object : import_placement_->proposal.objects) {
        image = std::get_if<document::ImageObject>(&object);
        if (image) break;
    }
    if (!image || !std::isfinite(world.x) || !std::isfinite(world.y) ||
        !std::isfinite(image->bounds_rect.width) ||
        !std::isfinite(image->bounds_rect.height) ||
        image->bounds_rect.width <= 0 || image->bounds_rect.height <= 0) {
        cancel_import_placement();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Kích thước hoặc vị trí ảnh nhập không hợp lệ"
            : "The imported image size or placement is invalid";
        return true;
    }
    image->bounds_rect.x = world.x;
    image->bounds_rect.y = world.y - image->bounds_rect.height;
    const document::ObjectId placed_id = image->id;
    const auto committed = target->workflow->commit_import_proposal(
        import_placement_->metadata, std::move(import_placement_->proposal), false);
    if (committed == document::ProposalStatus::Accepted) {
        last_import_succeeded_ = true;
        target->root->vector_controller().selection().select(placed_id);
        if (active_tab().id == target->id) set_active_tool(0);
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã đặt ảnh · một bước Ctrl+Z · góc trên trái tại "
            : "Image placed · one Ctrl+Z step · top-left at ";
        status_msg_ += format_mm_display_utf8(world.x, display_length_unit_, 3) + ", " +
            format_mm_display_utf8(world.y, display_length_unit_, 3);
    } else {
        try { target->workflow->cancel_import_job(import_placement_->metadata.job_id); } catch (...) {}
        status_msg_ = (committed == document::ProposalStatus::StaleRevision ||
                       committed == document::ProposalStatus::StalePageContext)
            ? (language_ == UILanguage::Vietnamese
                ? "Tài liệu hoặc trang đích đã đổi · ảnh chưa được đặt; hãy nhập lại"
                : "The document or target page changed · image was not placed; import it again")
            : (language_ == UILanguage::Vietnamese
                ? "Không thể đặt ảnh · tài liệu không đổi"
                : "Could not place the image · document unchanged");
    }
    import_placement_.reset();
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void NativeWindow::paint_import_placement_overlay(HDC dc) {
    if (!import_placement_ || !import_placement_->pointer_valid ||
        active_tab().id != import_placement_->target_tab_id) return;
    const document::ImageObject* image = nullptr;
    for (const auto& object : import_placement_->proposal.objects) {
        image = std::get_if<document::ImageObject>(&object);
        if (image) break;
    }
    if (!image) return;
    const RECT area = canvas_rect(width_, height_);
    auto& camera = root().renderer().camera();
    camera.set_viewport_size(area.right - area.left, area.bottom - area.top);
    const auto top_left = camera.world_to_screen(import_placement_->pointer_world);
    const auto bottom_right = camera.world_to_screen({
        import_placement_->pointer_world.x + image->bounds_rect.width,
        import_placement_->pointer_world.y - image->bounds_rect.height});
    const RECT frame{
        area.left + static_cast<LONG>(std::lround(top_left.x)),
        area.top + static_cast<LONG>(std::lround(top_left.y)),
        area.left + static_cast<LONG>(std::lround(bottom_right.x)),
        area.top + static_cast<LONG>(std::lround(bottom_right.y))};

    // Build a screen-resolution, premultiplied preview once per placement.
    // The source remains authoritative in the proposal; this DIB is only a
    // disposable view cache and never duplicates a full-resolution asset when
    // the image is larger than the visible canvas.
    const auto source_pixels = image->rgba_pixels();
    if (image->width_px > 0 && image->height_px > 0 &&
        static_cast<uint64_t>(image->width_px) *
                static_cast<uint64_t>(image->height_px) == source_pixels.size()) {
        const int viewport_width = (std::max)(
            1, static_cast<int>(area.right - area.left));
        const int viewport_height = (std::max)(
            1, static_cast<int>(area.bottom - area.top));
        const auto screen_extent = [](double extent, int viewport_extent) {
            if (!std::isfinite(extent) || extent < 1.0) return 1;
            return static_cast<int>((std::min)(
                static_cast<double>(viewport_extent), std::ceil(extent)));
        };
        const int screen_width = screen_extent(
            std::abs(static_cast<double>(bottom_right.x - top_left.x)),
            viewport_width);
        const int screen_height = screen_extent(
            std::abs(static_cast<double>(bottom_right.y - top_left.y)),
            viewport_height);
        const double preview_scale = (std::min)({
            1.0,
            static_cast<double>(screen_width) / image->width_px,
            static_cast<double>(screen_height) / image->height_px});
        const int desired_width = (std::clamp)(
            static_cast<int>(std::lround(image->width_px * preview_scale)),
            1, image->width_px);
        const int desired_height = (std::clamp)(
            static_cast<int>(std::lround(image->height_px * preview_scale)),
            1, image->height_px);
        if (!import_placement_->preview_bitmap ||
            import_placement_->preview_width_px != desired_width ||
            import_placement_->preview_height_px != desired_height) {
            BITMAPINFO bitmap_info{};
            bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bitmap_info.bmiHeader.biWidth = desired_width;
            bitmap_info.bmiHeader.biHeight = -desired_height;
            bitmap_info.bmiHeader.biPlanes = 1;
            bitmap_info.bmiHeader.biBitCount = 32;
            bitmap_info.bmiHeader.biCompression = BI_RGB;
            void* bitmap_pixels = nullptr;
            HBITMAP preview = CreateDIBSection(
                dc, &bitmap_info, DIB_RGB_COLORS, &bitmap_pixels, nullptr, 0);
            if (preview && bitmap_pixels) {
                auto* destination = static_cast<uint32_t*>(bitmap_pixels);
                for (int y = 0; y < desired_height; ++y) {
                    const auto source_y = static_cast<int64_t>(
                        (static_cast<uint64_t>(2 * static_cast<int64_t>(y) + 1) *
                         static_cast<uint64_t>(image->height_px)) /
                        (2u * static_cast<uint64_t>(desired_height)));
                    for (int x = 0; x < desired_width; ++x) {
                        const auto source_x = static_cast<int64_t>(
                            (static_cast<uint64_t>(2 * static_cast<int64_t>(x) + 1) *
                             static_cast<uint64_t>(image->width_px)) /
                            (2u * static_cast<uint64_t>(desired_width)));
                        const uint32_t rgba = source_pixels[
                            static_cast<size_t>(source_y) * image->width_px +
                            static_cast<size_t>(source_x)];
                        const uint32_t alpha = rgba & 0xffu;
                        const uint32_t red = (((rgba >> 24) & 0xffu) * alpha + 127u) / 255u;
                        const uint32_t green = (((rgba >> 16) & 0xffu) * alpha + 127u) / 255u;
                        const uint32_t blue = (((rgba >> 8) & 0xffu) * alpha + 127u) / 255u;
                        destination[static_cast<size_t>(y) * desired_width + x] =
                            (alpha << 24) | (red << 16) | (green << 8) | blue;
                    }
                }
                if (import_placement_->preview_bitmap)
                    DeleteObject(import_placement_->preview_bitmap);
                import_placement_->preview_bitmap = preview;
                import_placement_->preview_bitmap_pixels = destination;
                import_placement_->preview_width_px = desired_width;
                import_placement_->preview_height_px = desired_height;
            } else if (preview) {
                DeleteObject(preview);
            }
        }
    }

    const int saved_dc = SaveDC(dc);
    IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);
    if (import_placement_->preview_bitmap &&
        import_placement_->preview_width_px > 0 &&
        import_placement_->preview_height_px > 0) {
        HDC preview_dc = CreateCompatibleDC(dc);
        if (preview_dc) {
            const HGDIOBJ old_bitmap = SelectObject(
                preview_dc, import_placement_->preview_bitmap);
            BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            AlphaBlend(dc, frame.left, frame.top,
                       frame.right - frame.left, frame.bottom - frame.top,
                       preview_dc, 0, 0, import_placement_->preview_width_px,
                       import_placement_->preview_height_px, blend);
            SelectObject(preview_dc, old_bitmap);
            DeleteDC(preview_dc);
        }
    }
    HPEN pen = CreatePen(PS_DASH, 1, RGB(0, 170, 205));
    if (!pen) {
        RestoreDC(dc, saved_dc);
        return;
    }
    const HGDIOBJ old_pen = SelectObject(dc, pen);
    const HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, frame.left, frame.top, frame.right, frame.bottom);
    MoveToEx(dc, frame.left - 7, frame.top, nullptr);
    LineTo(dc, frame.left + 7, frame.top);
    MoveToEx(dc, frame.left, frame.top - 7, nullptr);
    LineTo(dc, frame.left, frame.top + 7);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);

    std::wstring label = format_length_pair_mm(
        image->bounds_rect.width, image->bounds_rect.height, display_length_unit_, 3);
    label += language_ == UILanguage::Vietnamese
        ? L" · nhấp để đặt · Esc hủy" : L" · click to place · Esc cancels";
    RECT label_rect{frame.left, frame.top - 24, frame.left + 300, frame.top - 3};
    if (label_rect.top < area.top) {
        label_rect.top = frame.bottom + 3;
        label_rect.bottom = label_rect.top + 21;
    }
    HBRUSH label_background = CreateSolidBrush(RGB(30, 40, 48));
    FillRect(dc, &label_rect, label_background);
    DeleteObject(label_background);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(235, 248, 251));
    DrawTextW(dc, label.c_str(), static_cast<int>(label.size()), &label_rect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RestoreDC(dc, saved_dc);
}

void NativeWindow::handle_import_worker_message(WPARAM kind) {
    if (!import_job_) return;
    if (kind == 0) {
        import_job_->progress_message_pending.store(false, std::memory_order_release);
        if (import_job_active()) {
            const auto stage = import_job_->stage.load(std::memory_order_acquire);
            const size_t count = import_job_->records_received.load(std::memory_order_acquire);
            const uint64_t source_copied = import_job_->source_bytes_copied.load(
                std::memory_order_acquire);
            const uint64_t source_total = import_job_->source_bytes_total.load(
                std::memory_order_acquire);
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
                if (source_total > 0) {
                    constexpr uint64_t kKiB = 1024;
                    constexpr uint64_t kMiB = 1024 * kKiB;
                    const auto format_bytes = [](uint64_t bytes) {
                        if (bytes >= kMiB) return std::to_string(bytes / kMiB) + " MiB";
                        if (bytes >= kKiB) return std::to_string(bytes / kKiB) + " KiB";
                        return std::to_string(bytes) + " B";
                    };
                    const auto percent = source_copied >= source_total ? 100u
                        : static_cast<unsigned>((static_cast<long double>(source_copied) /
                            static_cast<long double>(source_total)) * 100.0L);
                    status_msg_ = (language_ == UILanguage::Vietnamese
                        ? "Đang chép ảnh nguồn · " : "Staging raster source · ") +
                        std::to_string(percent) + "% (" + format_bytes(source_copied) +
                        " / " + format_bytes(source_total) + ")" +
                        (language_ == UILanguage::Vietnamese
                            ? " · Esc để hủy" : " · Esc to cancel");
                } else {
                    status_msg_ = (language_ == UILanguage::Vietnamese
                        ? "Đang nhập · đã nhận " : "Importing · received ") +
                        std::to_string(count) + (language_ == UILanguage::Vietnamese
                            ? " bản ghi · Esc để hủy" : " records · Esc to cancel");
                }
                break;
            case ImportProgressStage::Validating:
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Đang kiểm tra cấu trúc và báo cáo độ trung thực… · Esc để hủy"
                    : "Validating structure and fidelity report… · Esc to cancel";
                break;
            case ImportProgressStage::WaitingForMemory:
                status_msg_ = language_ == UILanguage::Vietnamese
                    ? "Đang tạm chờ RAM hệ thống · tiếp tục khi có bộ nhớ trống · Esc để hủy"
                    : "Waiting for system RAM to free up · Esc to cancel";
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
    const auto rollback_open_tab = [&] {
        if (!session->is_open_action || !target) return;
        const auto current = target->root->document().current_snapshot();
        if (!current || current->document_id() != session->metadata.document_id ||
            current->epoch() != session->metadata.epoch ||
            current->revision() != session->metadata.base_revision) return;
        if (session->created_open_tab) {
            if (tabs_.size() > 1) close_tab(target_index);
        } else {
            target->title = session->previous_tab_title;
            target->file_path = session->previous_file_path;
            if (target_index == active_tab_index_) document_path_ = target->file_path;
        }
    };
    if (!result || !target || !target->workflow || !target->root) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Tác vụ nhập đã kết thúc nhưng tài liệu đích không còn mở"
            : "The import ended after its target document was closed";
        close_session();
        return;
    }
    if (session->cancel_requested.load(std::memory_order_acquire) || result->cancelled) {
        try { target->workflow->cancel_import_job(session->metadata.job_id); } catch (...) {}
        rollback_open_tab();
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đã hủy nhập; tài liệu không đổi"
                                                          : "Import cancelled; document unchanged";
        close_session();
        return;
    }
    if (!result->success) {
        try { target->workflow->cancel_import_job(session->metadata.job_id); } catch (...) {}
        rollback_open_tab();
        const auto message = NativeWindow::utf8_to_wide(result->error_message);
        status_msg_ = language_ == UILanguage::Vietnamese ? "Nhập tệp thất bại" : "Import failed";
        if (IsWindowVisible(hwnd_)) {
            MessageBoxW(hwnd_, message.c_str(),
                language_ == UILanguage::Vietnamese ? L"Không thể nhập tệp" : L"Could not import file",
                MB_OK | MB_ICONWARNING);
        } else if (!result->error_message.empty()) {
            status_msg_ += ": ";
            status_msg_ += result->error_message;
        }
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

    size_t object_count = result->proposal.objects.size();
    for (const auto& page : result->proposal.pages)
        object_count += page.objects.size();
    const size_t layer_count = result->proposal.layers.size();
    const auto& proposal = result->proposal;
    const bool is_raster = proposal.profile_id == "raster-rgba8-v1";
    const bool is_pdf_subset = proposal.profile_id == "pdf-page-vector-v2" ||
                               proposal.profile_id == "ai-pdf-compatible-vector-v2" ||
                               proposal.profile_id == "pdf-page-set-v3" ||
                               proposal.profile_id == "ai-pdf-compatible-page-set-v3";
    const bool is_eps_subset = proposal.profile_id == "eps-path-v1";
    const bool is_cdr_subset = proposal.profile_id == "cdr-libcdr-svg-v2";

    // A raster import is a placement gesture: after the isolated worker has
    // validated the image, show the cursor preview immediately. The review
    // dialog used to block this common path even though the source and ICC
    // metadata are retained; keep its color-fidelity caveat visible in status.
    if (is_raster && !session->is_open_action && !auto_accept_import_review_) {
        const auto imported_image = result->proposal.objects.size() == 1
            ? std::get_if<document::ImageObject>(&result->proposal.objects.front())
            : nullptr;
        if (!imported_image || !result->proposal.pages.empty()) {
            try { target->workflow->cancel_import_job(session->metadata.job_id); } catch (...) {}
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đề xuất ảnh không hợp lệ · tài liệu không đổi"
                : "The raster image proposal is invalid · document unchanged";
            close_session();
            return;
        }
        try {
            import_placement_ = std::make_unique<ImportPlacementSession>();
            import_placement_->metadata = session->metadata;
            import_placement_->proposal = std::move(result->proposal);
            import_placement_->target_tab_id = session->target_tab_id;
            import_placement_->source_filename = session->source_filename;
            if (target_index == active_tab_index_ && is_canvas_point(
                    last_canvas_pointer_x_, last_canvas_pointer_y_)) {
                import_placement_->pointer_world = canvas_to_world(
                    last_canvas_pointer_x_, last_canvas_pointer_y_);
                import_placement_->pointer_valid = true;
            }
        } catch (...) {
            try { target->workflow->cancel_import_job(session->metadata.job_id); } catch (...) {}
            import_placement_.reset();
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Không đủ bộ nhớ để mở xem trước vị trí ảnh"
                : "Could not prepare the image placement preview";
            close_session();
            return;
        }
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Di chuyển ảnh · nhấp để đặt · Esc hủy · giữ ICC nguồn, màu xem trước chưa xác minh"
            : "Move image · click to place · Esc cancels · source ICC retained, preview color unverified";
        close_session();
        return;
    }

    std::wostringstream content;
    content << (language_ == UILanguage::Vietnamese ? L"Nguồn: " : L"Source: ")
            << NativeWindow::utf8_to_wide(proposal.format_name).c_str() << L" · "
            << (language_ == UILanguage::Vietnamese ? L"trang nguồn · " : L"source pages · ")
            << (proposal.pages.empty() ? 1 : proposal.pages.size()) << L" · "
            << format_length_pair_mm(proposal.source_page_width_mm,
                                     proposal.source_page_height_mm,
                                     display_length_unit_, 4) << L"\n"
            << object_count << (language_ == UILanguage::Vietnamese
                ? (is_raster ? L" ảnh raster · " : L" đối tượng chỉnh sửa được · ")
                : (is_raster ? L" raster image · " : L" editable objects · "))
            << layer_count << (language_ == UILanguage::Vietnamese ? L" layer nguồn\n" : L" source layers\n")
            << (language_ == UILanguage::Vietnamese ? L"Độ bao phủ: một phần · Hiển thị: chưa xác minh · Sẵn sàng sản xuất: chưa đánh giá."
                                                    : L"Coverage: partial · Appearance: unverified · Production: not assessed.");

    std::wstring details;
    if (is_raster) {
        details = language_ == UILanguage::Vietnamese
            ? L"Ảnh được giải mã thành pixel RGBA8 để hiển thị; byte nguồn, DPI, hướng EXIF và ICC được giữ trong tài liệu. Chuyển đổi màu chưa được chứng nhận giống tuyệt đối giữa các ứng dụng.\n"
              L"Ảnh raster không phải đường vector để cắt CNC. Độ phân giải nguồn và việc chuyển sang RGBA8 có thể giới hạn chất lượng phóng to. Khổ trang tài liệu hiện tại không đổi."
            : L"The image is decoded to RGBA8 pixels for display; source bytes, DPI, EXIF orientation and ICC data are retained in the document. Color conversion is not certified to match every application exactly.\n"
              L"A raster image is not vector geometry for CNC cutting. Source resolution and RGBA8 conversion can limit enlargement quality. The current document page size stays unchanged.";
    } else if (is_pdf_subset) {
        details = language_ == UILanguage::Vietnamese
            ? L"Các trang PDF được đọc thành path/chữ chỉnh sửa được theo từng trang; trang trắng vẫn được giữ. Hệ thống từ chối toàn bộ nếu một trang không đọc được. Hiệu ứng, clipping, ảnh nhúng, profile màu và overprint chưa được giữ đầy đủ. Với AI, đây chỉ là representation PDF-compatible; dữ liệu authoring riêng của Illustrator chưa được đọc. Không bảo đảm khớp giao diện, round-trip hay sản xuất CNC."
            : L"PDFium maps supported paths, text and placed images into editable objects on each source page; blank pages are retained. The whole proposal is rejected if any selected page cannot be read. Effects, clipping, source color profiles and overprint are not fully preserved. For AI, this reads only its PDF-compatible representation; Illustrator authoring data is not read. Appearance, round-trip and CNC production are not certified.";
    } else if (is_eps_subset) {
        details = language_ == UILanguage::Vietnamese
            ? L"DOS EPS được diễn giải trong Ghostscript chạy bằng AppContainer, không có quyền mạng và không được tạo process con; plain DSC EPS dùng bộ đọc operator allowlist. DOS EPS được chuyển tạm sang PDF rồi ánh xạ thành đối tượng PikDraw có thể sửa; ảnh nhúng có thể thành ImageObject. Cách này giữ nội dung trong giới hạn PDFium hỗ trợ, không giữ đầy đủ layer nguồn, hiệu ứng live, clipping, profile màu hay metadata EPS. Hình thức, round-trip và sản xuất chưa được xác minh."
            : L"DOS EPS is interpreted by Ghostscript in an AppContainer with no network capability or child processes; plain DSC EPS uses the allowlisted operator reader. DOS EPS is converted temporarily to PDF and mapped into editable PikDraw objects; embedded images may become ImageObjects. This retains only the content supported by the PDFium mapping and does not preserve source layers, live effects, clipping, color profiles or EPS metadata in full. Appearance, round-trip and production are not assessed.";
    } else if (is_cdr_subset) {
        details = language_ == UILanguage::Vietnamese
            ? L"libcdr chuyển trang CDR thành path/chữ/group chỉnh sửa được; layer cấp cao có mã Layer được ánh xạ sang layer PikDraw. Các đoạn chữ khác font/cỡ có thể được gộp theo font đầu tiên; cap/join và style chưa hỗ trợ có thể đổi hình thức. Hiệu ứng live, clipping, gradient, bitmap, tên layer, quản lý màu và dữ liệu authoring Corel không được giữ đầy đủ. Đối tượng ngoài subset làm từ chối toàn bộ file. Không nhập thumbnail; không bảo đảm appearance, round-trip hay sản xuất CNC."
            : L"libcdr converts a CDR page into editable path/text/group objects; top-level groups with Layer IDs map to PikDraw layers. Text runs with different fonts or sizes may be merged using the first font; unsupported cap/join and styles may change appearance. Live effects, clipping, gradients, bitmaps, source layer names, color management and Corel authoring data are not fully preserved. Unsupported generated objects reject the whole file. No thumbnail is imported; appearance, round-trip and CNC production are not certified.";
    } else {
        details = language_ == UILanguage::Vietnamese
            ? L"Các đơn vị SVG được quy đổi sang mm. DXF dùng $INSUNITS của nguồn; thiếu đơn vị được hỗ trợ thì từ chối.\n"
              L"Khổ trang tài liệu hiện tại không đổi. Geometry và layer nguồn sẽ được thêm vào trang hiện tại.\n"
              L"Báo cáo nhập đã duyệt được lưu cùng tài liệu .pr; tài nguyên nguồn được nhúng theo khả năng của định dạng. Import không chứng minh export ngược hoặc khả năng sản xuất."
            : L"SVG source units are converted to mm. DXF uses source $INSUNITS; unsupported or missing units are rejected.\n"
              L"The current document page size stays unchanged. Source geometry and layers are added to its current page.\n"
              L"The reviewed import report is stored in the .pr document; source resources are embedded according to format support. Import does not prove round-trip export or production readiness.";
    }
    if (is_pdf_subset || is_eps_subset || is_cdr_subset) {
        details += language_ == UILanguage::Vietnamese
            ? (session->is_open_action
              ? L"\nOpen tạo tài liệu mới với đầy đủ trang nguồn và khổ riêng từng trang. Báo cáo nhập đã duyệt được lưu trong .pr; nguồn nhúng tùy định dạng."
                : L"\nImport thêm trang nguồn sau trang đang chọn và giữ nguyên nội dung hiện tại. Báo cáo nhập đã duyệt được lưu trong .pr; nguồn nhúng tùy định dạng.")
            : (session->is_open_action
                ? L"\nOpen creates a document with every source page and its own dimensions. The reviewed import report is saved in .pr; embedded sources depend on format support."
                : L"\nImport inserts source pages after the active page and leaves existing content in place. The reviewed import report is saved in .pr; embedded sources depend on format support.");
    }
    for (const auto& issue : proposal.issues) {
        details += L"\n• ";
        switch (issue.severity) {
        case ImportIssueSummary::Severity::Info:
            details += language_ == UILanguage::Vietnamese ? L"Thông tin: " : L"Info: ";
            break;
        case ImportIssueSummary::Severity::Warning:
            details += language_ == UILanguage::Vietnamese ? L"Cảnh báo: " : L"Warning: ";
            break;
        case ImportIssueSummary::Severity::Unsupported:
            details += language_ == UILanguage::Vietnamese ? L"Không hỗ trợ: " : L"Unsupported: ";
            break;
        }
        if (issue.feature_code == "font_resolution") {
            details += language_ == UILanguage::Vietnamese
                ? L"Giữ tên font; hình thức chữ chưa được xác minh."
                : L"Font family is retained; text appearance is unverified.";
        } else if (issue.feature_code == "aci_color_7_context") {
            details += language_ == UILanguage::Vietnamese
                ? L"Màu DXF ACI 7 phụ thuộc nền CAD; preview dùng màu trắng."
                : L"DXF ACI 7 depends on CAD background; preview uses white.";
        } else if (issue.feature_code == "postscript_conversion") {
            details += language_ == UILanguage::Vietnamese
                ? L"DOS EPS được chuyển qua PDF trong worker cô lập; các phần ngoài ánh xạ PDFium không được giữ đầy đủ."
                : L"DOS EPS was normalized through PDF in an isolated worker; content outside the PDFium mapping is not fully retained.";
        } else {
            details += language_ == UILanguage::Vietnamese
                ? L"Có chẩn đoán giới hạn subset trong báo cáo nhập."
                : L"The import report contains an additional subset diagnostic.";
        }
        if (!issue.source_locator.empty()) {
            details += language_ == UILanguage::Vietnamese ? L" [Nguồn: " : L" [Source: ";
            details += NativeWindow::utf8_to_wide(issue.source_locator);
            if (!issue.diagnostic_code.empty()) {
                details += L" · ";
                details += NativeWindow::utf8_to_wide(issue.diagnostic_code);
            }
            details += L"]";
        }
    }

    const std::wstring title = language_ == UILanguage::Vietnamese ? L"Xem lại kết quả nhập" : L"Review import fidelity";
    const std::wstring instruction = language_ == UILanguage::Vietnamese
        ? ((is_pdf_subset || is_eps_subset || is_cdr_subset) ? (session->is_open_action
              ? L"Mở tài liệu path/chữ subset này và chấp nhận các phần không được giữ?"
              : L"Chỉ nhập subset path/chữ này và chấp nhận các phần không được giữ?")
          : is_raster ? L"Nhập ảnh raster đã chuyển đổi với các giới hạn hiển thị nêu trên?"
                     : L"Chỉ nhập các đối tượng được biểu diễn trong subset hiện tại?")
        : ((is_pdf_subset || is_eps_subset || is_cdr_subset) ? (session->is_open_action
              ? L"Open an editable path/text document from this subset and accept the listed losses?"
              : L"Import only this editable path/text subset and accept the listed losses?")
          : is_raster ? L"Import the converted raster image with these display limitations?"
                     : L"Import only the objects represented by the current subset?");
    const std::wstring apply_label = language_ == UILanguage::Vietnamese
        ? ((is_pdf_subset || is_eps_subset || is_cdr_subset) ? (session->is_open_action ? L"Mở subset" : L"Nhập subset")
          : is_raster ? L"Nhập ảnh" : L"Nhập đối tượng")
        : ((is_pdf_subset || is_eps_subset || is_cdr_subset) ? (session->is_open_action ? L"Open subset" : L"Import subset")
          : is_raster ? L"Import image" : L"Import editable subset");
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
    if (auto_accept_import_review_) {
        choice = kImportChoiceApply;
    } else if (session->is_open_action && !is_pdf_subset && !is_eps_subset && !is_cdr_subset) {
        choice = kImportChoiceApply;
    } else {
        using TaskDialogFunction = HRESULT(WINAPI*)(const TASKDIALOGCONFIG*, int*, int*, BOOL*);
        HMODULE common_controls = LoadLibraryExW(L"comctl32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
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
        rollback_open_tab();
        status_msg_ = language_ == UILanguage::Vietnamese ? "Đã hủy nhập; tài liệu không đổi"
                                                          : "Import cancelled; document unchanged";
        close_session();
        return;
    }

    const auto committed = target->workflow->commit_import_proposal(
        session->metadata, std::move(result->proposal), session->is_open_action);
    if (committed == document::ProposalStatus::Accepted) {
        last_import_succeeded_ = true;
        if (session->is_open_action) {
            const auto opened_snapshot = target->root->document().current_snapshot();
            if (opened_snapshot) {
                target->page_width_mm = opened_snapshot->page().width_mm;
                target->page_height_mm = opened_snapshot->page().height_mm;
                target->artboard_overview = settings_artboard_overview_choice_;
                target->active_artboard_id = opened_snapshot->active_page().artboards.empty()
                    ? document::kInvalidArtboardId
                    : opened_snapshot->active_page().artboards.front().id;
            }
        }
        const bool target_active = target_index == active_tab_index_;
        const auto& target_document = target->root->document();
        const bool target_view_current = target_active &&
            view_generation_ == session->view_generation &&
            target_document.active_page_id() == session->metadata.target_page_id &&
            target_document.page_navigation_generation() ==
                session->metadata.page_navigation_generation;
        bool cdr_content_outside_page = false;
        if (target_view_current) {
            if (is_cdr_subset) {
                if (!fit_document_content_to_canvas(&cdr_content_outside_page))
                    fit_page_to_canvas();
            } else {
                fit_page_to_canvas();
            }
            root().vector_controller().selection().clear();
            set_active_tool(0);
        }
        close_recovery_modal();
        if (session->is_open_action) {
            target->file_path.clear();
            status_msg_ = (language_ == UILanguage::Vietnamese ? "Đã mở tệp: " : "Opened file: ") +
                session->source_filename;
            if (target_active && is_cdr_subset && cdr_content_outside_page) {
                status_msg_ += language_ == UILanguage::Vietnamese
                    ? " · nội dung nằm ngoài trang, tọa độ được giữ; nhấn F để vừa trang"
                    : " · artwork extends beyond the page; coordinates preserved; press F to fit page";
            }
        } else {
            const std::string count_text = std::to_string(object_count);
            if (target_active) {
                status_msg_ = (language_ == UILanguage::Vietnamese ? "Đã nhập " : "Imported ") +
                    count_text + (language_ == UILanguage::Vietnamese
                        ? " đối tượng · một bước Ctrl+Z · khổ trang giữ nguyên"
                        : " objects · one Ctrl+Z step · page size unchanged");
                if (is_cdr_subset && cdr_content_outside_page) {
                    status_msg_ += language_ == UILanguage::Vietnamese
                        ? " · nội dung nằm ngoài trang, tọa độ được giữ; nhấn F để vừa trang"
                        : " · artwork extends beyond the page; coordinates preserved; press F to fit page";
                }
            }
        }
    } else {
        status_msg_ = (committed == document::ProposalStatus::StaleRevision ||
                       committed == document::ProposalStatus::StalePageContext)
            ? (language_ == UILanguage::Vietnamese
                ? "Tài liệu hoặc trang đích đã đổi · đề xuất nhập bị từ chối"
                : "The document or target page changed · import proposal rejected")
            : (language_ == UILanguage::Vietnamese ? "Không thể áp dụng đề xuất nhập"
                                                  : "Could not apply import proposal");
    }
    close_session();
}

#endif

} // namespace pikdraw::desktop
