#pragma once

#include <array>
#include <optional>

namespace pikdraw::desktop {

enum class FileMenuAction {
    NewDocument,
    Open,
    Save,
    SaveAs,
    ExportSvg,
    Undo,
    Redo,
    Exit,
    Separator
};

struct FileMenuEntry {
    FileMenuAction action;
    const wchar_t* label_vi;
    const wchar_t* label_en;
    const wchar_t* shortcut;
};

inline constexpr std::array<FileMenuEntry, 10> kFileMenuEntries{{
    {FileMenuAction::NewDocument, L"Bản vẽ mới", L"New document", L"Ctrl+N"},
    {FileMenuAction::Open, L"Mở tệp PikDraw…", L"Open PikDraw file…", L"Ctrl+O"},
    {FileMenuAction::Save, L"Lưu", L"Save", L"Ctrl+S"},
    {FileMenuAction::SaveAs, L"Lưu thành…", L"Save as…", L"Ctrl+Shift+S"},
    {FileMenuAction::ExportSvg, L"Xuất SVG…", L"Export SVG…", L"Ctrl+E"},
    {FileMenuAction::Separator, L"", L"", L""},
    {FileMenuAction::Undo, L"Hoàn tác", L"Undo", L"Ctrl+Z"},
    {FileMenuAction::Redo, L"Làm lại", L"Redo", L"Ctrl+Y"},
    {FileMenuAction::Separator, L"", L"", L""},
    {FileMenuAction::Exit, L"Thoát PikDraw", L"Exit PikDraw", L"Alt+F4"},
}};

inline constexpr int kFileMenuRowHeight = 27;
inline constexpr int kFileMenuSeparatorHeight = 9;
inline constexpr int kFileMenuPadding = 4;

inline constexpr int file_menu_height() noexcept {
    int height = 2 * kFileMenuPadding;
    for (const auto& entry : kFileMenuEntries) {
        height += entry.action == FileMenuAction::Separator
            ? kFileMenuSeparatorHeight : kFileMenuRowHeight;
    }
    return height;
}

inline std::optional<FileMenuAction> file_menu_action_at(int offset_y) noexcept {
    int y = kFileMenuPadding;
    for (const auto& entry : kFileMenuEntries) {
        const int row_height = entry.action == FileMenuAction::Separator
            ? kFileMenuSeparatorHeight : kFileMenuRowHeight;
        if (offset_y >= y && offset_y < y + row_height &&
            entry.action != FileMenuAction::Separator)
            return entry.action;
        y += row_height;
    }
    return std::nullopt;
}

} // namespace pikdraw::desktop
