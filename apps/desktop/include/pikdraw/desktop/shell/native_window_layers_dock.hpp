#pragma once

#include "pikdraw/document/model/object_id.hpp"
#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include <functional>
#include <string>
#include <vector>
#include <optional>
#include <unordered_set>
#include <cstdint>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace pikdraw::desktop {

class NativeWindow;

enum class LayerFilterType : uint8_t {
    All = 0,
    Path = 1,
    Text = 2,
    Image = 3,
    Group = 4
};

struct LayersDockMetrics {
    static constexpr int dock_width = 280;
    static constexpr int row_height = 26;
    static constexpr int header_height = 56;
    static constexpr int footer_height = 32;

    static constexpr int col_expand_x = 4;
    static constexpr int col_eye_x = 22;
    static constexpr int col_lock_x = 40;
    static constexpr int col_type_x = 58;
    static constexpr int col_name_x = 76;
    static constexpr int col_select_x = 256;
};

struct LayersDockState {
    LayerFilterType filter{LayerFilterType::All};
    std::string search_query;
    bool search_active{false};
    int scroll_offset_y{0};
    int total_content_height{0};
    std::unordered_set<document::ObjectId> collapsed_groups;
    std::unordered_set<size_t> collapsed_layers;

    std::optional<document::ObjectId> rename_object_id{std::nullopt};
    std::optional<size_t> rename_layer_idx{std::nullopt};
    std::string rename_buffer;

    size_t active_layer_index{0};

    // Solo and drag states
    std::optional<std::vector<bool>> pre_solo_visibility{std::nullopt};
    std::optional<std::vector<bool>> pre_solo_locked{std::nullopt};
    int drag_start_y{0};
    int drag_start_x{0};
    int drag_current_y{0};
    bool pointer_pressed{false};
    bool is_dragging{false};
    bool drag_is_layer{false};
    size_t drag_layer_idx{0};
    document::ObjectId drag_object_id{0};
    int drop_insert_index{-1};
};

#ifdef _WIN32
void paint_layers_panel_dock(NativeWindow& win, HDC mem_dc, int insp_x, int content_y, int w, int h, int status_h);
bool handle_layers_panel_click(NativeWindow& win, int x, int y, bool is_double_click = false);
bool handle_layers_panel_mouse_wheel(NativeWindow& win, int delta);
bool handle_layers_panel_mouse_down(NativeWindow& win, int x, int y);
bool handle_layers_panel_mouse_move(NativeWindow& win, int x, int y);
bool handle_layers_panel_mouse_up(NativeWindow& win, int x, int y);
bool handle_layers_panel_key(NativeWindow& win, WPARAM key);
bool handle_layers_panel_char(NativeWindow& win, wchar_t ch);
bool handle_layers_panel_context_menu(NativeWindow& win, int screen_x, int screen_y, int x, int y);
#endif

} // namespace pikdraw::desktop
