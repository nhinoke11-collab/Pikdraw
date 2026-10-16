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
    [[nodiscard]] static std::string wide_to_utf8(const std::wstring& wide);

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

    // Multi-Document Tabs
    struct DocumentTab {
        std::string id{"doc-1"};
        std::string title{"Bản vẽ 1"};
        std::string file_path{""};
        double page_width_mm{297.0};
        double page_height_mm{210.0};
        std::string color_mode{"CMYK"}; // CMYK or RGB
        int dpi{300};
        std::string unit{"mm"};
        std::unique_ptr<CompositionRoot> root;
        std::unique_ptr<DocumentWorkflow> workflow;
        bool is_dirty{false};
    };

    [[nodiscard]] size_t tab_count() const noexcept { return tabs_.size(); }
    [[nodiscard]] size_t active_tab_index() const noexcept { return active_tab_index_; }
    [[nodiscard]] const std::vector<DocumentTab>& tabs() const noexcept { return tabs_; }
    [[nodiscard]] DocumentTab& active_tab() noexcept;
    [[nodiscard]] const DocumentTab& active_tab() const noexcept;

    void switch_to_tab(size_t index) noexcept;
    void close_tab(size_t index) noexcept;
    void create_new_tab(const std::string& title, double w_mm, double h_mm,
                        const std::string& color_mode = "CMYK", int dpi = 300);

    // New Document Modal (Corel / Illus style)
    [[nodiscard]] bool is_new_doc_modal_open() const noexcept { return show_new_doc_modal_; }
    [[nodiscard]] double new_doc_width() const noexcept { return new_doc_w_; }
    [[nodiscard]] double new_doc_height() const noexcept { return new_doc_h_; }
    void open_new_doc_modal() noexcept;
    void close_new_doc_modal() noexcept;
    void confirm_new_document() noexcept;

    // Aspect ratio lock & object sizing
    [[nodiscard]] bool is_aspect_ratio_locked() const noexcept { return aspect_ratio_locked_; }
    void toggle_aspect_ratio_lock() noexcept { aspect_ratio_locked_ = !aspect_ratio_locked_; }

    // Group Isolation Mode (Chế độ cô lập nhóm - Corel & Illustrator style)
    [[nodiscard]] bool is_in_isolation_mode() const noexcept { return !isolation_stack_.empty(); }
    [[nodiscard]] std::optional<document::ObjectId> current_isolated_group_id() const noexcept {
        if (isolation_stack_.empty()) return std::nullopt;
        return isolation_stack_.back();
    }
    [[nodiscard]] const std::vector<document::ObjectId>& isolation_stack() const noexcept { return isolation_stack_; }
    void enter_group_isolation(document::ObjectId group_id);
    void exit_group_isolation();
    void exit_all_isolation();
    void handle_double_click(int x, int y);
    [[nodiscard]] bool is_object_in_isolated_group(document::ObjectId id) const;

    // Page sizing & presets dropdown (CorelDRAW & Illustrator style)
    void set_active_page_size(double w_mm, double h_mm);
    void toggle_page_orientation();
    void toggle_page_preset_dropdown() noexcept;
    void close_page_preset_dropdown() noexcept;
    [[nodiscard]] bool is_page_preset_dropdown_open() const noexcept { return show_page_preset_dropdown_; }
    void select_page_preset(int preset_index) noexcept;

    // Object transform
    void resize_selected_object(double delta_w, double delta_h, bool change_w = true);
    void move_selected_object(double delta_x, double delta_y);

    // Color operations
    void apply_fill_to_selection(const std::string& hex);
    void apply_stroke_to_selection(const std::string& hex, double width_mm);

    // Solid & Shaping operations
    bool trigger_boolean_operation(geometry::BooleanOp op);
    [[nodiscard]] bool keep_source_object() const noexcept { return keep_source_object_; }
    [[nodiscard]] bool keep_target_object() const noexcept { return keep_target_object_; }
    void toggle_keep_source() noexcept { keep_source_object_ = !keep_source_object_; }
    void toggle_keep_target() noexcept { keep_target_object_ = !keep_target_object_; }
    void set_keep_source_object(bool keep) noexcept { keep_source_object_ = keep; }
    void set_keep_target_object(bool keep) noexcept { keep_target_object_ = keep; }
    bool trigger_auto_weld();

    // Typography & Inline Editing (Corel / Word Reference)
    void start_inline_text_editing(geometry::Point2D pos_mm, document::ObjectId existing_id = document::kInvalidObjectId);
    void commit_inline_text_editing();
    void cancel_inline_text_editing();
    void cycle_font_family();
    void adjust_font_size(double delta_pt);
    void toggle_font_bold();
    void toggle_font_italic();
    void toggle_font_underline();
    void set_font_alignment(typography::TextAlignment align);
    void convert_selected_text_to_curves();
    void insert_table(int rows = 3, int cols = 3);
    [[nodiscard]] bool is_text_object_selected() const;
    [[nodiscard]] bool is_inline_editing() const noexcept { return is_inline_text_editing_; }
    [[nodiscard]] const std::string& inline_text_buffer() const noexcept { return inline_text_buffer_; }
    [[nodiscard]] const std::string& status_message() const noexcept { return status_msg_; }

    // Align & Distribute (CorelDRAW & Illustrator Style)
    void align_selected_objects(geometry::AlignMode mode);
    void distribute_selected_objects(geometry::DistributeMode mode);
    void set_align_reference(geometry::AlignReference ref) noexcept { align_reference_ = ref; }
    [[nodiscard]] geometry::AlignReference align_reference() const noexcept { return align_reference_; }
    void set_distribute_spacing_mm(double gap_mm) noexcept { distribute_spacing_mm_ = gap_mm; }
    [[nodiscard]] double distribute_spacing_mm() const noexcept { return distribute_spacing_mm_; }
    void apply_specified_spacing(bool horizontal = true);
    void open_inspector_properties_panel() noexcept;
    void set_key_object(std::optional<document::ObjectId> id) noexcept { key_object_id_ = id; }
    [[nodiscard]] std::optional<document::ObjectId> key_object() const noexcept { return key_object_id_; }

    // Hover Tooltip System (Bilingual & Compact Icons)
    struct TooltipInfo {
        bool visible{false};
        int x{0};
        int y{0};
        std::wstring text;
    };
    [[nodiscard]] const TooltipInfo& tooltip() const noexcept { return tooltip_; }
    void update_hover_tooltip(int mouse_x, int mouse_y);

    // Guideline System (CorelDRAW & Illustrator Style)
    enum class GuidelineOrientation {
        Horizontal,
        Vertical
    };

    struct Guideline {
        GuidelineOrientation orientation{GuidelineOrientation::Horizontal};
        double position_mm{0.0};
        bool locked{false};
    };

    void add_guideline(GuidelineOrientation orient, double pos_mm);
    void toggle_guidelines() noexcept;
    void clear_guidelines() noexcept;
    void set_show_guidelines(bool show) noexcept { show_guidelines_ = show; }
    [[nodiscard]] bool show_guidelines() const noexcept { return show_guidelines_; }
    [[nodiscard]] const std::vector<Guideline>& guidelines() const noexcept { return guidelines_; }
    [[nodiscard]] size_t guideline_count() const noexcept { return guidelines_.size(); }
    [[nodiscard]] bool is_dragging_guideline() const noexcept { return is_dragging_guideline_; }
    [[nodiscard]] double drag_guideline_pos_mm() const noexcept { return drag_guideline_pos_mm_; }
    [[nodiscard]] GuidelineOrientation drag_guideline_orientation() const noexcept { return drag_guideline_orientation_; }

    // Numeric Keyboard Input System (Direct typing on Property Bar)
    enum class NumericFieldId {
        None,
        PageWidth,
        PageHeight,
        ObjectX,
        ObjectY,
        ObjectWidth,
        ObjectHeight,
        ModalWidth,
        ModalHeight
    };

    [[nodiscard]] NumericFieldId active_numeric_field() const noexcept { return active_num_field_; }
    [[nodiscard]] const std::string& numeric_input_buffer() const noexcept { return num_input_buffer_; }
    void start_numeric_input(NumericFieldId field);
    void commit_numeric_field();
    void cancel_numeric_field();
    void handle_numeric_char_input(wchar_t ch);

    [[nodiscard]] CompositionRoot& root() noexcept;
    [[nodiscard]] const CompositionRoot& root() const noexcept;

    [[nodiscard]] DocumentWorkflow& workflow() noexcept;
    [[nodiscard]] const DocumentWorkflow& workflow() const noexcept;

#ifdef _WIN32
    [[nodiscard]] HWND hwnd() const noexcept { return hwnd_; }
    bool handle_key_down(WPARAM key);
    void handle_mouse_down(int x, int y);
    void handle_mouse_move(int x, int y);
    void handle_mouse_up(int x, int y);
    [[nodiscard]] RECT canvas_rect(int width, int height) const noexcept;
    [[nodiscard]] bool is_canvas_point(int x, int y) const noexcept;
    [[nodiscard]] geometry::Point2D canvas_to_world(int x, int y) noexcept;
#endif

private:
#ifdef _WIN32
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    void paint(HDC hdc, int width, int height);
    void paint_settings_modal(HDC mem_dc, int width, int height);
    void paint_new_document_modal(HDC mem_dc, int width, int height);
    void paint_document_tab_bar(HDC mem_dc, int w, int topbar_h, int tabbar_h);
    void paint_file_menu(HDC mem_dc, int x, int y, int w, int h);
    void paint_home_screen(HDC mem_dc, int w, int h);
    void paint_document_canvas(HDC dc, int width, int height);
    void handle_canvas_mouse_down(int x, int y);
    void handle_canvas_mouse_move(int x, int y);
    void handle_canvas_mouse_up(int x, int y);
    void open_document_dialog();
    void save_document_dialog(bool force_save_as = false);
    void export_svg_dialog();
    void paint_canvas_rulers(HDC dc, int width, int height);
    void paint_canvas_guidelines(HDC dc, int width, int height);
    void paint_page_preset_dropdown(HDC dc, int x, int y, int width);

    HWND hwnd_{nullptr};
    HINSTANCE hinstance_{nullptr};
#endif
    int width_{1440};
    int height_{900};
    UILanguage language_{UILanguage::English};
    ThemeMode theme_{ThemeMode::Light};

    // Interactive UI State
    int active_tool_{0}; // 0: Select, 1: Rectangle, 2: Ellipse, 3: Pan, 4: Text
    int active_workspace_{0}; // 0: Vector, 1: Print, 2: Production, 3: 3D
    int active_dock_tab_{0}; // 0: Inspector, 1: Layers, 2: History
    int zoom_level_{100}; // 50, 75, 100, 125, 150, 200

    // Multi-Document Tabs
    std::vector<DocumentTab> tabs_;
    size_t active_tab_index_{0};
    void initialize_default_tab();

    // New Document Modal State
    bool show_new_doc_modal_{false};
    std::wstring new_doc_name_{L"Bản vẽ 1"};
    int new_doc_preset_{0}; // 0: A4, 1: A3, 2: A2, 3: Pano 2x1m, 4: Hộp đèn, 5: Tùy chỉnh
    double new_doc_w_{297.0};
    double new_doc_h_{210.0};
    bool new_doc_landscape_{true};
    int new_doc_color_mode_{0}; // 0: CMYK, 1: RGB
    int new_doc_dpi_{300}; // 300, 150, 72
    int new_doc_unit_{0}; // 0: mm, 1: cm, 2: m, 3: inch
    int next_doc_counter_{2};

    // Page Preset Dropdown List State (Corel / Illustrator Property Bar)
    bool show_page_preset_dropdown_{false};

    // Aspect ratio lock
    bool aspect_ratio_locked_{true};

    // Solid & Shaping state
    bool keep_source_object_{false};
    bool keep_target_object_{false};

    // Knife tool cutting state
    bool is_knife_cutting_{false};
    geometry::Point2D knife_start_world_{};
    geometry::Point2D knife_current_world_{};

    // Inline Text Editing & Typography State (Corel / Word Reference)
    bool is_inline_text_editing_{false};
    document::ObjectId inline_editing_text_id_{document::kInvalidObjectId};
    std::string inline_text_buffer_;
    geometry::Point2D inline_text_pos_mm_{};
    int font_family_index_{0}; // 0: "Be Vietnam Pro", 1: "Segoe UI", 2: "Arial", 3: "Times New Roman", 4: "JetBrains Mono"
    double font_size_pt_{24.0};
    bool font_bold_{false};
    bool font_italic_{false};
    bool font_underline_{false};
    typography::TextAlignment font_alignment_{typography::TextAlignment::Left};
    int table_rows_{3};
    int table_cols_{3};

    // Align & Distribute State (Corel / Illustrator)
    geometry::AlignReference align_reference_{geometry::AlignReference::Selection};
    double distribute_spacing_mm_{10.0};
    std::optional<document::ObjectId> key_object_id_{std::nullopt};

    // Shortcuts Preset Choice (0: CorelDRAW, 1: Illustrator, 2: Default PikDraw)
    int shortcut_profile_{0};

    // File Menu Dropdown State (Corel Reference)
    bool show_file_menu_{false};
    int hovered_file_item_{-1};

    // Home Screen State (Trang Home)
    bool show_home_screen_{false};
    int active_home_tab_{0}; // 0: Bắt đầu, 1: Tệp gần đây, 2: Mẫu thi công, 3: Phím tắt Corel

    // Settings Modal State
    bool show_settings_modal_{false};
    int settings_active_tab_{0}; // 0: Giao diện, 1: Đơn vị & Lưới, 2: Quản lý màu & ICC, 3: Thi công & CNC, 4: Hiệu năng, 5: Phím tắt
    int settings_theme_choice_{1}; // 0: Dark Mode, 1: Light Mode, 2: Auto OS
    int settings_accent_choice_{0}; // 0: Cyber Mint, 1: Sky Blue, 2: Purple Tech, 3: Amber CAD
    int settings_unit_choice_{0}; // 0: mm, 1: cm, 2: inch, 3: pt

    // Group Isolation Mode Stack
    std::vector<document::ObjectId> isolation_stack_;

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

    std::string status_msg_{"Ready | Click canvas or tools to start vector editing"};

    // Hover Tooltip State
    TooltipInfo tooltip_{};

    // Guideline State
    std::vector<Guideline> guidelines_;
    bool show_guidelines_{true};
    bool is_dragging_guideline_{false};
    GuidelineOrientation drag_guideline_orientation_{GuidelineOrientation::Horizontal};
    double drag_guideline_pos_mm_{0.0};

    // Numeric Input State (Direct typing on Property Bar & Modals)
    NumericFieldId active_num_field_{NumericFieldId::None};
    std::string num_input_buffer_;

    // Canvas Viewport Cache (Fast zero-allocation redraw during interaction)
    bool canvas_cache_valid_{false};
    uint64_t cached_revision_{0};
    double cached_zoom_{0.0};
    double cached_camera_x_{0.0};
    double cached_camera_y_{0.0};
    int cached_canvas_w_{0};
    int cached_canvas_h_{0};
    ThemeMode cached_theme_{ThemeMode::Light};
#ifdef _WIN32
    HDC cached_canvas_dc_{nullptr};
    HBITMAP cached_canvas_bmp_{nullptr};
    HGDIOBJ cached_canvas_old_bmp_{nullptr};
    std::vector<uint8_t> reusable_rgba_buffer_;
    std::vector<uint8_t> reusable_bgra_buffer_;
    void cleanup_canvas_cache() noexcept;

    // Double-buffered window backbuffer cache
    HDC backbuffer_dc_{nullptr};
    HBITMAP backbuffer_bmp_{nullptr};
    HGDIOBJ backbuffer_old_bmp_{nullptr};
    int backbuffer_w_{0};
    int backbuffer_h_{0};
    void cleanup_backbuffer() noexcept;

    // Cached UI fonts
    HFONT font_be_vietnam_{nullptr};
    HFONT font_be_vietnam_bold_{nullptr};
    HFONT font_secondary_{nullptr};
    HFONT font_mono_{nullptr};
    HFONT font_tab_{nullptr};
    HFONT font_tab_bold_{nullptr};
    HFONT font_ruler_{nullptr};
    void ensure_ui_fonts() noexcept;
    void cleanup_ui_fonts() noexcept;
#endif

    DesktopWindow shell_;
};

} // namespace pikdraw::desktop
