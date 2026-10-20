#include "native_window_toolbox.hpp"
#include "pikdraw/desktop/shell/native_window.hpp"

namespace pikdraw::desktop {

ToolboxSlotInfo get_toolbox_slot_info(ToolboxSlotId id) noexcept {
    switch (id) {
    case ToolboxSlotId::Pick:
        return {ToolboxSlotId::Pick, 0, 0, 0, L"Pick Tool", L"Công cụ chọn & di chuyển", L"V, Space"};
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
        return {ToolboxSlotId::Knife, 5, 0, 5, L"Knife / Slice Tool", L"Dao cắt xén vector", L"K"};
    case ToolboxSlotId::Blur:
        return {ToolboxSlotId::Blur, -1, 0, 11, L"Gaussian Blur", L"Hiệu ứng làm mờ", L""};
    case ToolboxSlotId::DropShadow:
        return {ToolboxSlotId::DropShadow, -2, 0, 12, L"Drop Shadow", L"Hiệu ứng đổ bóng", L""};
    case ToolboxSlotId::Contour:
        return {ToolboxSlotId::Contour, 12, 0, 14, L"Contour Tool", L"Tạo viền Contour", L"Ctrl+F9"};
    case ToolboxSlotId::Gradient:
        return {ToolboxSlotId::Gradient, 13, 0, 13, L"Gradient Tool", L"Chuyển sắc tương tác", L"G"};
    case ToolboxSlotId::Dimension:
        return {ToolboxSlotId::Dimension, 10, 0, 16, L"Dimension Tool", L"Công cụ đo kích thước (CAD)", L"D"};
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
    toolbox_visible_mask_ = 0xFFF; // all 12 slots
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
        if (list[i] == slot) return static_cast<int>(i);
    }
    return -1;
}

} // namespace pikdraw::desktop
