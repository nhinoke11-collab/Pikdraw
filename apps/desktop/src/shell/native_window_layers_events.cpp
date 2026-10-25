#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/desktop/shell/native_window_layers_dock.hpp"
#include "../tools/drawing_tool_controller.hpp"
#include "../tools/dimension_tool_controller.hpp"
#include "native_window_layers_tree.hpp"
#include "pikdraw/document/commands/layer_commands.hpp"
#include "pikdraw/document/commands/command.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace pikdraw::desktop {

#ifdef _WIN32

namespace {
struct LayerDockRowHit {
    bool layer_row{false};
    size_t layer_index{0};
    document::ObjectId object_id{document::kInvalidObjectId};
    size_t object_index{0};
};

std::optional<LayerDockRowHit> layer_dock_row_at(
    const document::DocumentSnapshot& snapshot, const LayersDockState& state,
    int y, int window_height) {
    const int tree_top = 138 + 70;
    const int tree_bottom = window_height - 26 - 30 - 6;
    if (y < tree_top || y >= tree_bottom) return std::nullopt;
    const int row_height = LayersDockMetrics::row_height;
    int row_y = tree_top - state.scroll_offset_y;
    const auto& layers = snapshot.page().layers;
    for (size_t layer_index = 0; layer_index < layers.size(); ++layer_index) {
        if (y >= row_y && y < row_y + row_height)
            return LayerDockRowHit{true, layer_index, document::kInvalidObjectId, 0};
        row_y += row_height + 2;
        if (state.collapsed_layers.contains(layer_index)) continue;
        for (const auto& row : layer_tree_rows(snapshot, layers[layer_index], state)) {
            if (!layer_tree_row_matches(snapshot, row, state)) continue;
            if (y >= row_y && y < row_y + row_height) {
                const auto* entry = snapshot.hierarchy().find(row.id);
                if (!entry || entry->parent != document::kInvalidObjectId)
                    return LayerDockRowHit{false, layer_index,
                        document::kInvalidObjectId, 0};
                const auto& root_ids = layers[layer_index].object_ids;
                const auto it = std::find(root_ids.begin(), root_ids.end(), row.id);
                const size_t index = it == root_ids.end()
                    ? root_ids.size() : static_cast<size_t>(it - root_ids.begin());
                return LayerDockRowHit{false, layer_index, row.id, index};
            }
            row_y += row_height + 1;
        }
    }
    return std::nullopt;
}
} // namespace

void NativeWindow::set_active_layer_index(size_t index) {
    auto snapshot = root().document().current_snapshot();
    const size_t count = snapshot->page().layers.size();
    if (count == 0) return;
    layers_state_.active_layer_index = std::min(index, count - 1);
    workflow().set_active_layer_index(layers_state_.active_layer_index);
    if (drawing_tool_) drawing_tool_->set_active_layer_index(layers_state_.active_layer_index);
    if (dimension_tool_) dimension_tool_->set_active_layer_index(layers_state_.active_layer_index);
    const auto& active = snapshot->page().layers[layers_state_.active_layer_index];
    if (active.locked || !active.visible) {
        status_msg_ = language_ == UILanguage::Vietnamese
            ? (active.locked ? "Lớp đang khóa · Mở khóa lớp để tiếp tục vẽ"
                             : "Lớp đang ẩn · Hiện lớp để thấy nội dung mới")
            : (active.locked ? "Active layer is locked · Unlock it to draw"
                             : "Active layer is hidden · Show it to see new content");
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool NativeWindow::handle_layers_dock_click(int x, int y, bool is_double_click) {
    return handle_layers_panel_click(*this, x, y, is_double_click);
}

bool NativeWindow::handle_layers_dock_mouse_wheel(int delta) {
    return handle_layers_panel_mouse_wheel(*this, delta);
}

bool NativeWindow::handle_layers_dock_mouse_down(int x, int y) {
    return handle_layers_panel_mouse_down(*this, x, y);
}

bool NativeWindow::handle_layers_dock_mouse_move(int x, int y) {
    return handle_layers_panel_mouse_move(*this, x, y);
}

bool NativeWindow::handle_layers_dock_mouse_up(int x, int y) {
    return handle_layers_panel_mouse_up(*this, x, y);
}

bool NativeWindow::handle_layers_dock_key(WPARAM key) {
    return handle_layers_panel_key(*this, key);
}

bool NativeWindow::handle_layers_dock_char(wchar_t ch) {
    return handle_layers_panel_char(*this, ch);
}

bool NativeWindow::handle_layers_dock_context_menu(int screen_x, int screen_y, int x, int y) {
    return handle_layers_panel_context_menu(*this, screen_x, screen_y, x, y);
}

bool handle_layers_panel_click(NativeWindow& win, int x, int y, bool is_double_click) {
    const int w = win.width();
    const int h = win.height();
    const int insp_x = w - LayersDockMetrics::dock_width;
    const int content_y = 138;
    const int status_h = 26;

    if (x < insp_x || x > w) return false;

    auto snapshot = win.root().document().current_snapshot();
    auto& state = win.layers_dock_state();
    const auto& layers = snapshot->page().layers;
    const auto& objects_map = snapshot->objects();

    // 1. Search Box Hit-Testing (y: content_y + 22 .. content_y + 44)
    const int search_y = content_y + 22;
    if (y >= search_y && y <= search_y + 22) {
        if (x >= w - 26 && x <= w - 10 && !state.search_query.empty()) {
            state.search_query.clear();
            state.search_active = false;
        } else {
            state.search_active = true;
            state.rename_layer_idx = std::nullopt;
            state.rename_object_id = std::nullopt;
        }
        InvalidateRect(win.hwnd(), nullptr, FALSE);
        return true;
    }

    // 2. Filter Bar Hit-Testing (y: content_y + 46 .. content_y + 68)
    const int filter_bar_y = content_y + 46;
    if (y >= filter_bar_y && y <= filter_bar_y + 22) {
        const int filter_btn_w = (w - insp_x - 24) / 5;
        if (filter_btn_w > 0 && x >= insp_x + 12 && x <= insp_x + 12 + filter_btn_w * 5) {
            const int clicked_idx = (x - (insp_x + 12)) / filter_btn_w;
            if (clicked_idx >= 0 && clicked_idx < 5) {
                state.filter = static_cast<LayerFilterType>(clicked_idx);
                state.scroll_offset_y = 0;
                InvalidateRect(win.hwnd(), nullptr, FALSE);
                return true;
            }
        }
    }

    // 3. Footer Action Bar Hit-Testing (y: h - status_h - 30 .. h - status_h - 4)
    const int footer_y = h - status_h - 30;
    if (y >= footer_y && y <= footer_y + 26) {
        const int footer_btn_w = (w - insp_x - 24) / 5;
        if (footer_btn_w > 0 && x >= insp_x + 12 && x <= insp_x + 12 + footer_btn_w * 5) {
            const int btn_idx = (x - (insp_x + 12)) / footer_btn_w;
            const auto& selection = win.root().vector_controller().selection().selected_ids();

            if (btn_idx == 0) { // + Layer
                std::string l_name = "Layer " + std::to_string(layers.size() + 1);
                win.root().document().execute_command(std::make_unique<document::AddLayerCommand>(l_name));
                win.set_active_layer_index(layers.size());
                InvalidateRect(win.hwnd(), nullptr, FALSE);
                return true;
            } else if (btn_idx == 1) { // Group
                if (selection.size() >= 2) {
                    (void)win.workflow().group_selected();
                    InvalidateRect(win.hwnd(), nullptr, FALSE);
                    return true;
                }
            } else if (btn_idx == 2) { // Up ▲ (Bring forward)
                if (!selection.empty()) {
                    for (auto id : selection) {
                        win.root().document().execute_command(std::make_unique<document::ReorderObjectInLayerCommand>(
                            id, document::ReorderObjectInLayerCommand::Step::BringForward));
                    }
                    InvalidateRect(win.hwnd(), nullptr, FALSE);
                    return true;
                }
            } else if (btn_idx == 3) { // Dn ▼ (Send backward)
                if (!selection.empty()) {
                    for (auto id : selection) {
                        win.root().document().execute_command(std::make_unique<document::ReorderObjectInLayerCommand>(
                            id, document::ReorderObjectInLayerCommand::Step::SendBackward));
                    }
                    InvalidateRect(win.hwnd(), nullptr, FALSE);
                    return true;
                }
            } else if (btn_idx == 4) { // Del 🗑 (Delete selected)
                if (!selection.empty()) {
                    for (auto id : selection) {
                        win.root().document().execute_command(std::make_unique<document::RemoveObjectCommand>(id));
                    }
                    win.root().vector_controller().selection().clear();
                    InvalidateRect(win.hwnd(), nullptr, FALSE);
                    return true;
                }
            }
        }
    }

    // 4. Tree List Hit-Testing
    const int tree_top = filter_bar_y + 24;
    const int tree_bottom = footer_y - 6;
    const int row_h = LayersDockMetrics::row_height;

    if (y < tree_top || y > tree_bottom) return false;

    // Reset search active if clicked outside search bar
    state.search_active = false;

    int current_y = tree_top - state.scroll_offset_y;
    const bool is_alt_pressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
    const bool is_shift_pressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool is_ctrl_pressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

    for (size_t layer_idx = 0; layer_idx < layers.size(); ++layer_idx) {
        const auto& layer = layers[layer_idx];
        const bool is_layer_collapsed = state.collapsed_layers.count(layer_idx) > 0;

        // Check Layer Row Hit
        if (y >= current_y && y < current_y + row_h) {
            const int rx = x - (insp_x + 8);
            if (rx >= 0 && rx <= 16) { // Expand/collapse triangle
                if (is_layer_collapsed) {
                    state.collapsed_layers.erase(layer_idx);
                } else {
                    state.collapsed_layers.insert(layer_idx);
                }
                InvalidateRect(win.hwnd(), nullptr, FALSE);
                return true;
            } else if (rx > 16 && rx <= 32) { // Eye icon
                if (is_alt_pressed) { // Solo Layer mode
                    if (state.pre_solo_visibility.has_value()) {
                        // Restore previous visibility
                        for (size_t i = 0; i < std::min(layers.size(), state.pre_solo_visibility->size()); ++i) {
                            win.root().document().execute_command(
                                std::make_unique<document::SetLayerVisibilityCommand>(i, (*state.pre_solo_visibility)[i]));
                        }
                        state.pre_solo_visibility = std::nullopt;
                    } else {
                        // Save current visibility and solo this layer
                        std::vector<bool> current_vis;
                        current_vis.reserve(layers.size());
                        for (size_t i = 0; i < layers.size(); ++i) {
                            current_vis.push_back(layers[i].visible);
                            win.root().document().execute_command(
                                std::make_unique<document::SetLayerVisibilityCommand>(i, i == layer_idx));
                        }
                        state.pre_solo_visibility = std::move(current_vis);
                    }
                } else {
                    win.root().document().execute_command(
                        std::make_unique<document::SetLayerVisibilityCommand>(layer_idx, !layer.visible));
                }
                InvalidateRect(win.hwnd(), nullptr, FALSE);
                return true;
            } else if (rx > 32 && rx <= 46) { // Printer icon (Printable / CNC)
                win.root().document().execute_command(
                    std::make_unique<document::SetLayerPrintableCommand>(layer_idx, !layer.printable));
                InvalidateRect(win.hwnd(), nullptr, FALSE);
                return true;
            } else if (rx > 46 && rx <= 60) { // Lock icon
                if (is_alt_pressed) { // Lock other layers
                    if (state.pre_solo_locked.has_value()) {
                        for (size_t i = 0; i < std::min(layers.size(), state.pre_solo_locked->size()); ++i)
                            win.root().document().execute_command(
                                std::make_unique<document::SetLayerLockedCommand>(
                                    i, (*state.pre_solo_locked)[i]));
                        state.pre_solo_locked = std::nullopt;
                    } else {
                        std::vector<bool> current_locked;
                        current_locked.reserve(layers.size());
                        for (size_t i = 0; i < layers.size(); ++i) {
                            current_locked.push_back(layers[i].locked);
                            win.root().document().execute_command(
                                std::make_unique<document::SetLayerLockedCommand>(i, i != layer_idx));
                        }
                        state.pre_solo_locked = std::move(current_locked);
                    }
                } else {
                    win.root().document().execute_command(
                        std::make_unique<document::SetLayerLockedCommand>(layer_idx, !layer.locked));
                }
                InvalidateRect(win.hwnd(), nullptr, FALSE);
                return true;
            } else if (x >= w - 24) { // Selection proxy square
                auto& sel = win.root().vector_controller().selection();
                if (!is_shift_pressed) sel.clear();
                for (auto id : layer.object_ids) {
                    sel.select(id, vector::SelectionMode::Add);
                }
                win.set_active_layer_index(layer_idx);
                InvalidateRect(win.hwnd(), nullptr, FALSE);
                return true;
            } else { // Click on layer body
                if (is_double_click) {
                    state.rename_layer_idx = layer_idx;
                    state.rename_buffer = layer.name;
                    state.rename_object_id = std::nullopt;
                } else {
                    win.set_active_layer_index(layer_idx);
                    if (!layer.locked && !layer.object_ids.empty()) {
                        auto& sel = win.root().vector_controller().selection();
                        if (!is_shift_pressed) sel.clear();
                        for (auto id : layer.object_ids) {
                            sel.select(id, vector::SelectionMode::Add);
                        }
                    }
                }
                InvalidateRect(win.hwnd(), nullptr, FALSE);
                return true;
            }
        }
        current_y += row_h + 2;

        if (is_layer_collapsed) continue;

        // Traverse children
        for(const auto& tree_row:layer_tree_rows(*snapshot,layer,state)) {
            const auto obj_id=tree_row.id;
            const auto obj_opt = objects_map.get(obj_id);
            if (!obj_opt) continue;

            const auto& obj_var = *obj_opt;
            if (!layer_tree_row_matches(*snapshot, tree_row, state)) continue;
            const bool is_group = std::holds_alternative<document::GroupObject>(obj_var);

            const bool obj_visible = std::visit([](const auto& o) { return o.visible; }, obj_var);
            const bool obj_locked = std::visit([](const auto& o) { return o.locked; }, obj_var);
            const auto obj_name = std::visit([](const auto& o) { return o.name; }, obj_var);

            if (y >= current_y && y < current_y + row_h) {
                const int rx = x - (insp_x + 18 + int(tree_row.depth)*12);

                if(is_group && rx >= -14 && rx < 0) {
                    if(state.collapsed_groups.contains(obj_id)) state.collapsed_groups.erase(obj_id);
                    else state.collapsed_groups.insert(obj_id);
                    InvalidateRect(win.hwnd(), nullptr, FALSE);
                    return true;
                }
                if (rx >= 0 && rx <= 20) { // Eye icon
                    win.root().document().execute_command(
                        std::make_unique<document::SetObjectVisibilityCommand>(obj_id, !obj_visible));
                    InvalidateRect(win.hwnd(), nullptr, FALSE);
                    return true;
                } else if (rx > 20 && rx <= 36) { // Lock icon
                    win.root().document().execute_command(
                        std::make_unique<document::SetObjectLockedCommand>(obj_id, !obj_locked));
                    InvalidateRect(win.hwnd(), nullptr, FALSE);
                    return true;
                } else { // Row selection or double click
                    if (is_double_click) {
                        if (is_group) {
                            win.enter_group_isolation(obj_id);
                        } else {
                            state.rename_object_id = obj_id;
                            state.rename_buffer = obj_name;
                            state.rename_layer_idx = std::nullopt;
                        }
                        InvalidateRect(win.hwnd(), nullptr, FALSE);
                        return true;
                    }
                    if (!obj_locked) {
                        win.set_active_layer_index(layer_idx);
                        auto& sel = win.root().vector_controller().selection();
                        if (is_shift_pressed || is_ctrl_pressed) {
                            sel.select(obj_id, vector::SelectionMode::Toggle);
                        } else {
                            const auto* entry = snapshot->hierarchy().find(obj_id);
                            if (entry && entry->mask) win.edit_powerclip_frame(entry->parent);
                            else {
                                if (entry && entry->parent) win.enter_group_isolation(entry->parent);
                                else win.exit_all_isolation();
                                win.root().vector_controller().set_selection({obj_id}, *snapshot);
                            }
                        }
                        InvalidateRect(win.hwnd(), nullptr, FALSE);
                        return true;
                    }
                }
            }
            current_y += row_h + 1;
        }
    }

    return true;
}

bool handle_layers_panel_mouse_wheel(NativeWindow& win, int delta) {
    auto& state = win.layers_dock_state();
    const int step = LayersDockMetrics::row_height * 2;
    state.scroll_offset_y -= (delta > 0 ? step : -step);
    state.scroll_offset_y = std::max(0, state.scroll_offset_y);
    InvalidateRect(win.hwnd(), nullptr, FALSE);
    return true;
}

bool handle_layers_panel_mouse_down(NativeWindow& win, int x, int y) {
    auto& state = win.layers_dock_state();
    state.pointer_pressed = false;
    const auto snapshot = win.root().document().current_snapshot();
    if (!snapshot) return false;
    const auto hit = layer_dock_row_at(*snapshot, state, y, win.height());
    if (!hit) return false;
    state.drag_start_y = y;
    state.drag_start_x = x;
    state.drag_current_y = y;
    state.is_dragging = false;
    state.drop_insert_index = -1;
    state.pointer_pressed = true;
    state.drag_is_layer = hit->layer_row;
    state.drag_layer_idx = hit->layer_index;
    state.drag_object_id = hit->layer_row ? document::kInvalidObjectId : hit->object_id;
    if (win.hwnd()) SetCapture(win.hwnd());
    return true;
}

bool handle_layers_panel_mouse_move(NativeWindow& win, int x, int y) {
    auto& state = win.layers_dock_state();
    if (!state.pointer_pressed) return false;
    if (std::hypot(x - state.drag_start_x, y - state.drag_start_y) > 6.0) {
        state.is_dragging = true;
        state.drag_current_y = y;
        const auto snapshot = win.root().document().current_snapshot();
        const auto hit = snapshot ? layer_dock_row_at(
            *snapshot, state, y, win.height()) : std::nullopt;
        state.drop_insert_index = hit
            ? static_cast<int>(hit->layer_row ? hit->layer_index : hit->object_index)
            : -1;
        InvalidateRect(win.hwnd(), nullptr, FALSE);
    }
    return true;
}

bool handle_layers_panel_mouse_up(NativeWindow& win, int, int y) {
    auto& state = win.layers_dock_state();
    if (!state.pointer_pressed) return false;
    const bool was_dragging = state.is_dragging;
    const auto source_layer = state.drag_layer_idx;
    const auto source_object = state.drag_object_id;
    const auto source_is_layer = state.drag_is_layer;
    state.pointer_pressed = false;
    state.is_dragging = false;
    if (win.hwnd() && GetCapture() == win.hwnd()) ReleaseCapture();

    bool changed = false;
    const auto snapshot = win.root().document().current_snapshot();
    const auto target = snapshot
        ? layer_dock_row_at(*snapshot, state, y, win.height()) : std::nullopt;
    if (was_dragging && target) {
        if (source_is_layer && target->layer_index < snapshot->page().layers.size()) {
            changed = win.root().document().execute_command(
                std::make_unique<document::ReorderLayerCommand>(
                    source_layer, target->layer_index)).success;
            if (changed) {
                size_t active = state.active_layer_index;
                if (active == source_layer) active = target->layer_index;
                else if (source_layer < active && active <= target->layer_index) --active;
                else if (target->layer_index <= active && active < source_layer) ++active;
                win.set_active_layer_index(active);
            }
        } else if (!source_is_layer && source_object != document::kInvalidObjectId &&
                   target->layer_index < snapshot->page().layers.size()) {
            const auto& destination = snapshot->page().layers[target->layer_index];
            size_t insert_at = destination.object_ids.size();
            if (target->object_id != document::kInvalidObjectId)
                insert_at = target->object_index;
            changed = win.root().document().execute_command(
                std::make_unique<document::MoveObjectsToLayerCommand>(
                    std::vector<document::ObjectId>{source_object},
                    target->layer_index, insert_at)).success;
            if (changed) win.set_active_layer_index(target->layer_index);
        }
    }

    state.drag_object_id = document::kInvalidObjectId;
    state.drop_insert_index = -1;
    InvalidateRect(win.hwnd(), nullptr, FALSE);
    return true;
}

bool handle_layers_panel_key(NativeWindow& win, WPARAM key) {
    auto& state = win.layers_dock_state();
    if (state.rename_layer_idx.has_value()) {
        if (key == VK_RETURN) {
            if (!state.rename_buffer.empty()) {
                win.root().document().execute_command(
                    std::make_unique<document::RenameLayerCommand>(*state.rename_layer_idx, state.rename_buffer));
            }
            state.rename_layer_idx = std::nullopt;
            state.rename_buffer.clear();
            InvalidateRect(win.hwnd(), nullptr, FALSE);
            return true;
        } else if (key == VK_ESCAPE) {
            state.rename_layer_idx = std::nullopt;
            state.rename_buffer.clear();
            InvalidateRect(win.hwnd(), nullptr, FALSE);
            return true;
        }
    } else if (state.rename_object_id.has_value()) {
        if (key == VK_RETURN) {
            if (!state.rename_buffer.empty()) {
                win.root().document().execute_command(
                    std::make_unique<document::RenameObjectCommand>(*state.rename_object_id, state.rename_buffer));
            }
            state.rename_object_id = std::nullopt;
            state.rename_buffer.clear();
            InvalidateRect(win.hwnd(), nullptr, FALSE);
            return true;
        } else if (key == VK_ESCAPE) {
            state.rename_object_id = std::nullopt;
            state.rename_buffer.clear();
            InvalidateRect(win.hwnd(), nullptr, FALSE);
            return true;
        }
    } else if (state.search_active) {
        if (key == VK_ESCAPE) {
            state.search_query.clear();
            state.search_active = false;
            InvalidateRect(win.hwnd(), nullptr, FALSE);
            return true;
        }
    }
    if (state.search_active || state.rename_layer_idx.has_value() ||
        state.rename_object_id.has_value()) return true;
    return false;
}

bool handle_layers_panel_char(NativeWindow& win, wchar_t ch) {
    auto& state = win.layers_dock_state();
    if (state.rename_layer_idx.has_value() || state.rename_object_id.has_value()) {
        if (ch == VK_BACK) {
            if (!state.rename_buffer.empty()) state.rename_buffer.pop_back();
            InvalidateRect(win.hwnd(), nullptr, FALSE);
            return true;
        } else if (ch >= 32) {
            state.rename_buffer += win.wide_to_utf8(std::wstring(1, ch));
            InvalidateRect(win.hwnd(), nullptr, FALSE);
            return true;
        }
    } else if (state.search_active) {
        if (ch == VK_BACK) {
            if (!state.search_query.empty()) state.search_query.pop_back();
            state.scroll_offset_y = 0;
            InvalidateRect(win.hwnd(), nullptr, FALSE);
            return true;
        } else if (ch >= 32) {
            state.search_query += win.wide_to_utf8(std::wstring(1, ch));
            state.scroll_offset_y = 0;
            InvalidateRect(win.hwnd(), nullptr, FALSE);
            return true;
        }
    }
    return false;
}

bool handle_layers_panel_context_menu(NativeWindow& win, int screen_x, int screen_y, int, int y) {
    const int content_y = 138;
    const int tree_top = content_y + 70;
    const int row_h = LayersDockMetrics::row_height;

    auto snapshot = win.root().document().current_snapshot();
    auto& state = win.layers_dock_state();
    const auto& layers = snapshot->page().layers;
    const auto& objects_map = snapshot->objects();
    const bool is_vi = (win.language() == UILanguage::Vietnamese);

    int current_y = tree_top - state.scroll_offset_y;

    for (size_t layer_idx = 0; layer_idx < layers.size(); ++layer_idx) {
        const auto& layer = layers[layer_idx];
        const bool is_layer_collapsed = state.collapsed_layers.count(layer_idx) > 0;

        // Hit test layer row
        if (y >= current_y && y < current_y + row_h) {
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, 101, is_vi ? L"Đổi tên lớp (Rename)" : L"Rename Layer");
            AppendMenuW(hMenu, MF_STRING, 102, is_vi ? L"Nhân bản lớp (Duplicate)" : L"Duplicate Layer");
            if (layers.size() > 1) {
                AppendMenuW(hMenu, MF_STRING, 103, is_vi ? L"Xóa lớp (Delete)" : L"Delete Layer");
            }
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, 104, layer.visible ? (is_vi ? L"Ẩn lớp" : L"Hide Layer") : (is_vi ? L"Hiện lớp" : L"Show Layer"));
            AppendMenuW(hMenu, MF_STRING, 105, layer.locked ? (is_vi ? L"Mở khóa lớp" : L"Unlock Layer") : (is_vi ? L"Khóa lớp" : L"Lock Layer"));
            AppendMenuW(hMenu, MF_STRING, 106, layer.printable ? (is_vi ? L"Tắt in/xuất CNC" : L"Disable Print/CNC") : (is_vi ? L"Bật in/xuất CNC" : L"Enable Print/CNC"));
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

            // Layer Color palette submenu
            HMENU hColor = CreatePopupMenu();
            const wchar_t* col_names[] = {L"Cyan", L"Red", L"Blue", L"Amber", L"Purple", L"Green", L"Pink", L"Orange"};
            for (size_t c = 0; c < 8; ++c) {
                AppendMenuW(hColor, MF_STRING, 201 + c, col_names[c]);
            }
            AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hColor), is_vi ? L"Đổi màu lớp" : L"Layer Color");

            const auto& selection = win.root().vector_controller().selection().selected_ids();
            if (!selection.empty()) {
                AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(hMenu, MF_STRING, 107, is_vi ? L"Chuyển đối tượng đang chọn vào lớp này" : L"Move Selection to this Layer");
            }

            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen_x, screen_y, 0, win.hwnd(), nullptr);
            DestroyMenu(hMenu);

            if (cmd == 101) { // Rename
                state.rename_layer_idx = layer_idx;
                state.rename_buffer = layer.name;
                state.rename_object_id = std::nullopt;
            } else if (cmd == 102) { // Duplicate
                win.root().document().execute_command(std::make_unique<document::DuplicateLayerCommand>(layer_idx));
                win.set_active_layer_index(layer_idx + 1);
            } else if (cmd == 103) { // Delete
                const size_t active_before = state.active_layer_index;
                win.root().document().execute_command(std::make_unique<document::RemoveLayerCommand>(layer_idx));
                const size_t remaining = win.root().document().current_snapshot()->page().layers.size();
                if (remaining > 0) {
                    const size_t next_active = active_before == layer_idx
                        ? std::min(layer_idx, remaining - 1)
                        : active_before > layer_idx ? active_before - 1 : active_before;
                    win.set_active_layer_index(next_active);
                }
            } else if (cmd == 104) { // Visibility
                win.root().document().execute_command(std::make_unique<document::SetLayerVisibilityCommand>(layer_idx, !layer.visible));
            } else if (cmd == 105) { // Lock
                win.root().document().execute_command(std::make_unique<document::SetLayerLockedCommand>(layer_idx, !layer.locked));
            } else if (cmd == 106) { // Printable
                win.root().document().execute_command(std::make_unique<document::SetLayerPrintableCommand>(layer_idx, !layer.printable));
            } else if (cmd >= 201 && cmd <= 208) { // Set Color
                size_t c_idx = cmd - 201;
                win.root().document().execute_command(std::make_unique<document::SetLayerColorCommand>(layer_idx, document::default_layer_color(c_idx)));
            } else if (cmd == 107) { // Move selection here
                win.root().document().execute_command(std::make_unique<document::MoveObjectsToLayerCommand>(selection, layer_idx));
                win.set_active_layer_index(layer_idx);
            }
            InvalidateRect(win.hwnd(), nullptr, FALSE);
            return true;
        }
        current_y += row_h + 2;

        if (is_layer_collapsed) continue;

        for(const auto& tree_row:layer_tree_rows(*snapshot,layer,state)) {
            const auto obj_id=tree_row.id;
            const auto obj_opt = objects_map.get(obj_id);
            if (!obj_opt) continue;
            if (!layer_tree_row_matches(*snapshot, tree_row, state)) continue;

            if (y >= current_y && y < current_y + row_h) {
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, 301, is_vi ? L"Đổi tên đối tượng" : L"Rename Object");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(hMenu, MF_STRING, 302, is_vi ? L"Lên trước (Bring Forward)\tCtrl+]" : L"Bring Forward\tCtrl+]");
                AppendMenuW(hMenu, MF_STRING, 303, is_vi ? L"Xuống sau (Send Backward)\tCtrl+[" : L"Send Backward\tCtrl+[");
                AppendMenuW(hMenu, MF_STRING, 304, is_vi ? L"Lên trên cùng (Bring to Front)\tCtrl+Shift+]" : L"Bring to Front\tCtrl+Shift+]");
                AppendMenuW(hMenu, MF_STRING, 305, is_vi ? L"Xuống dưới cùng (Send to Back)\tCtrl+Shift+[" : L"Send to Back\tCtrl+Shift+[");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

                // Move to other layer submenu
                HMENU hLayers = CreatePopupMenu();
                for (size_t l = 0; l < layers.size(); ++l) {
                    std::wstring lname = win.utf8_to_wide(layers[l].name);
                    AppendMenuW(hLayers, MF_STRING, 401 + l, lname.c_str());
                }
                AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hLayers), is_vi ? L"Chuyển sang lớp" : L"Move to Layer");

                AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(hMenu, MF_STRING, 306, is_vi ? L"Xóa (Delete)\tDel" : L"Delete\tDel");

                int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen_x, screen_y, 0, win.hwnd(), nullptr);
                DestroyMenu(hMenu);

                if (cmd == 301) { // Rename
                    state.rename_object_id = obj_id;
                    state.rename_buffer = std::visit([](const auto& o) { return o.name; }, *obj_opt);
                    state.rename_layer_idx = std::nullopt;
                } else if (cmd == 302) { // Bring forward
                    win.root().document().execute_command(std::make_unique<document::ReorderObjectInLayerCommand>(
                        obj_id, document::ReorderObjectInLayerCommand::Step::BringForward));
                } else if (cmd == 303) { // Send backward
                    win.root().document().execute_command(std::make_unique<document::ReorderObjectInLayerCommand>(
                        obj_id, document::ReorderObjectInLayerCommand::Step::SendBackward));
                } else if (cmd == 304) { // Bring to front
                    win.root().document().execute_command(std::make_unique<document::ReorderObjectInLayerCommand>(
                        obj_id, document::ReorderObjectInLayerCommand::Step::BringToFront));
                } else if (cmd == 305) { // Send to back
                    win.root().document().execute_command(std::make_unique<document::ReorderObjectInLayerCommand>(
                        obj_id, document::ReorderObjectInLayerCommand::Step::SendToBack));
                } else if (cmd >= 401 && cmd < 401 + static_cast<int>(layers.size())) { // Move to layer
                    size_t target_l = cmd - 401;
                    win.root().document().execute_command(std::make_unique<document::MoveObjectToLayerCommand>(obj_id, target_l));
                } else if (cmd == 306) { // Delete
                    win.root().document().execute_command(std::make_unique<document::RemoveObjectCommand>(obj_id));
                    win.root().vector_controller().selection().clear();
                }
                InvalidateRect(win.hwnd(), nullptr, FALSE);
                return true;
            }
            current_y += row_h + 1;
        }
    }

    return false;
}

#endif

} // namespace pikdraw::desktop
