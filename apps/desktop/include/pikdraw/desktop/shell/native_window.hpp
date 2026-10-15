#pragma once

#include "pikdraw/desktop/shell/desktop_window.hpp"
#include "pikdraw/desktop/runtime/composition_root.hpp"
#include "pikdraw/desktop/workflows/document_workflow.hpp"

#include <string>
#include <memory>
#include <optional>
#include <cstdint>
#include "pikdraw/geometry/primitives/point.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace pikdraw::desktop {

enum class UILanguage {
    Vietnamese,
    English
};

/// Native Win32 window implementing the Cyber Dark Design System 2026 with full interactivity
class NativeWindow {
public:
    NativeWindow(int width = 1440, int height = 900);
    ~NativeWindow();

    bool initialize(const std::string& title = "PikDraw 2026 - Bản vẽ không tên [Vector Workspace]");
    int run();

    void capture_screenshot_bmp(const std::string& path);

    [[nodiscard]] static std::wstring utf8_to_wide(const std::string& utf8);

    [[nodiscard]] UILanguage language() const noexcept { return language_; }
    void set_language(UILanguage lang) noexcept;
    void toggle_language() noexcept;

    [[nodiscard]] ThemeMode theme() const noexcept { return theme_; }
    void set_theme(ThemeMode mode) noexcept;
    void toggle_theme() noexcept;

    [[nodiscard]] int active_tool() const noexcept { return active_tool_; }
    void set_active_tool(int tool_index) noexcept;

    [[nodiscard]] int active_workspace() const noexcept { return active_workspace_; }
    void set_active_workspace(int ws_index) noexcept;

    [[nodiscard]] int active_dock_tab() const noexcept { return active_dock_tab_; }
    void set_active_dock_tab(int tab_index) noexcept;

    void zoom_in() noexcept;
    void zoom_out() noexcept;
    void fit_page_to_canvas() noexcept;
    void trigger_export() noexcept;
    bool export_svg_to(const std::string& file_path);
    void create_new_document();

    // File Menu (CorelDRAW Reference)
    [[nodiscard]] bool is_file_menu_open() const noexcept { return show_file_menu_; }
    void open_file_menu() noexcept;
    void close_file_menu() noexcept;
    void toggle_file_menu() noexcept;

    // Home Screen (Trang chủ)
    [[nodiscard]] bool is_home_screen_open() const noexcept { return show_home_screen_; }
    void open_home_screen() noexcept;
    void close_home_screen() noexcept;
    void toggle_home_screen() noexcept;
    [[nodiscard]] int active_home_tab() const noexcept { return active_home_tab_; }
    void set_active_home_tab(int tab) noexcept;
    void open_recent_document(int doc_index) noexcept;
    void create_from_template(int template_index) noexcept;

    [[nodiscard]] bool is_settings_modal_open() const noexcept { return show_settings_modal_; }
    void open_settings_modal() noexcept;
    void close_settings_modal() noexcept;
    void toggle_settings_modal() noexcept;

    [[nodiscard]] int settings_active_tab() const noexcept { return settings_active_tab_; }
    void set_settings_tab(int tab) noexcept;

    [[nodiscard]] int settings_theme_choice() const noexcept { return settings_theme_choice_; }
    void set_settings_theme_choice(int theme) noexcept;

    [[nodiscard]] int settings_accent_choice() const noexcept { return settings_accent_choice_; }
    void set_settings_accent_choice(int accent) noexcept;

    [[nodiscard]] int settings_unit_choice() const noexcept { return settings_unit_choice_; }
    void set_settings_unit_choice(int unit) noexcept;

    [[nodiscard]] DesktopWindow& shell() noexcept { return shell_; }
    [[nodiscard]] const DesktopWindow& shell() const noexcept { return shell_; }

    [[nodiscard]] CompositionRoot& root() noexcept { return root_; }
    [[nodiscard]] const CompositionRoot& root() const noexcept { return root_; }

    [[nodiscard]] DocumentWorkflow& workflow() noexcept { return workflow_; }
    [[nodiscard]] const DocumentWorkflow& workflow() const noexcept { return workflow_; }

#ifdef _WIN32
    [[nodiscard]] HWND hwnd() const noexcept { return hwnd_; }
#endif

private:
#ifdef _WIN32
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    void paint(HDC hdc, int width, int height);
    void paint_settings_modal(HDC mem_dc, int width, int height);
    void paint_file_menu(HDC mem_dc, int x, int y, int w, int h);
    void paint_home_screen(HDC mem_dc, int w, int h);
    void paint_document_canvas(HDC dc, int width, int height);
    void handle_mouse_down(int x, int y);
    void handle_mouse_move(int x, int y);
    void handle_mouse_up(int x, int y);
    void handle_canvas_mouse_down(int x, int y);
    void handle_canvas_mouse_move(int x, int y);
    void handle_canvas_mouse_up(int x, int y);
    bool handle_key_down(WPARAM key);
    [[nodiscard]] RECT canvas_rect(int width, int height) const noexcept;
    [[nodiscard]] bool is_canvas_point(int x, int y) const noexcept;
    [[nodiscard]] geometry::Point2D canvas_to_world(int x, int y) noexcept;
    void open_document_dialog();
    void save_document_dialog(bool force_save_as = false);
    void export_svg_dialog();
    void paint_canvas_rulers(HDC dc, int width, int height);

    HWND hwnd_{nullptr};
    HINSTANCE hinstance_{nullptr};
#endif
    int width_{1440};
    int height_{900};
    UILanguage language_{UILanguage::Vietnamese};
    ThemeMode theme_{ThemeMode::CyberDark};

    // Interactive UI State
    int active_tool_{0}; // 0: Select, 1: Rectangle, 2: Ellipse, 3: Pan
    int active_workspace_{0}; // 0: Vector, 1: Print, 2: Production, 3: 3D
    int active_dock_tab_{0}; // 0: Inspector, 1: Layers, 2: History
    int zoom_level_{100}; // 50, 75, 100, 125, 150, 200

    // File Menu Dropdown State (Corel Reference)
    bool show_file_menu_{false};
    int hovered_file_item_{-1};

    // Home Screen State (Trang Home)
    bool show_home_screen_{false};
    int active_home_tab_{0}; // 0: Bắt đầu, 1: Tệp gần đây, 2: Mẫu thi công, 3: Phím tắt Corel

    // Settings Modal State
    bool show_settings_modal_{false};
    int settings_active_tab_{0}; // 0: Giao diện, 1: Đơn vị & Lưới, 2: Quản lý màu & ICC, 3: Thi công & CNC, 4: Hiệu năng
    int settings_theme_choice_{0}; // 0: Dark Mode, 1: Light Mode, 2: Auto OS
    int settings_accent_choice_{0}; // 0: Cyber Mint, 1: Sky Blue, 2: Purple Tech, 3: Amber CAD
    int settings_unit_choice_{0}; // 0: mm, 1: cm, 2: inch, 3: pt

    // Canvas object geometry (mm)
    double shape_x_{100.0};
    double shape_y_{150.0};
    double shape_w_{1850.0};
    double shape_h_{420.0};
    double cursor_mm_x_{100.0};
    double cursor_mm_y_{150.0};

    // Dragging state
    bool is_dragging_{false};
    bool is_panning_{false};
    bool space_down_{false};
    int wheel_delta_accumulator_{0};
    bool pointer_active_{false};
    bool is_creating_shape_{false};
    uint64_t pointer_start_revision_{0};
    int last_pointer_x_{0};
    int last_pointer_y_{0};
    int drag_start_x_{0};
    int drag_start_y_{0};
    double drag_init_shape_x_{100.0};
    double drag_init_shape_y_{150.0};
    geometry::Point2D pointer_down_world_{};
    geometry::Point2D pointer_current_world_{};
    geometry::Point2D drag_preview_delta_{};
    std::optional<geometry::Rect2D> drag_preview_bounds_;
    std::string document_path_;

    std::string status_msg_{"Sẵn sàng | Nhấp chuột để tương tác các công cụ"};

    DesktopWindow shell_;
    CompositionRoot root_;
    DocumentWorkflow workflow_;
};

} // namespace pikdraw::desktop
