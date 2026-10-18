#include "pikdraw/desktop/shell/native_window.hpp"
#include "pikdraw/desktop/shell/native_window_layers_dock.hpp"
#include "pikdraw/document/commands/layer_commands.hpp"
#include "pikdraw/document/commands/command.hpp"
#include <algorithm>

namespace pikdraw::desktop {

#ifdef _WIN32

bool NativeWindow::handle_layers_dock_click(int x, int y, bool is_double_click) {
    return handle_layers_panel_click(*this, x, y, is_double_click);
}

bool NativeWindow::handle_layers_dock_mouse_wheel(int delta) {
    return handle_layers_panel_mouse_wheel(*this, delta);
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

    // 1. Filter Bar Hit-Testing (y: content_y + 26 .. content_y + 48)
    const int filter_bar_y = content_y + 26;
    if (y >= filter_bar_y && y <= filter_bar_y + 22) {
        const int filter_btn_w = (w - insp_x - 24) / 5;
        if (filter_btn_w > 0 && x >= insp_x + 12 && x <= insp_x + 12 + filter_btn_w * 5) {
            const int clicked_idx = (x - (insp_x + 12)) / filter_btn_w;
            if (clicked_idx >= 0 && clicked_idx < 5) {
                state.filter = static_cast<LayerFilterType>(clicked_idx);
                InvalidateRect(win.hwnd(), nullptr, FALSE);
                return true;
            }
        }
    }

    // 2. Footer Action Bar Hit-Testing (y: h - status_h - 32 .. h - status_h - 4)
    const int footer_y = h - status_h - 30;
    if (y >= footer_y && y <= footer_y + 26) {
        const int footer_btn_w = (w - insp_x - 24) / 5;
        if (footer_btn_w > 0 && x >= insp_x + 12 && x <= insp_x + 12 + footer_btn_w * 5) {
            const int btn_idx = (x - (insp_x + 12)) / footer_btn_w;
            const auto& selection = win.root().vector_controller().selection().selected_ids();

            if (btn_idx == 0) { // + Layer
                std::string l_name = "Layer " + std::to_string(layers.size() + 1);
                win.root().document().execute_command(std::make_unique<document::AddLayerCommand>(l_name));
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

    // 3. Tree List Hit-Testing
    const int tree_top = filter_bar_y + 26;
    const int tree_bottom = footer_y - 6;
    const int row_h = LayersDockMetrics::row_height;

    if (y < tree_top || y > tree_bottom) return false;

    int current_y = tree_top - state.scroll_offset_y;

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
                win.root().document().execute_command(
                    std::make_unique<document::SetLayerVisibilityCommand>(layer_idx, !layer.visible));
                InvalidateRect(win.hwnd(), nullptr, FALSE);
                return true;
            } else if (rx > 32 && rx <= 48) { // Lock icon
                win.root().document().execute_command(
                    std::make_unique<document::SetLayerLockedCommand>(layer_idx, !layer.locked));
                InvalidateRect(win.hwnd(), nullptr, FALSE);
                return true;
            } else { // Click on layer body -> select objects on this layer
                if (!layer.locked && !layer.object_ids.empty()) {
                    auto& sel = win.root().vector_controller().selection();
                    sel.clear();
                    for (auto id : layer.object_ids) {
                        sel.select(id, vector::SelectionMode::Add);
                    }
                    InvalidateRect(win.hwnd(), nullptr, FALSE);
                    return true;
                }
            }
        }
        current_y += row_h + 2;

        if (is_layer_collapsed) continue;

        // Traverse children in reverse stacking order
        for (int obj_i = static_cast<int>(layer.object_ids.size()) - 1; obj_i >= 0; --obj_i) {
            const auto obj_id = layer.object_ids[static_cast<size_t>(obj_i)];
            const auto obj_opt = objects_map.get(obj_id);
            if (!obj_opt) continue;

            const auto& obj_var = *obj_opt;
            const bool is_group = std::holds_alternative<document::GroupObject>(obj_var);
            const bool is_text = std::holds_alternative<document::TextObject>(obj_var);
            const bool is_image = std::holds_alternative<document::ImageObject>(obj_var);

            if (state.filter == LayerFilterType::Path && (is_text || is_image || is_group)) continue;
            if (state.filter == LayerFilterType::Text && !is_text) continue;
            if (state.filter == LayerFilterType::Image && !is_image) continue;
            if (state.filter == LayerFilterType::Group && !is_group) continue;

            const bool obj_visible = std::visit([](const auto& o) { return o.visible; }, obj_var);
            const bool obj_locked = std::visit([](const auto& o) { return o.locked; }, obj_var);

            if (y >= current_y && y < current_y + row_h) {
                const int rx = x - (insp_x + 18);

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
                } else { // Row selection or double-click
                    if (is_double_click && is_group) {
                        win.enter_group_isolation(obj_id);
                        InvalidateRect(win.hwnd(), nullptr, FALSE);
                        return true;
                    }
                    if (!obj_locked) {
                        win.root().vector_controller().selection().select(obj_id);
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

#endif

} // namespace pikdraw::desktop
