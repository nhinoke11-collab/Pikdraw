#pragma once

#include "pikdraw/document/model/object_id.hpp"
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
};

#ifdef _WIN32
void paint_layers_panel_dock(NativeWindow& win, HDC mem_dc, int insp_x, int content_y, int w, int h, int status_h);
bool handle_layers_panel_click(NativeWindow& win, int x, int y, bool is_double_click = false);
bool handle_layers_panel_mouse_wheel(NativeWindow& win, int delta);
#endif

} // namespace pikdraw::desktop
