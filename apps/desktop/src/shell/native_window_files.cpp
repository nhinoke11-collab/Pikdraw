#include "pikdraw/desktop/shell/native_window.hpp"

#include <algorithm>
#include <cmath>

#ifdef _WIN32
#include <commdlg.h>
#endif

namespace pikdraw::desktop {

#ifdef _WIN32
namespace {

std::string wide_to_utf8(const wchar_t* value) {
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

void NativeWindow::open_document_dialog() {
    std::wstring path(32768, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd_;
    dialog.lpstrFilter = kPikdrawFilter;
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrDefExt = L"pikdraw";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog))
        return;

    if (root_.document().is_dirty() && root_.document().current_snapshot()->object_count() > 0) {
        const auto prompt = utf8_to_wide(language_ == UILanguage::Vietnamese
            ? "Bỏ các thay đổi chưa lưu và mở tệp khác?"
            : "Discard unsaved changes and open another file?");
        const auto title = utf8_to_wide(language_ == UILanguage::Vietnamese
            ? "Mở tài liệu" : "Open document");
        if (MessageBoxW(hwnd_, prompt.c_str(), title.c_str(), MB_YESNO | MB_ICONWARNING) != IDYES)
            return;
    }

    const auto utf8_path = wide_to_utf8(path.c_str());
    if (utf8_path.empty() || !workflow_.open_native(utf8_path)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không thể mở tệp PikDraw" : "Could not open PikDraw file";
    } else {
        document_path_ = utf8_path;
        root_.vector_controller().selection().clear();
        set_active_tool(0);
        fit_page_to_canvas();
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã mở tài liệu PikDraw" : "PikDraw document opened";
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativeWindow::save_document_dialog(bool force_save_as) {
    std::string path = document_path_;
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

    if (path.empty() || !workflow_.save_native(path)) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Không thể lưu tài liệu" : "Could not save document";
    } else {
        document_path_ = path;
        status_msg_ = language_ == UILanguage::Vietnamese
            ? "Đã lưu tài liệu PikDraw" : "PikDraw document saved";
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativeWindow::export_svg_dialog() {
    std::wstring path(32768, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd_;
    dialog.lpstrFilter = L"SVG vector (*.svg)\0*.svg\0All files\0*.*\0\0";
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrDefExt = L"svg";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&dialog))
        export_svg_to(wide_to_utf8(path.c_str()));
}

#endif

} // namespace pikdraw::desktop
