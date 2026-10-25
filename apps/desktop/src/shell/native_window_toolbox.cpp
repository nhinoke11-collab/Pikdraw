#include "native_window_toolbox.hpp"
#include "pikdraw/desktop/shell/native_window.hpp"

namespace pikdraw::desktop {

ToolboxSlotInfo get_toolbox_slot_info(ToolboxSlotId id) noexcept {
    switch (id) {
    case ToolboxSlotId::Pick:
        return {ToolboxSlotId::Pick, 0, 0, 0, L"Pick Tool", L"Công cụ chọn & di chuyển", L"V"};
    case ToolboxSlotId::Node:
        return {ToolboxSlotId::Node, 11, 0, 15, L"Node Edit Tool", L"Chỉnh sửa điểm (Node)", L"F10"};
    case ToolboxSlotId::QuickShapes:
        return {ToolboxSlotId::QuickShapes, 1, 1, 1, L"Quick Shapes", L"Hình cơ bản", L"R, E"};
    case ToolboxSlotId::PathDrawing:
        return {ToolboxSlotId::PathDrawing, 6, 2, 6, L"Path Drawing & Pen", L"Đường vẽ & Bút Pen", L"L, Shift+L"};
    case ToolboxSlotId::Pan:
        return {ToolboxSlotId::Pan, 3, 0, 3, L"Pan Hand Tool", L"Kéo trượt màn hình", L"H"};
    case ToolboxSlotId::Text:
        return {ToolboxSlotId::Text, 4, 0, 4, L"Text & Table Tool", L"Văn bản & Bảng", L"F8"};
    case ToolboxSlotId::Knife:
        return {ToolboxSlotId::Knife, 5, 6, 5, L"Cut & Shape Tools", L"Cắt, tẩy & tạo vùng", L"K"};
    case ToolboxSlotId::Blur:
        return {ToolboxSlotId::Blur, -1, 3, 11, L"Effects", L"Hiệu ứng", L""};
    case ToolboxSlotId::DropShadow:
        return {ToolboxSlotId::DropShadow, -2, 0, 12, L"Drop Shadow", L"Hiệu ứng đổ bóng", L""};
    case ToolboxSlotId::Contour:
        return {ToolboxSlotId::Contour, 12, 0, 14, L"Contour Tool", L"Tạo viền Contour", L"Ctrl+F9"};
    case ToolboxSlotId::Gradient:
        return {ToolboxSlotId::Gradient, 13, 4, 13, L"Gradient Tool", L"Công cụ chuyển sắc (Gradient)", L"G"};
    case ToolboxSlotId::Eyedropper:
        return {ToolboxSlotId::Eyedropper, 15, 4, 18, L"Color Eyedropper", L"Hút màu & Sao chép kiểu", L"I"};
    case ToolboxSlotId::Dimension:
        return {ToolboxSlotId::Dimension, 10, 0, 16, L"Dimension Tool", L"Công cụ đo kích thước (CAD)", L"D"};
    case ToolboxSlotId::Artboard:
        return {ToolboxSlotId::Artboard, 14, 0, 17, L"Artboard Tool", L"Công cụ Artboard", L"Shift+O"};
    case ToolboxSlotId::Annotation:
        return {ToolboxSlotId::Annotation, 17, 7, 20, L"Annotation & Detail Views", L"Chú thích & Ô chi tiết", L""};
    default:
        return {ToolboxSlotId::Pick, 0, 0, 0, L"", L"", L""};
    }
}

bool NativeWindow::is_toolbox_slot_visible(ToolboxSlotId slot) const noexcept {
    if (slot == ToolboxSlotId::Pick) return true; // Pick is always visible
    const int idx = static_cast<int>(slot);
    return (toolbox_visible_mask_ & (1u << idx)) != 0;
}

void NativeWindow::set_toolbox_slot_visible(ToolboxSlotId slot, bool visible) noexcept {
    if (slot == ToolboxSlotId::Pick) return;
    const int idx = static_cast<int>(slot);
    if (visible) {
        toolbox_visible_mask_ |= (1u << idx);
    } else {
        toolbox_visible_mask_ &= ~(1u << idx);
    }
}

void NativeWindow::toggle_toolbox_slot_visible(ToolboxSlotId slot) noexcept {
    if (slot == ToolboxSlotId::Pick) return;
    const int idx = static_cast<int>(slot);
    toolbox_visible_mask_ ^= (1u << idx);
}

void NativeWindow::reset_toolbox_slots_default() noexcept {
    toolbox_visible_mask_ = 0x7FFF; // all 15 slots
    toolbox_edit_mode_ = false;
}

std::vector<ToolboxSlotId> NativeWindow::visible_toolbox_slots() const {
    std::vector<ToolboxSlotId> list;
    list.reserve(static_cast<size_t>(ToolboxSlotId::Count));
    for (int i = 0; i < static_cast<int>(ToolboxSlotId::Count); ++i) {
        const auto slot = static_cast<ToolboxSlotId>(i);
        if (is_toolbox_slot_visible(slot)) {
            list.push_back(slot);
        }
    }
    return list;
}

int NativeWindow::get_visible_row_for_slot(ToolboxSlotId slot) const {
    const auto list = visible_toolbox_slots();
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i] == slot) {
            return (toolbox_columns_ == 2) ? static_cast<int>(i / 2) : static_cast<int>(i);
        }
    }
    return -1;
}

int NativeWindow::get_visible_index_for_slot(ToolboxSlotId slot) const noexcept {
    const auto list = visible_toolbox_slots();
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i] == slot) return static_cast<int>(i);
    }
    return -1;
}

void NativeWindow::set_toolbox_columns(int cols) noexcept {
    toolbox_columns_ = (cols == 1) ? 1 : 2;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativeWindow::toggle_toolbox_columns() noexcept {
    toolbox_columns_ = (toolbox_columns_ == 2) ? 1 : 2;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

int NativeWindow::toolbox_width() const noexcept {
    return (toolbox_columns_ == 2) ? 86 : 52;
}

RECT NativeWindow::get_toolbox_toggle_columns_rect() const noexcept {
    const int tool_w = toolbox_width();
    return RECT{6, 104, tool_w - 6, 120};
}

RECT NativeWindow::get_toolbox_slot_rect(int index) const noexcept {
    const int tools_start_y = 124;
    if (toolbox_columns_ == 2) {
        const int col = index % 2;
        const int row = index / 2;
        const int tx = 6 + col * 39;
        const int ty = tools_start_y + row * 38;
        return RECT{tx, ty, tx + 36, ty + 34};
    } else {
        const int ty = tools_start_y + index * 40;
        return RECT{6, ty, 46, ty + 36};
    }
}

RECT NativeWindow::get_toolbox_plus_rect(int total_slots) const noexcept {
    const int tools_start_y = 124;
    const int tool_w = toolbox_width();
    if (toolbox_columns_ == 2) {
        const int num_rows = (total_slots + 1) / 2;
        const int plus_y = tools_start_y + num_rows * 38;
        return RECT{6, plus_y, tool_w - 6, plus_y + 26};
    } else {
        const int plus_y = tools_start_y + total_slots * 40;
        return RECT{6, plus_y, tool_w - 6, plus_y + 30};
    }
}

} // namespace pikdraw::desktop
