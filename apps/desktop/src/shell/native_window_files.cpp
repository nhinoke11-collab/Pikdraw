#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/document/storage/save_recovery.hpp"
#include "../utf8_path.hpp"

#include <algorithm>
#include <cmath>

#ifdef _WIN32
#include <commdlg.h>
#include <objbase.h>
#endif

namespace pikdraw::desktop {

#ifdef _WIN32
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
    L"PikDraw document (*.pikdraw)\0*.pikdraw\0All files\0*.*\0\0";

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
    // All writes run on the document's existing UI writer, from immutable snapshots.
    for (auto& tab : tabs_) {
        auto& doc = tab.root->document();
        if (!doc.is_dirty() || (tab.recovery_written && tab.recovery_epoch == doc.epoch()
            && tab.recovery_revision == doc.revision())) continue;
        const auto directory = recovery_directory_for_tab(tab);
        if (directory.empty()) continue;
        const auto snapshot = doc.current_snapshot();
        const auto previous = SaveRecoveryService::find_latest_checkpoint(directory, doc.id());
        std::string error;
        if (!SaveRecoveryService::write_recovery_checkpoint(directory, doc.id(), *snapshot, &error)) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Không thể tạo bản phục hồi tự động" : "Could not write auto-recovery checkpoint";
            continue;
        }
        tab.recovery_revision = snapshot->revision();
        tab.recovery_written = true;
        // Keep the last successful checkpoint until its replacement is safely on disk.
        if (previous) {
            std::error_code ignored;
            std::filesystem::remove(detail::path_from_utf8(previous->path), ignored);
        }
    }
}

void NativeWindow::open_document_dialog() {
    if (is_inline_text_editing_) commit_inline_text_editing();
    std::wstring path(32768, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd_;
    dialog.lpstrFilter = language_ == UILanguage::Vietnamese
        ? L"Định dạng có adapter mở (*.pikdraw;*.svg;*.dxf;*.cdr;*.ai;*.pdf;*.eps;*.png;*.jpg;*.jpeg;*.tif;*.tiff)\0*.pikdraw;*.svg;*.dxf;*.cdr;*.ai;*.pdf;*.eps;*.png;*.jpg;*.jpeg;*.tif;*.tiff\0"
          L"PDF/AI: path và chữ của một trang (subset) (*.ai;*.pdf)\0*.ai;*.pdf\0"
          L"EPS: path vector PostScript trong subset an toàn (*.eps)\0*.eps\0"
          L"CorelDRAW CDR: path/chữ/layer subset (*.cdr)\0*.cdr\0"
          L"Tài liệu PikDraw (*.pikdraw)\0*.pikdraw\0"
          L"Đồ họa Vector (*.svg;*.dxf;*.cdr;*.ai;*.pdf;*.eps)\0*.svg;*.dxf;*.cdr;*.ai;*.pdf;*.eps\0"
          L"Hình ảnh raster (*.png;*.jpg;*.jpeg;*.tif;*.tiff)\0*.png;*.jpg;*.jpeg;*.tif;*.tiff\0"
          L"Tất cả tệp (*.*)\0*.*\0\0"
        : L"Open formats with adapters (*.pikdraw;*.svg;*.dxf;*.cdr;*.ai;*.pdf;*.eps;*.png;*.jpg;*.jpeg;*.tif;*.tiff)\0*.pikdraw;*.svg;*.dxf;*.cdr;*.ai;*.pdf;*.eps;*.png;*.jpg;*.jpeg;*.tif;*.tiff\0"
          L"PDF/AI: single-page path and text subset (*.ai;*.pdf)\0*.ai;*.pdf\0"
          L"EPS: safe editable PostScript vector path subset (*.eps)\0*.eps\0"
          L"CorelDRAW CDR path/text/layer subset (*.cdr)\0*.cdr\0"
          L"PikDraw Document (*.pikdraw)\0*.pikdraw\0"
          L"Vector Drawings (*.svg;*.dxf;*.cdr;*.ai;*.pdf;*.eps)\0*.svg;*.dxf;*.cdr;*.ai;*.pdf;*.eps\0"
          L"Raster Images (*.png;*.jpg;*.jpeg;*.tif;*.tiff)\0*.png;*.jpg;*.jpeg;*.tif;*.tiff\0"
          L"All Files (*.*)\0*.*\0\0";
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrDefExt = L"pikdraw";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog))
        return;

    const std::wstring extension = std::filesystem::path(path.c_str()).extension().wstring();
    if (_wcsicmp(extension.c_str(), L".pikdraw") == 0) {
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
        if (utf8_path.empty() || !workflow().open_native(utf8_path)) {
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Không thể mở tệp PikDraw" : "Could not open PikDraw file";
        } else {
            active_tab().file_path = utf8_path;
            document_path_ = utf8_path;
            root().vector_controller().selection().clear();
            set_active_tool(0);
            fit_page_to_canvas();
            status_msg_ = language_ == UILanguage::Vietnamese
                ? "Đã mở tài liệu PikDraw" : "PikDraw document opened";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    import_file_to_tab(path.c_str(), true);
}

void NativeWindow::save_document_dialog(bool force_save_as) {
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
        dialog.lpstrDefExt = L"pikdraw";
        dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameW(&dialog))
            return;
        path = wide_to_utf8(wide_path.c_str());
    }

    if (path.empty() || !workflow().save_native(path)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không thể lưu tài liệu" : "Could not save document";
    } else {
        active_tab().file_path = path;
        document_path_ = path;
        const auto directory = recovery_directory_for_tab(active_tab());
        if (!directory.empty())
            document::SaveRecoveryService::cleanup_checkpoints(directory, root().document().id());
        active_tab().recovery_written = false;
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã lưu tài liệu PikDraw" : "PikDraw document saved";
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativeWindow::export_svg_dialog(bool selected_objects, bool all_pages) {
    std::wstring path(32768, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd_;
    dialog.lpstrFilter = L"SVG vector (*.svg)\0*.svg\0\0";
    dialog.lpstrTitle = all_pages
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
                      !all_pages, all_pages);
}

#endif

} // namespace pikdraw::desktop
