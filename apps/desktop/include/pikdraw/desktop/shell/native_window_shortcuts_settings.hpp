#pragma once

#include "pikdraw/desktop/actions/action_dispatcher.hpp"
#include "pikdraw/desktop/actions/action_id.hpp"
#include "pikdraw/document/commands/layer_commands.hpp"
#include <string>
#include <vector>
#include <memory>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace pikdraw::desktop {

class NativeWindow;

enum class ShortcutCategory : uint8_t {
    All = 0,
    Order = 1,
    Nudge = 2,
    Edit = 3,
    Tools = 4
};

struct ShortcutItem {
    std::string action_id;
    std::string title_vi;
    std::string title_en;
    std::string shortcut;
    std::string default_shortcut;
    ShortcutCategory category{ShortcutCategory::All};
};

struct ShortcutsSettingsState {
    int active_profile{0};
    int selected_row{0};
    int scroll_offset_y{0};
    ShortcutCategory active_category{ShortcutCategory::All};
    bool is_recording{false};
    std::string recording_key_display;
    bool initialized{false};
    std::vector<ShortcutItem> items;
};

#ifdef _WIN32

/// Format Win32 virtual key and modifier states into canonical shortcut string
std::string format_key_shortcut_string(WPARAM key, bool ctrl, bool shift, bool alt);

/// Ensure default CorelDRAW shortcuts and handlers are registered
void ensure_shortcuts_initialized(NativeWindow& win);

/// Reorder selected objects in Z-order
bool reorder_selected_objects(NativeWindow& win, document::ReorderObjectInLayerCommand::Step step);

/// Nudge selected objects by dx/dy mm
bool nudge_selected_objects(NativeWindow& win, double dx_mm, double dy_mm);

/// Dispatch shortcut from canvas keydown
bool dispatch_canvas_shortcuts(NativeWindow& win, WPARAM key);

/// Paint Shortcuts & Hotkeys tab in Settings modal
void paint_shortcuts_settings_tab(
    NativeWindow& win, HDC mem_dc, int cx, int cy, bool is_vi, bool is_dark,
    COLORREF m_text_pri, COLORREF m_text_mut, COLORREF m_teal, COLORREF m_border,
    HFONT font_vn_bold, HFONT font_vn_norm, HFONT font_sec, HFONT font_mono);

/// Handle mouse clicks inside Shortcuts & Hotkeys tab
bool handle_shortcuts_settings_click(NativeWindow& win, int x, int y);

/// Handle key press while recording or navigating in Shortcuts & Hotkeys tab
bool handle_shortcuts_settings_key(NativeWindow& win, WPARAM key);

/// Handle mouse wheel inside Shortcuts & Hotkeys tab
bool handle_shortcuts_settings_wheel(NativeWindow& win, int delta);

/// Get the singleton or window-scoped shortcuts settings state
ShortcutsSettingsState& get_shortcuts_settings_state();

#endif

} // namespace pikdraw::desktop
