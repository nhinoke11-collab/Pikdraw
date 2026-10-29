#pragma once

#include "pikdraw/desktop/shell/desktop_window.hpp"
#include "pikdraw/desktop/performance/pointer_move_coalescer.hpp"
#include "pikdraw/desktop/runtime/composition_root.hpp"
#include "pikdraw/desktop/workflows/document_workflow.hpp"
#include "pikdraw/desktop/workflows/cut_geometry_job.hpp"
#include "pikdraw/desktop/presenters/object_style.hpp"
#include "pikdraw/color/preview/color_transform.hpp"
#include "pikdraw/color/profiles/profile_resource.hpp"
#include "pikdraw/document/storage/save_recovery.hpp"
#include "pikdraw/desktop/tools/canvas_text_editor.hpp"
#include "pikdraw/typography/layout/case_conversion.hpp"
#include "pikdraw/desktop/shell/native_window_layers_dock.hpp"
#include "pikdraw/vector/tools/shape_tools.hpp"
#include "pikdraw/vector/precision/snap/layout_snap.hpp"

#include <array>
#include <string>
#include <memory>
#include <optional>
#include <filesystem>
#include <functional>
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <cmath>
#include <unordered_set>
#include <thread>
#include <vector>
#include "pikdraw/geometry/primitives/point.hpp"
#include "pikdraw/geometry/units/length_unit.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"
#include "pikdraw/rendering/damage_tracker.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace pikdraw::raster {
class RasterEditPreviewService;
class RasterHistogramService;
}

namespace pikdraw::desktop {
struct CanvasFrameCache;
struct NativeWindowAutomationRuntime;
enum class CanvasTraceKind : std::uint8_t;
class CanvasTrace;

struct CanvasColorSample {
    bool valid{false};
    bool on_object{false};
    bool is_bitmap_pixel{false};
    bool has_gradient{false};
    bool has_object_style{false};
    document::ObjectId object_id{document::kInvalidObjectId};
    std::string object_label;
    int pixel_x{-1};
    int pixel_y{-1};
    uint8_t r{255};
    uint8_t g{255};
    uint8_t b{255};
    uint8_t a{255};
    color::ColorValue color_value{color::RGBColor{1.0, 1.0, 1.0, 1.0}};
    std::optional<document::GradientPaint> gradient_paint;
    EditorStyle object_style{};
    double object_opacity{1.0};
    document::BlendMode object_blend_mode{document::BlendMode::Normal};
};
#ifdef _WIN32
inline constexpr UINT kImportWorkerMessage = WM_APP + 0x37;
inline constexpr UINT kExportWorkerMessage = WM_APP + 0x38;
inline constexpr UINT kRasterExportWorkerMessage = WM_APP + 0x39;
inline constexpr UINT kAutosaveWorkerMessage = WM_APP + 0x3A;
inline constexpr UINT kRecoveryScanWorkerMessage = WM_APP + 0x3B;
inline constexpr UINT kNativeFileWorkerMessage = WM_APP + 0x3C;
inline constexpr UINT kRasterPreviewWorkerMessage = WM_APP + 0x3D;
inline constexpr UINT kRasterHistogramWorkerMessage = WM_APP + 0x3E;
inline constexpr UINT kRasterProfileWorkerMessage = WM_APP + 0x3F;
inline constexpr UINT kTestSheetWorkerMessage = WM_APP + 0x40;
inline constexpr UINT kCutGeometryWorkerMessage = WM_APP + 0x41;
inline constexpr UINT kCutGeometryProgressMessage = WM_APP + 0x42;
#endif
struct ImportJobSession;
struct RecoveryScanJobSession;
struct NativeFileJobSession;
struct ImportPlacementSession {
    document::AsyncProposalMetadata metadata;
    ImportedContentProposal proposal;
    std::string target_tab_id;
    std::string source_filename;
    geometry::Point2D pointer_world{};
    bool pointer_valid{false};
#ifdef _WIN32
    HBITMAP preview_bitmap{nullptr};
    uint32_t* preview_bitmap_pixels{nullptr};
    int preview_width_px{0};
    int preview_height_px{0};

    ~ImportPlacementSession() {
        if (preview_bitmap) DeleteObject(preview_bitmap);
    }
#endif
};
struct ExportJobSession;
struct RasterExportJobSession;
struct RasterProfileConversionJobSession;
struct TestSheetJobSession;
struct AutosaveJobSession;
enum class MenuActionId;
class NativeFontPicker;
namespace tools { class DrawingToolController; class PenToolController; class NodeToolController; class ContourToolController; class GradientToolController; class DimensionToolController; class ArtboardToolController; class AnnotationToolController; enum class AnnotationToolMode : uint8_t; }
enum class ToolboxSlotId : int;

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
    void enable_mcp(bool edit_access = false) noexcept {
        mcp_enabled_ = true;
        mcp_edit_access_ = edit_access;
    }
    [[nodiscard]] bool set_mcp_access(bool enabled, bool edit_access) noexcept;
    [[nodiscard]] bool mcp_listener_active() const noexcept;
    [[nodiscard]] bool mcp_edit_access_active() const noexcept;
    [[nodiscard]] const std::string& mcp_error_message() const noexcept;

    void capture_screenshot_bmp(const std::string& path);
    static constexpr std::uintptr_t kAutoSaveTimerId = 0x504B;
    static constexpr std::uintptr_t kCutGeometryPollTimerId = 0x504C;
    static constexpr std::uintptr_t kSmartTrimHoverTimerId = 0x504D;
    // Shell coordinates are DIP; only Win32 device boundaries use physical pixels.
    [[nodiscard]] int dip(int value) const noexcept {
        return static_cast<int>(std::lround(value * dpi_scale_));
    }

    [[nodiscard]] static std::wstring utf8_to_wide(const std::string& utf8);
    [[nodiscard]] static std::string wide_to_utf8(const std::wstring& wide);

    [[nodiscard]] UILanguage language() const noexcept { return language_; }
    void set_language(UILanguage lang) noexcept;
    void toggle_language() noexcept;

    [[nodiscard]] ThemeMode theme() const noexcept { return theme_; }
    void set_theme(ThemeMode mode) noexcept;
    void toggle_theme() noexcept;
    [[nodiscard]] color::DeviceRGBA canvas_background_color() const noexcept;
    [[nodiscard]] color::PreviewRGBA preview_color_for_ui(
        const color::ColorValue& value) const noexcept {
        const auto* preview = effective_color_preview();
        return preview ? preview->convert(value) : color::to_preview_rgba(value);
    }
#ifdef _WIN32
    [[nodiscard]] COLORREF canvas_background_colorref() const noexcept;
#endif

    [[nodiscard]] int active_tool() const noexcept { return active_tool_; }
    [[nodiscard]] bool knife_freehand_mode() const noexcept { return knife_freehand_mode_; }
    [[nodiscard]] double knife_gap_mm() const noexcept { return knife_gap_mm_; }
    [[nodiscard]] double vector_eraser_diameter_mm() const noexcept { return vector_eraser_diameter_mm_; }
    [[nodiscard]] bool vector_eraser_round_nib() const noexcept { return vector_eraser_round_nib_; }
    [[nodiscard]] size_t shape_builder_selected_face_count() const noexcept {
        return shape_builder_seed_points_.size();
    }
    [[nodiscard]] document::ObjectId trim_objects_target_id() const noexcept { return trim_objects_target_id_; }
    [[nodiscard]] const std::vector<document::ObjectId>& trim_objects_source_ids() const noexcept {
        return trim_objects_source_ids_;
    }
    [[nodiscard]] bool trim_objects_keep_sources() const noexcept { return trim_objects_keep_sources_; }
    [[nodiscard]] bool cut_geometry_preview_ready(CutGeometryOperation operation) const noexcept {
        return pending_cut_geometry_proposal_ &&
            pending_cut_geometry_proposal_->proposal.operation == operation;
    }
    [[nodiscard]] std::optional<geometry::Point2D> smart_trim_hover_point_for_testing() const noexcept {
        return smart_trim_hover_world_;
    }
    [[nodiscard]] std::size_t cut_geometry_preview_contour_count(
        CutGeometryOperation operation) const noexcept {
        return cut_geometry_preview_ready(operation)
            ? pending_cut_geometry_proposal_->proposal.preview_contours.size() : 0;
    }
    [[nodiscard]] bool cut_geometry_preview_contour_removed(
        CutGeometryOperation operation, std::size_t index) const noexcept {
        return cut_geometry_preview_ready(operation) &&
            index < pending_cut_geometry_proposal_->proposal.preview_contours.size() &&
            pending_cut_geometry_proposal_->proposal.preview_contours[index].removed;
    }
    void commit_trim_objects();
    void reset_trim_objects_roles();
    void commit_shape_builder(bool erase_faces);
    void reset_shape_builder();
    void set_active_tool(int tool_index) noexcept;
    [[nodiscard]] bool drawing_snap_panel_open() const noexcept;
    [[nodiscard]] bool drawing_snap_index_ready() const noexcept;
    [[nodiscard]] bool drawing_path_active() const noexcept;
    [[nodiscard]] size_t drawing_path_point_count() const noexcept;

    [[nodiscard]] int active_workspace() const noexcept { return active_workspace_; }
    void set_active_workspace(int ws_index) noexcept;

    [[nodiscard]] int active_dock_tab() const noexcept { return active_dock_tab_; }
    [[nodiscard]] bool page_units_flyout_open() const noexcept { return show_page_units_flyout_; }
    void set_active_dock_tab(int tab_index) noexcept;
    [[nodiscard]] LayersDockState& layers_dock_state() noexcept { return layers_state_; }
    [[nodiscard]] const LayersDockState& layers_dock_state() const noexcept { return layers_state_; }
    [[nodiscard]] size_t active_layer_index() const noexcept { return layers_state_.active_layer_index; }
    void set_active_layer_index(size_t index);
    [[nodiscard]] bool is_dock_text_editing() const noexcept {
        return artboard_name_editing_ || (active_dock_tab_ == 1 && (layers_state_.search_active || layers_state_.rename_layer_idx.has_value() || layers_state_.rename_object_id.has_value()));
    }
    [[nodiscard]] bool align_distribute_panel_visible() const noexcept { return show_align_panel_; }
    [[nodiscard]] bool is_text_input_context() const noexcept;
    [[nodiscard]] bool has_active_document_interaction() const noexcept;
    void on_external_document_change() noexcept;

    void zoom_in() noexcept;
    void zoom_out() noexcept;
    void zoom_to_actual_size() noexcept;
    void fit_page_to_canvas() noexcept;
    void fit_artboards_to_canvas() noexcept;
    [[nodiscard]] bool artboard_overview_enabled() const noexcept;
    void set_artboard_overview_enabled(bool enabled, bool save_default = true) noexcept;
    [[nodiscard]] bool set_active_artboard(document::ArtboardId id) noexcept;
    bool fit_document_content_to_canvas(bool* content_outside_page = nullptr) noexcept;
    void trigger_export() noexcept;
    void open_import_dialog();
    bool export_svg_to(const std::string& file_path, bool selected_objects = false,
                       bool allow_replace_existing = false,
                       bool all_pages = false,
                       bool active_artboard = false);
    void create_new_document();
    bool apply_selection_color(bool stroke, color::ColorValue value);
    bool set_selection_fill_enabled(bool enabled);
    bool set_selection_stroke_enabled(bool enabled);
    bool set_selection_stroke_width(double width_mm);
    bool configure_preview_profiles(const std::filesystem::path &rgb_source,
        const std::filesystem::path &cmyk_source, std::string *error = nullptr);
    bool refresh_display_preview_profile(bool force_reload = false);

    // Main Menu Bar & Dropdowns (CorelDRAW Reference)
    [[nodiscard]] bool is_menu_open() const noexcept { return open_menu_id_ >= 0; }
    [[nodiscard]] int open_menu_id() const noexcept { return open_menu_id_; }
    void open_menu(int menu_id) noexcept;
    void close_menu() noexcept;
    void toggle_menu(int menu_id) noexcept;
    // File Menu (CorelDRAW Reference - Backward Compatibility)
    [[nodiscard]] bool is_file_menu_open() const noexcept { return open_menu_id_ == 0 || show_file_menu_; }
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
    void open_ai_connections_settings() noexcept;
    void close_settings_modal() noexcept;
    void toggle_settings_modal() noexcept;
    [[nodiscard]] int settings_active_tab() const noexcept { return settings_active_tab_; }
    void set_settings_tab(int tab) noexcept;
    void toggle_ai_connections_read_choice() noexcept;
    void toggle_ai_connections_edit_choice() noexcept;
    void reset_ai_connections_choices() noexcept;
    [[nodiscard]] bool apply_ai_connections_choices() noexcept;
    void activate_ai_connections_focus() noexcept;
    [[nodiscard]] int ai_connections_focus_index() const noexcept { return settings_ai_focus_index_; }
    [[nodiscard]] bool ai_connections_read_choice() const noexcept { return settings_mcp_enabled_choice_; }
    [[nodiscard]] bool ai_connections_edit_choice() const noexcept { return settings_mcp_edit_choice_; }
    [[nodiscard]] int settings_tac_scope_choice() const noexcept { return settings_tac_scope_choice_; }
    [[nodiscard]] double settings_tac_limit_percent() const noexcept { return settings_tac_limit_percent_; }
    [[nodiscard]] const std::optional<color::TACSummary>& settings_tac_summary() const noexcept {
        return settings_tac_summary_;
    }
    [[nodiscard]] const std::string& settings_tac_error() const noexcept { return settings_tac_error_; }
    [[nodiscard]] int settings_tac_focus_index() const noexcept { return settings_tac_focus_index_; }
    void set_settings_tac_scope_choice(int scope) noexcept;
    void move_settings_tac_focus(int direction) noexcept;
    void activate_settings_tac_focus() noexcept;
    void validate_settings_tac() noexcept;
    void fix_settings_tac() noexcept;
    [[nodiscard]] int settings_theme_choice() const noexcept { return settings_theme_choice_; }
    void set_settings_theme_choice(int theme) noexcept;
    [[nodiscard]] int settings_accent_choice() const noexcept { return settings_accent_choice_; }
    void set_settings_accent_choice(int accent) noexcept;
    [[nodiscard]] int settings_unit_choice() const noexcept { return settings_unit_choice_; }
    [[nodiscard]] geometry::units::LengthUnit display_length_unit() const noexcept;
    void apply_preferred_dimension_unit() noexcept;
    void set_settings_unit_choice(int unit);
    void sync_display_unit_from_active_page() noexcept;
    [[nodiscard]] DesktopWindow& shell() noexcept { return shell_; }
    [[nodiscard]] const DesktopWindow& shell() const noexcept { return shell_; }

    // Multi-Document Tabs
    struct DocumentTab {
        std::string id{"doc-1"};
        std::string title{"Bản vẽ 1"};
        std::string file_path{""};
        std::string recovery_directory;
        uint64_t recovery_epoch{0};
        uint64_t recovery_revision{0};
        bool recovery_written{false};
        double page_width_mm{297.0};
        double page_height_mm{210.0};
        std::string unit{"mm"};
        document::ArtboardId active_artboard_id{document::kInvalidArtboardId};
        bool artboard_overview{false};
        std::unique_ptr<CompositionRoot> root;
        std::unique_ptr<DocumentWorkflow> workflow;
        bool is_dirty{false};
    };

    [[nodiscard]] size_t tab_count() const noexcept { return tabs_.size(); }
    [[nodiscard]] size_t active_tab_index() const noexcept { return active_tab_index_; }
    [[nodiscard]] bool cut_geometry_job_active() const noexcept {
        return static_cast<bool>(cut_geometry_job_);
    }
    [[nodiscard]] uint64_t view_generation() const noexcept { return view_generation_; }
    [[nodiscard]] const std::vector<DocumentTab>& tabs() const noexcept { return tabs_; }
    [[nodiscard]] bool is_recovery_modal_open() const noexcept { return show_recovery_modal_; }
    [[nodiscard]] bool is_recovery_scan_pending() const noexcept { return recovery_scan_pending_; }
    [[nodiscard]] bool is_native_file_job_pending() const noexcept { return native_file_job_pending_; }
    [[nodiscard]] size_t recovery_candidate_count() const noexcept { return recovery_candidates_.size(); }
    [[nodiscard]] DocumentTab& active_tab() noexcept;
    [[nodiscard]] const DocumentTab& active_tab() const noexcept;

    void switch_to_tab(size_t index) noexcept;
    void close_tab(size_t index) noexcept;
    void create_new_tab(const std::string& title, double w_mm, double h_mm,
                        const std::string& color_mode = "CMYK", int dpi = 0);

    // New Document Modal (Corel / Illus style)
    [[nodiscard]] bool is_new_doc_modal_open() const noexcept { return show_new_doc_modal_; }
    [[nodiscard]] double new_doc_width() const noexcept { return new_doc_w_; }
    [[nodiscard]] double new_doc_height() const noexcept { return new_doc_h_; }
    void open_new_doc_modal() noexcept;
    void close_new_doc_modal() noexcept;
    void confirm_new_document() noexcept;
    void close_recovery_modal() noexcept;

    // Aspect ratio lock & object sizing
    [[nodiscard]] bool is_aspect_ratio_locked() const noexcept { return aspect_ratio_locked_; }
    void toggle_aspect_ratio_lock() noexcept { aspect_ratio_locked_ = !aspect_ratio_locked_; }

    // Rectangle Quick Corner Rounding (CorelDRAW Style)
    [[nodiscard]] vector::RectCornerType rect_corner_type() const noexcept { return rect_corner_type_; }
    void set_rect_corner_type(vector::RectCornerType type) noexcept;
    [[nodiscard]] double rect_corner_tl() const noexcept { return rect_corner_tl_; }
    [[nodiscard]] double rect_corner_tr() const noexcept { return rect_corner_tr_; }
    [[nodiscard]] double rect_corner_br() const noexcept { return rect_corner_br_; }
    [[nodiscard]] double rect_corner_bl() const noexcept { return rect_corner_bl_; }
    [[nodiscard]] bool rect_corners_locked() const noexcept { return rect_corners_locked_; }
    void toggle_rect_corners_locked() noexcept;
    [[nodiscard]] bool rect_corners_scale_relative() const noexcept { return rect_corners_scale_relative_; }
    void toggle_rect_corners_scale_relative() noexcept;

    // Rectangle Tool creation defaults (always sharp corners 0.0mm by default, separated from selection)
    [[nodiscard]] vector::RectCornerType creation_rect_corner_type() const noexcept { return creation_rect_corner_type_; }
    void set_creation_rect_corner_type(vector::RectCornerType type) noexcept;
    [[nodiscard]] double creation_rect_corner_tl() const noexcept { return creation_rect_corner_tl_; }
    [[nodiscard]] double creation_rect_corner_tr() const noexcept { return creation_rect_corner_tr_; }
    [[nodiscard]] double creation_rect_corner_br() const noexcept { return creation_rect_corner_br_; }
    [[nodiscard]] double creation_rect_corner_bl() const noexcept { return creation_rect_corner_bl_; }
    [[nodiscard]] bool creation_rect_corners_locked() const noexcept { return creation_rect_corners_locked_; }
    void toggle_creation_rect_corners_locked() noexcept;
    [[nodiscard]] bool creation_rect_corners_scale_relative() const noexcept { return creation_rect_corners_scale_relative_; }
    void toggle_creation_rect_corners_scale_relative() noexcept;

    [[nodiscard]] bool is_selected_rectangle(document::ObjectId* out_id = nullptr, vector::RectCornerSettings* out_settings = nullptr) const;
    void apply_rect_corner_settings(const vector::RectCornerSettings& settings);
    void sync_rect_corner_controls_from_selection();

    // Rotation Mode & Pivot Point (CorelDRAW / Illustrator standard behavior)
    [[nodiscard]] bool is_rotate_mode() const noexcept { return is_rotate_mode_; }
    void set_rotate_mode(bool rotate) noexcept { is_rotate_mode_ = rotate; }
    void toggle_rotate_mode() noexcept { is_rotate_mode_ = !is_rotate_mode_; }
    [[nodiscard]] std::optional<geometry::Point2D> custom_pivot_world() const noexcept { return custom_pivot_world_; }
    void set_custom_pivot_world(std::optional<geometry::Point2D> pivot) noexcept { custom_pivot_world_ = pivot; }
    void reset_pivot() noexcept { custom_pivot_world_.reset(); }

    // Group Isolation Mode (Chế độ cô lập nhóm - Corel & Illustrator style)
    [[nodiscard]] bool is_in_isolation_mode() const noexcept;
    [[nodiscard]] std::optional<document::ObjectId> current_isolated_group_id() const noexcept {
        return is_in_isolation_mode() ? std::optional{isolation_stack_.back()} : std::nullopt;
    }
    [[nodiscard]] const std::vector<document::ObjectId>& isolation_stack() const noexcept { return isolation_stack_; }
    void enter_group_isolation(document::ObjectId group_id);
    void select_powerclip_contents(document::ObjectId group_id);
    void edit_powerclip_frame(document::ObjectId group_id);
    void synchronize_powerclip_view();
    void begin_powerclip_target_pick(bool copy_contents = false);
    bool handle_powerclip_target_pick(geometry::Point2D point);
    void register_powerclip_actions(CompositionRoot& document_root);
    std::optional<document::ObjectId> powerclip_properties_target() const;
#ifdef _WIN32
    bool paint_powerclip_controls(HDC dc, int left, int top, int right, bool vertical);
    bool handle_powerclip_controls(int x, int y, int left, int top, int right, bool vertical);
#endif
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
    void toggle_keep_source() noexcept { keep_source_object_ = !keep_source_object_; } void toggle_keep_target() noexcept { keep_target_object_ = !keep_target_object_; }
    void set_keep_source_object(bool k) noexcept { keep_source_object_ = k; } void set_keep_target_object(bool k) noexcept { keep_target_object_ = k; }
    bool trigger_auto_weld();

    // Typography & Inline Editing (Corel / Word Reference)
    void start_inline_text_editing(geometry::Point2D pos_mm, document::ObjectId existing_id = document::kInvalidObjectId);
    void commit_inline_text_editing();
    void cancel_inline_text_editing();
    bool handle_inline_text_key(uint32_t key, bool control, bool shift);
    bool handle_inline_text_character(uint32_t character);
    bool handle_inline_text_mouse(int x, int y, int phase, bool shift = false);
    void refresh_inline_text_preview();
    void sync_inline_text_controls();
    void apply_text_style_controls(const vector::TextPropertiesDelta& delta);
#ifdef _WIN32
    void paint_inline_text_overlay(HDC dc);
    bool handle_inline_text_ime(LPARAM flags);
    bool handle_annotation_note_ime(LPARAM flags);
    void paint_text_property_bar(HDC mem_dc, int w, int content_top, int topbar_h, int tabbar_h);
    void paint_dimension_property_bar(HDC mem_dc, int w, int content_top, int topbar_h, int tabbar_h);
    void paint_annotation_property_bar(HDC mem_dc, int w, int content_top, int topbar_h, int tabbar_h);
    bool handle_annotation_property_bar_click(int x, int y);
    void paint_annotation_properties_panel(HDC mem_dc, int w, int inspector_x, int content_y);
    bool handle_annotation_properties_panel_click(int x, int y);
    bool has_annotation_properties_target() const;
    bool handle_dimension_property_bar_click(int x, int y);
    void paint_dimension_properties_panel(HDC mem_dc, int insp_x, int inspect_w, int content_y, int max_h);
    bool handle_dimension_dock_click(int x, int y);
    void show_dimension_decimals_menu(int screen_x, int screen_y);
    void show_dimension_unit_menu(int screen_x, int screen_y);
    void show_dimension_arrow_type_menu(int screen_x, int screen_y);
    void show_dimension_line_width_menu(int screen_x, int screen_y);
    void show_dimension_text_size_menu(int screen_x, int screen_y);
    void show_dimension_text_position_menu(int screen_x, int screen_y);
#endif
    void toggle_all_dimensions_visibility();
    void toggle_all_dimensions_lock();
    void apply_dimension_style_to_all();
    void execute_quick_dimension();
    void execute_perimeter_dimension();
    void cycle_dimension_unit();
    void cycle_dimension_decimals();
    void cycle_dimension_arrow_type();
    void cycle_dimension_line_width();
    void cycle_dimension_text_size();
    [[nodiscard]] bool is_dimension_object_selected() const;
    void cycle_font_family();
    void adjust_font_size(double delta_pt);
    void toggle_font_bold(); void toggle_font_italic(); void toggle_font_underline(); void toggle_font_strikethrough();
    void toggle_text_bullets(); void toggle_text_numbering(); void toggle_non_printing_characters(); void toggle_convert_artistic_paragraph();
    void set_font_alignment(typography::TextAlignment align);
    void convert_selected_text_to_curves();
    void cycle_change_case();
    void apply_change_case(typography::ChangeCaseMode mode);
    void link_selected_text_frames();
    void unlink_selected_text_frames();
    void combine_selected_text_objects();
    void split_selected_text_lines();
    void fit_text_to_path();
    void straighten_text_from_path();
    void reverse_text_on_path();
    void sync_selected_text_controls();
    void insert_table(int rows = 3, int cols = 3);
    void open_font_picker(int anchor_x, int anchor_y);
    void close_font_picker();
    [[nodiscard]] bool is_font_picker_open() const noexcept;
    [[nodiscard]] NativeFontPicker* font_picker() noexcept;
    [[nodiscard]] bool is_text_object_selected() const;
    [[nodiscard]] bool is_inline_editing() const noexcept { return is_inline_text_editing_; }
    [[nodiscard]] bool is_inline_text_editing() const noexcept { return is_inline_text_editing_; }
    [[nodiscard]] const std::string& inline_text_buffer() const noexcept { return inline_text_buffer_; }
    [[nodiscard]] const std::string& status_message() const noexcept { return status_msg_; }
    void set_status_message(std::string message) { status_msg_ = std::move(message); }
    bool import_file_to_tab(const std::wstring& path, bool is_open_action = false);
    [[nodiscard]] bool import_job_active() const noexcept;
    [[nodiscard]] bool last_import_succeeded() const noexcept { return last_import_succeeded_; }
    void set_auto_accept_import_review(bool enabled) noexcept {
        auto_accept_import_review_ = enabled;
    }
    [[nodiscard]] int shortcut_profile() const noexcept { return shortcut_profile_; }
    void set_shortcut_profile(int p) noexcept { shortcut_profile_ = std::clamp(p, 0, 2); }

    // Align & Distribute (CorelDRAW & Illustrator Style)
    void align_selected_objects(geometry::AlignMode mode);
    void distribute_selected_objects(geometry::DistributeMode mode);
    void set_align_reference(geometry::AlignReference ref) noexcept { align_reference_ = ref; }
    [[nodiscard]] geometry::AlignReference align_reference() const noexcept { return align_reference_; }
    void set_distribute_spacing_mm(double gap_mm) noexcept { distribute_spacing_mm_ = gap_mm; }
    [[nodiscard]] double distribute_spacing_mm() const noexcept { return distribute_spacing_mm_; }
    void apply_specified_spacing(bool horizontal = true);
    void open_inspector_properties_panel() noexcept;
    void open_object_properties_panel() noexcept;
    void open_align_distribute_panel() noexcept;
    [[nodiscard]] bool style_inspector_visible() const noexcept { return show_style_inspector_; }
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

    // Dimensions and Corel Tool Resume State
    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] int previous_drawing_tool() const noexcept { return previous_drawing_tool_; }
    void set_previous_drawing_tool(int tool) noexcept { previous_drawing_tool_ = tool; }
    void execute_menu_action(int action_id);
    [[nodiscard]] bool is_menu_action_checked(MenuActionId action) const noexcept;
#ifdef _WIN32
    [[nodiscard]] HMENU build_context_menu(const std::vector<document::ObjectId>& selection, bool is_canvas_hit = true) const;
    bool show_context_menu(int screen_x, int screen_y, int canvas_x, int canvas_y);
#endif
    [[nodiscard]] WORD canvas_cursor_at(int x, int y) const;

    // Guideline System (CorelDRAW & Illustrator Style)
    enum class GuidelineOrientation { Horizontal, Vertical };
    struct Guideline {
        GuidelineOrientation orientation{GuidelineOrientation::Horizontal};
        double position_mm{0.0};
        bool locked{false};
        uint64_t id{0};
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
    [[nodiscard]] int guideline_at(int x, int y) const;
    [[nodiscard]] int selected_guideline_index() const noexcept { return selected_guideline_index_; }
    void set_selected_guideline_index(int idx) noexcept { selected_guideline_index_ = idx; }
    bool delete_selected_guideline() noexcept;

    // Numeric Keyboard Input System (Direct typing on Property Bar)
    enum class NumericFieldId {
        None,
        KnifeGap,
        VectorEraserDiameter,
        PageWidth,
        PageHeight,
        AnnotationScale,
        AnnotationTextSize,
        AnnotationSourceX, AnnotationSourceY, AnnotationSourceWidth, AnnotationSourceHeight,
        AnnotationFrameX, AnnotationFrameY,
        PolygonSides,
        PolygonRotation,
        ObjectX,
        ObjectY,
        ObjectWidth, ObjectHeight, ObjectRotation, ObjectOpacity,
        FillOpacity, StrokeOpacity, BlurSigmaX, BlurSigmaY, ShadowFeatherX, ShadowFeatherY,
        TextFontSize, TextFrameWidth, TextLineSpacing,
        ShadowOffsetX, ShadowOffsetY, ShadowOpacity, DistributeSpacing,
        ContourSpacing, ContourSteps, ContourTolerance, ContourMiterLimit,
        GradientStopOffset, GradientStopOpacity, GradientStopMidpoint,
        GradientAngle, GradientLength, GradientCenterX, GradientCenterY,
        PenExpression, PenLength, PenAngle, ModalWidth, ModalHeight,
        RectCornerRadiusTL, RectCornerRadiusTR, RectCornerRadiusBR, RectCornerRadiusBL,
        TacLimitPercent, GridSpacing,
        ArtboardX, ArtboardY, ArtboardWidth, ArtboardHeight,
        SnapAcquireRadius, SnapReleaseRadius,
        RasterExposureEv, RasterColorBalanceRedCyan,
        RasterColorBalanceGreenMagenta, RasterColorBalanceBlueYellow,
        RasterLightContrast, RasterLightHighlights, RasterLightShadows,
        RasterLightWhites, RasterLightBlacks,
        RasterHslHue, RasterHslSaturation, RasterHslLightness,
        RasterVibranceAmount,
        RasterLevelsBlackIn, RasterLevelsWhiteIn, RasterLevelsGamma,
        RasterLevelsBlackOut, RasterLevelsWhiteOut,
        RasterWhiteBalanceTemperature, RasterWhiteBalanceTint,
        RasterCurvePoint0, RasterCurvePoint1, RasterCurvePoint2,
        RasterCurvePoint3, RasterCurvePoint4
    };

    [[nodiscard]] NumericFieldId active_numeric_field() const noexcept { return active_num_field_; }
    [[nodiscard]] const std::string& numeric_input_buffer() const noexcept { return num_input_buffer_; }
    [[nodiscard]] const std::string& pen_numeric_length() const noexcept { return pen_numeric_length_; }
    [[nodiscard]] const std::string& pen_numeric_angle() const noexcept { return pen_numeric_angle_; }
    void update_pen_dimension_candidate();
    void start_numeric_input(NumericFieldId field);
    void start_effect_numeric_input(NumericFieldId field, size_t effect_index);
    void commit_numeric_field();
    void cancel_numeric_field();
    void handle_numeric_char_input(wchar_t ch);

    [[nodiscard]] const std::string& array_input_buffer() const noexcept { return array_input_buffer_; }
    void handle_array_char(char c);
    void handle_array_backspace();
    void commit_array_input();

    // Quick Shape Dimension Input (SketchUp style VCB)
    [[nodiscard]] bool is_quick_dimension_active() const noexcept { return quick_dimension_active_; }
    [[nodiscard]] const std::string& quick_dimension_buffer() const noexcept { return quick_dimension_buffer_; }
    void end_quick_dimension() noexcept;
    bool apply_quick_dimension();

    [[nodiscard]] CompositionRoot& root() noexcept;
    [[nodiscard]] const CompositionRoot& root() const noexcept;
    [[nodiscard]] DocumentWorkflow& workflow() noexcept;
    [[nodiscard]] const DocumentWorkflow& workflow() const noexcept;
    [[nodiscard]] tools::NodeToolController* node_tool() noexcept { return node_tool_.get(); }
    [[nodiscard]] const tools::NodeToolController* node_tool() const noexcept { return node_tool_.get(); }
    [[nodiscard]] tools::ContourToolController* contour_tool() noexcept { return contour_tool_.get(); }
    [[nodiscard]] const tools::ContourToolController* contour_tool() const noexcept { return contour_tool_.get(); }
    void trigger_effects_contour();
    void trigger_break_contour_apart();
    bool apply_contour_effect();
    bool handle_contour_property_click(int x, int y);
    void paint_contour_canvas_overlay(HDC hdc, const RECT& canvas);
    [[nodiscard]] tools::GradientToolController* gradient_tool() noexcept { return gradient_tool_.get(); }
    [[nodiscard]] const tools::GradientToolController* gradient_tool() const noexcept { return gradient_tool_.get(); }
    bool handle_gradient_property_click(int x, int y);
    bool handle_gradient_pointer_down(int x, int y, unsigned modifiers);
    bool handle_gradient_pointer_move(int x, int y, unsigned modifiers);
    bool handle_gradient_pointer_up(int x, int y, unsigned modifiers);
    bool handle_gradient_ramp_pointer_move(int x, int y);
    bool handle_gradient_ramp_pointer_up(int x, int y);
    void paint_gradient_canvas_overlay(HDC hdc, const RECT& canvas);
    [[nodiscard]] tools::DimensionToolController* dimension_tool() noexcept { return dimension_tool_.get(); }
    [[nodiscard]] const tools::DimensionToolController* dimension_tool() const noexcept { return dimension_tool_.get(); }
    void paint_dimension_canvas_overlay(HDC hdc, const RECT& canvas);
    [[nodiscard]] tools::AnnotationToolController* annotation_tool() noexcept { return annotation_tool_.get(); }
    [[nodiscard]] const tools::AnnotationToolController* annotation_tool() const noexcept { return annotation_tool_.get(); }
    void activate_annotation_tool(tools::AnnotationToolMode mode);
    void paint_annotation_canvas_overlay(HDC hdc, const RECT& canvas);
    bool handle_artboard_property_click(int x, int y);
    bool handle_artboard_pointer_down(int x, int y, unsigned modifiers);
    bool handle_artboard_pointer_move(int x, int y, unsigned modifiers);
    bool handle_artboard_pointer_up(int x, int y, unsigned modifiers);
    void update_artboard_snap_preview(const document::DocumentSnapshot& snapshot,
                                     geometry::Point2D pointer_world, unsigned modifiers);
    bool handle_artboard_tool_key(WPARAM key);
    bool handle_artboard_tool_char(wchar_t ch);
    void paint_artboard_canvas_overlay(HDC dc, const RECT& area);
    void paint_artboard_properties_panel(HDC dc, int window_width, int inspector_x, int content_y);
    bool commit_artboard_numeric_field(NumericFieldId field);
    bool handle_snap_toolbar_click(int x, int y);
    bool handle_snap_properties_click(int x, int y);
    void paint_snap_to_popup(HDC dc, int window_width, int topbar_height);
    void paint_snap_properties_panel(HDC dc, int window_width, int inspector_x, int content_y);

    // Toolbox Customization & Dynamic Slots
    [[nodiscard]] uint32_t toolbox_visible_mask() const noexcept { return toolbox_visible_mask_; }
    void set_toolbox_visible_mask(uint32_t mask) noexcept { toolbox_visible_mask_ = (mask | 1u); }
    [[nodiscard]] bool is_toolbox_slot_visible(ToolboxSlotId slot) const noexcept;
    void set_toolbox_slot_visible(ToolboxSlotId slot, bool visible) noexcept;
    void toggle_toolbox_slot_visible(ToolboxSlotId slot) noexcept;
    void reset_toolbox_slots_default() noexcept;
    [[nodiscard]] bool is_toolbox_edit_mode() const noexcept { return toolbox_edit_mode_; }
    void set_toolbox_edit_mode(bool edit_mode) noexcept { toolbox_edit_mode_ = edit_mode; }
    void toggle_toolbox_edit_mode() noexcept { toolbox_edit_mode_ = !toolbox_edit_mode_; }
    [[nodiscard]] bool is_toolbox_customize_open() const noexcept { return show_toolbox_customize_flyout_; }
    void open_toolbox_customize_flyout() noexcept { show_toolbox_customize_flyout_ = true; }
    void close_toolbox_customize_flyout() noexcept { show_toolbox_customize_flyout_ = false; }
    void toggle_toolbox_customize_flyout() noexcept { show_toolbox_customize_flyout_ = !show_toolbox_customize_flyout_; }
    [[nodiscard]] std::vector<ToolboxSlotId> visible_toolbox_slots() const;
    [[nodiscard]] int get_visible_row_for_slot(ToolboxSlotId slot) const;
    [[nodiscard]] int get_visible_index_for_slot(ToolboxSlotId slot) const noexcept;
    [[nodiscard]] int toolbox_columns() const noexcept { return toolbox_columns_; }
    void set_toolbox_columns(int cols) noexcept;
    void toggle_toolbox_columns() noexcept;
    [[nodiscard]] int toolbox_width() const noexcept;
    [[nodiscard]] RECT get_toolbox_slot_rect(int index) const noexcept;
    [[nodiscard]] RECT get_toolbox_plus_rect(int total_slots) const noexcept;
    [[nodiscard]] RECT get_toolbox_toggle_columns_rect() const noexcept;

#ifdef _WIN32
    [[nodiscard]] HWND hwnd() const noexcept { return hwnd_; }
    [[nodiscard]] int toolbox_open_group() const noexcept { return toolbox_open_group_; }
    [[nodiscard]] int toolbox_focus_group() const noexcept { return toolbox_focus_group_; }
    [[nodiscard]] int toolbox_focus_slot_index() const noexcept { return toolbox_focus_slot_index_; }
    [[nodiscard]] bool toolbox_has_keyboard_focus() const noexcept { return toolbox_keyboard_focus_; }
    [[nodiscard]] int toolbox_flyout_index() const noexcept { return toolbox_flyout_index_; }
    bool handle_key_down(WPARAM key);
    void handle_key_up(WPARAM key) noexcept;
    [[nodiscard]] bool space_pan_active() const noexcept { return space_down_; }
    bool handle_toolbox_key(WPARAM key, bool alt_pressed = false, bool shift_pressed = false);
    void handle_mouse_down(int x, int y);
    void handle_mouse_down(int x, int y, unsigned modifiers);
    void handle_mouse_move(int x, int y);
    void handle_mouse_move(int x, int y, unsigned modifiers);
    void handle_mouse_up(int x, int y);
    void handle_mouse_up(int x, int y, unsigned modifiers);
    void scroll_style_inspector(int wheel_delta);
    [[nodiscard]] int style_inspector_scroll_offset() const noexcept { return style_inspector_scroll_; }
    [[nodiscard]] RECT canvas_rect(int width, int height) const noexcept;
    [[nodiscard]] bool is_canvas_point(int x, int y) const noexcept;
    [[nodiscard]] geometry::Point2D canvas_to_world(int x, int y) noexcept;
    [[nodiscard]] int selection_handle_at(int x, int y) const;
    bool handle_chrome_click(int x, int y) { return handle_workspace_chrome_mouse_down(x, y, 0); }
    bool handle_layers_dock_mouse_wheel(int delta);
    bool handle_layers_dock_click(int x, int y, bool is_double_click = false);

    bool handle_layers_dock_mouse_down(int x, int y);
    bool handle_layers_dock_mouse_move(int x, int y);
    bool handle_layers_dock_mouse_up(int x, int y);
    bool handle_layers_dock_key(WPARAM key);
    bool handle_layers_dock_char(wchar_t ch);
    bool handle_layers_dock_context_menu(int screen_x, int screen_y, int x, int y);
    bool apply_style_patch(const vector::StylePatch &patch);
    [[nodiscard]] bool can_convert_selected_to_bitmap() const;
    bool convert_selected_to_bitmap(uint32_t dpi = 300);
#endif

private:
    enum class GradientRampDragKind : uint8_t { None, Stop, Midpoint };
    // Narrow test seam for the in-process MCP E2E; credentials stay out of the public API.
    friend struct NativeWindowMcpE2eAccess;
    friend struct NativeWindowImportE2eAccess;
    friend struct NativeWindowExportE2eAccess;
    friend struct NativeWindowFileIoE2eAccess;
    friend struct RasterProfileWorkflowTestAccess;
    friend struct DropShadowRibbon;

    bool start_mcp_gateway();
    void initialize_performance_trace() noexcept;
    void shutdown_performance_trace() noexcept;
    void record_performance_event(CanvasTraceKind kind,
                                  uint64_t event_id = 0,
                                  uint64_t frame_id = 0) noexcept;
    void queue_coalesced_mouse_move(int x, int y, unsigned modifiers);
    void flush_coalesced_mouse_move();
    void discard_ai_connections_choices() noexcept;
    [[nodiscard]] std::string recovery_root_path() const;
    void scan_recovery_checkpoints(bool show_when_found = true,
                                   bool force_modal = false,
                                   bool only_if_document_idle = false);
    void handle_recovery_scan_worker_message();
    void join_recovery_scan_worker() noexcept;
    void start_native_save(std::string path);
    void start_native_open(std::string path);
    void start_recovery_load(document::RecoveryInfo candidate);
    void handle_native_file_worker_message();
    void join_native_file_job() noexcept;
#ifdef _WIN32
    void request_cancel_native_open_for_tab(const std::string& tab_id) noexcept;
#endif
    void recover_selected_checkpoint();
    void discard_selected_checkpoint();
    [[nodiscard]] bool handle_recovery_key(WPARAM key);
    [[nodiscard]] bool handle_recovery_mouse_down(int x, int y);
    static constexpr int kStylePanelTop = 162;
    static constexpr int kAppearanceBlendTop = 616;
    static constexpr int kAppearanceEffectsTop = 708;
    static constexpr int kAppearanceEffectListTop = 794;
#ifdef _WIN32
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    void request_cancel_import_job();
    void cancel_import_job();
    void handle_import_worker_message(WPARAM kind);
    [[nodiscard]] bool import_placement_active() const noexcept;
    void cancel_import_placement();
    bool start_cut_geometry_job(CutGeometryRequest request);
    void cancel_cut_geometry_job() noexcept;
    void handle_cut_geometry_worker_message(std::uint64_t generation);
    void handle_cut_geometry_progress_message(std::uint64_t generation,
                                               std::size_t completed);
    void poll_cut_geometry_job();
    void update_smart_trim_hover_preview(const geometry::Point2D& point,
                                         bool inside_canvas);
    void process_smart_trim_hover_timer();
    bool apply_pending_cut_geometry_proposal(CutGeometryOperation operation,
                                             bool erase_faces = false);
    void discard_pending_cut_geometry_proposal() noexcept;
    void paint_cut_geometry_preview_overlay(HDC dc, const RECT& area);
    [[nodiscard]] bool handle_import_placement_escape(WPARAM key);
    [[nodiscard]] bool place_pending_import_at(int x, int y);
    void paint_import_placement_overlay(HDC dc);
    void update_layout_snap_preview(const document::DocumentSnapshot& snapshot,
                                    geometry::Point2D raw_delta,
                                    int resize_handle = -1,
                                    bool preserve_aspect = false,
                                    bool shape_point = false);
    void paint_smart_guide_overlay(HDC dc, const RECT& area);
    void export_svg_dialog(bool selected_objects, bool all_pages,
                           bool active_artboard = false);
    [[nodiscard]] bool start_svg_export(const std::string& file_path, bool selected_objects,
                                        bool allow_replace_existing, bool all_pages,
                                        bool active_artboard = false);
    void request_cancel_export_job();
    void cancel_export_job();
    void handle_export_worker_message(WPARAM kind);
    [[nodiscard]] bool export_job_active() const noexcept;
    void export_raster_dialog();
    void export_selected_bitmap_master();
    void export_selected_bitmap_master_cmyk();
    [[nodiscard]] bool can_export_selected_bitmap_test_sheet() const;
    void export_selected_bitmap_test_sheet();
    void request_cancel_test_sheet_job();
    void cancel_test_sheet_job();
    void handle_test_sheet_worker_message(WPARAM kind);
    void print_selected_bitmap_test_sheet();
    void import_measurement_run();
    void compare_measurement_runs();
    void request_cancel_raster_export_job();
    void cancel_raster_export_job();
    void handle_raster_export_worker_message(WPARAM kind);
    [[nodiscard]] bool raster_export_job_active() const noexcept;
    [[nodiscard]] bool can_edit_selected_bitmap(bool require_srgb_preview = false) const;
    [[nodiscard]] bool can_export_selected_bitmap_master() const;
    [[nodiscard]] bool can_export_selected_bitmap_cmyk_master() const;
    [[nodiscard]] bool can_assign_selected_bitmap_profile() const;
    void assign_selected_bitmap_profile();
    [[nodiscard]] bool can_convert_selected_bitmap_profile_to_srgb() const;
    void convert_selected_bitmap_profile_to_srgb();
    void request_cancel_raster_profile_conversion();
    void cancel_raster_profile_conversion();
    void handle_raster_profile_worker_message(WPARAM kind);
    [[nodiscard]] bool raster_profile_conversion_active() const noexcept;
    void edit_selected_bitmap_crop();
    bool start_raster_eraser_stroke(const geometry::Point2D& point_world);
    bool commit_raster_eraser_stroke(
        const std::vector<geometry::Point2D>& points_world);
    void edit_selected_bitmap_exposure();
    [[nodiscard]] std::optional<double> selected_bitmap_exposure_ev() const;
    bool set_selected_bitmap_exposure_ev(double exposure_ev);
    [[nodiscard]] document::RasterLightParams selected_bitmap_light() const;
    [[nodiscard]] bool selected_bitmap_has_light_node() const;
    bool set_selected_bitmap_light_value(NumericFieldId field, double value);
    [[nodiscard]] document::RasterHslParams selected_bitmap_hsl() const;
    [[nodiscard]] double selected_bitmap_vibrance_amount() const;
    bool set_selected_bitmap_color_adjustment(NumericFieldId field,
                                               double value);
    [[nodiscard]] document::RasterLevelsParams selected_bitmap_levels() const;
    bool set_selected_bitmap_levels_value(NumericFieldId field, double value);
    bool set_selected_bitmap_levels_channel(document::RasterToneChannel channel);
    [[nodiscard]] document::RasterWhiteBalanceParams
    selected_bitmap_white_balance() const;
    bool set_selected_bitmap_white_balance_value(NumericFieldId field,
                                                 double value);
    [[nodiscard]] document::RasterCurvesParams selected_bitmap_curves() const;
    bool set_selected_bitmap_curves(document::RasterCurvesParams parameters);
    bool set_selected_bitmap_curve_point(NumericFieldId field, double value);
    bool set_selected_bitmap_curves_channel(document::RasterToneChannel channel);
    [[nodiscard]] document::RasterColorBalanceParams
        selected_bitmap_color_balance() const;
    bool set_selected_bitmap_color_balance_value(NumericFieldId field,
                                                 double value);
    bool toggle_selected_bitmap_color_balance_preserve_luminosity();
    void edit_selected_bitmap_light();
    void edit_selected_bitmap_color_balance();
    void edit_selected_bitmap_channel_mixer();
    void edit_selected_bitmap_local_exposure_mask();
    void edit_selected_bitmap_adjustment_mask();
    void edit_selected_bitmap_levels();
      void edit_selected_bitmap_hsl();
    void edit_selected_bitmap_curves();
    void edit_selected_bitmap_white_balance();
    void edit_selected_bitmap_vibrance();
    void edit_selected_bitmap_selective_hsl();
    void edit_selected_bitmap_clone_stamp();
    void flip_selected_bitmap(bool horizontal);
    [[nodiscard]] document::RasterSelectiveHslParams
    selected_bitmap_selective_hsl(document::RasterHueRange range) const;
    bool set_selected_bitmap_selective_hsl_value(
        document::RasterHueRange range, int component, double value);
    void focus_raster_adjustment_in_dock(int tab, int hsl_channel = -1);
    bool auto_levels_selected_bitmap();
    bool reset_selected_bitmap_tab_adjustments(int tab);
    bool handle_raster_dock_interactive_mouse_down(int x, int y);
    bool handle_raster_dock_interactive_mouse_move(int x, int y);
    bool handle_raster_dock_interactive_mouse_up(int x, int y);

    [[nodiscard]] int inspector_width() const noexcept;
    [[nodiscard]] int properties_dock_content_height();
    void scroll_properties_dock(int wheel_delta);
    void reset_properties_dock_scroll() noexcept;

    // Eyedropper & Live Canvas Color Inspection
    [[nodiscard]] CanvasColorSample sample_canvas_color_at(int client_x, int client_y) const;
    [[nodiscard]] bool apply_displayed_canvas_pixel(
        int client_x, int client_y, CanvasColorSample& sample) const;
    [[nodiscard]] const CanvasColorSample& hover_color_sample() const noexcept { return hover_color_sample_; }
    [[nodiscard]] const CanvasColorSample& eyedropper_picked_sample() const noexcept { return eyedropper_picked_sample_; }
    [[nodiscard]] int eyedropper_mode() const noexcept { return eyedropper_mode_; }
    void set_eyedropper_mode(int mode) noexcept { eyedropper_mode_ = std::clamp(mode, 0, 2); }
    bool apply_eyedropper_sample_to_objects(
        const CanvasColorSample& sample,
        const std::vector<document::ObjectId>& target_ids,
        bool apply_to_stroke = false,
        bool force_solid_color = false);
    bool handle_eyedropper_mouse_down(int x, int y, unsigned modifiers);
    bool handle_eyedropper_mouse_move(int x, int y, unsigned modifiers);
    bool handle_eyedropper_property_bar_click(int x, int y);
#ifdef _WIN32
    void paint_raster_inspector_dock(HDC mem_dc, int panel_left, int panel_width, int content_y);
    void paint_eyedropper_property_bar(HDC mem_dc, int w, int topbar_h, int tabbar_h, int content_top);
    void paint_eyedropper_cursor_overlay(HDC dc, int width, int height);
#endif
    void handle_raster_preview_worker_message();
    void handle_raster_histogram_worker_message();
    void handle_autosave_worker_message();
    void drain_autosave_jobs(bool reschedule_latest);
    void join_autosave_jobs() noexcept;
    void paint(HDC hdc, int width, int height);
    void paint_settings_modal(HDC mem_dc, int width, int height);
    void paint_new_document_modal(HDC mem_dc, int width, int height);
    void paint_recovery_modal(HDC mem_dc, int width, int height);
    void paint_document_tab_bar(HDC mem_dc, int w, int topbar_h, int tabbar_h);
    void paint_status_bar(HDC mem_dc, int width, int height, int status_height);
    void paint_topmost_ui_layer(HDC dc, int width, int height);
    void paint_left_toolbox(HDC mem_dc, int w, int h);
    void paint_right_inspector_dock(HDC mem_dc, int w, int h);
    void paint_layers_dock(HDC mem_dc, int insp_x, int content_y, int w, int h, int status_h);

    void paint_main_menu_bar(HDC mem_dc, int w, int topbar_h);
    void paint_dropdown_menu(HDC mem_dc, int menu_id, int x, int y);
    void paint_text_case_submenu(HDC mem_dc);
    [[nodiscard]] RECT text_case_menu_parent_rect() const noexcept;
    [[nodiscard]] RECT text_case_submenu_rect() const noexcept;
    void clear_text_case_submenu_tooltip() noexcept;
    bool handle_text_case_submenu_mouse_move(int x, int y);
    bool handle_text_case_submenu_mouse_down(int x, int y);
    bool handle_text_case_submenu_key(unsigned key);
    RECT get_menu_bar_item_rect(int menu_index, int topbar_h) const noexcept;
    int get_menu_bar_item_at(int mouse_x, int mouse_y, int topbar_h) const noexcept;
    [[nodiscard]] bool is_menu_action_enabled(MenuActionId action) const;
    void activate_toolbox_effect_item(int item);
    void paint_file_menu(HDC mem_dc, int x, int y, int w, int h);
    void paint_home_screen(HDC mem_dc, int w, int h);
    void paint_document_canvas(HDC dc, int width, int height);
    bool handle_modal_and_home_mouse_down(int x, int y);
    bool handle_workspace_chrome_mouse_down(int x, int y, unsigned modifiers);
    bool handle_cut_tool_properties_click(int x, int y, bool in_properties_dock);
    void handle_canvas_mouse_down(int x, int y, unsigned modifiers);
    void handle_canvas_mouse_move(int x, int y, unsigned modifiers);
    void handle_canvas_mouse_up(int x, int y, unsigned modifiers);
    void cancel_canvas_interaction(bool cancel_cut_job = true);
    bool commit_created_shape();
    [[nodiscard]] geometry::Rect2D shape_drag_bounds(unsigned modifiers) const;
    [[nodiscard]] double shape_drag_polygon_rotation(unsigned modifiers) const noexcept;
    void paint_selection_overlay(HDC dc);
    void paint_style_inspector(HDC dc, int left, int top, int bottom);
    bool handle_style_click(int x, int y, bool right_click = false);
    [[nodiscard]] int style_inspector_content_height() const;
    [[nodiscard]] bool style_numeric_field_at(int x, int y) const;
    bool handle_align_panel_click(int x, int y);
    void open_style_editor(bool stroke);
    void choose_stroke_width(int x, int y);
    void show_color_profiles();
    void assign_active_page_color_profile();
    void convert_active_page_color_profile();
    void assign_selected_paint_color_profile();
    void convert_selected_paint_color_profile();
    void convert_document_color_profile();
    void load_color_preferences();
    void open_document_dialog();
    void save_document_dialog(bool force_save_as = false);
    void autosave_documents();
    [[nodiscard]] std::string recovery_directory_for_tab(DocumentTab& tab);
    void export_svg_dialog();
    void paint_canvas_rulers(HDC dc, int width, int height);
    void paint_canvas_overlay_layer(HDC dc, int width, int height);
    bool paint_cached_canvas_overlay(HDC dc, int width, int height, const RECT& dirty);
    void invalidate_canvas_overlay();
    void paint_canvas_interaction_handles(HDC dc, int width, int height);
    bool paint_direct_present_frame(int width, int height);
    void cleanup_direct_present_target() noexcept;
    void paint_drawing_overlay(HDC dc);
    void paint_pen_overlay(HDC dc);
    void paint_drawing_tool_overlays(HDC dc);
    bool handle_drawing_panel_click(int x, int y);
    bool handle_drawing_click(int x, int y, unsigned modifiers);
    bool handle_drawing_character(char value);
    bool handle_drawing_key_down(WPARAM key);
    bool handle_pen_pointer_down(int x, int y, unsigned modifiers);
    bool handle_pen_pointer_move(int x, int y, unsigned modifiers);
    bool handle_pen_pointer_up(int x, int y, unsigned modifiers);
    bool handle_node_pointer_down(int x, int y, unsigned modifiers);
    bool handle_node_pointer_move(int x, int y, unsigned modifiers);
    bool handle_node_pointer_up(int x, int y, unsigned modifiers);
    bool handle_node_property_click(int x, int y);
    void paint_node_canvas_overlay(HDC dc, int width, int height);
    bool handle_pen_character(char value);
    bool handle_pen_key_down(WPARAM key);
    bool finish_pen_session(bool close_path, bool cancel = false);
    [[nodiscard]] geometry::Point2D pen_snap_point(
        geometry::Point2D raw_world, unsigned modifiers);
    void finish_drawing_tool(bool select_tool);
    [[nodiscard]] double window_dpi_scale() const noexcept;
    void paint_canvas_guidelines(HDC dc, int width, int height);
    void paint_page_preset_dropdown(HDC dc, int x, int y, int width);

    HWND hwnd_{nullptr};
    void configure_ui_dc(HDC dc) const noexcept {
        SetMapMode(dc, MM_ANISOTROPIC); SetWindowExtEx(dc, 96, 96, nullptr);
        const int dpi = static_cast<int>(std::lround(dpi_scale_ * 96)); SetViewportExtEx(dc, dpi, dpi, nullptr);
    }
    HWND tooltip_hwnd_{nullptr};
    HINSTANCE hinstance_{nullptr};
#endif
    float dpi_scale_{1.0f};
    [[nodiscard]] int logical_pixel(int value) const noexcept { return static_cast<int>(std::lround(value / dpi_scale_)); }
    int width_{1440};
    int height_{900};
    UILanguage language_{UILanguage::English};
    ThemeMode theme_{ThemeMode::Light};

    // Interactive UI State
    int active_tool_{0}; // 0: Select, 1: Rectangle, 2: Ellipse, 3: Pan, 4: Text, 5: Knife, 6: Line, 7: Polyline, 8: Pen, 9: Polygon
    int quick_shapes_last_tool_{1};
    int path_drawing_last_tool_{6};
    int toolbox_open_group_{0}; // 0: closed, 1: Quick Shapes, 2: Path Drawing, 3: Effects
    int toolbox_focus_group_{1};
    int toolbox_focus_slot_index_{0};
    bool toolbox_keyboard_focus_{false};
    int toolbox_flyout_index_{0};
    int toolbox_columns_{1}; // 1 or 2 columns layout
    uint32_t toolbox_visible_mask_{0x7FFF}; // all 15 slots
    bool toolbox_edit_mode_{false};
    bool show_toolbox_customize_flyout_{false};
    int polygon_sides_{6};
    double polygon_rotation_degrees_{-90.0};
    vector::RectCornerType rect_corner_type_{vector::RectCornerType::Round};
    double rect_corner_tl_{0.0};
    double rect_corner_tr_{0.0};
    double rect_corner_br_{0.0};
    double rect_corner_bl_{0.0};
    bool rect_corners_locked_{true};
    bool rect_corners_scale_relative_{false};
    vector::RectCornerType creation_rect_corner_type_{vector::RectCornerType::Round};
    double creation_rect_corner_tl_{0.0};
    double creation_rect_corner_tr_{0.0};
    double creation_rect_corner_br_{0.0};
    double creation_rect_corner_bl_{0.0};
    bool creation_rect_corners_locked_{true};
    bool creation_rect_corners_scale_relative_{false};
    int active_workspace_{0}; // 0: Vector, 1: Print, 2: Production, 3: 3D
    int active_dock_tab_{0}; // 0: Inspector, 1: Layers, 2: History
    bool show_style_inspector_{false};
    bool show_align_panel_{false};
    bool show_snap_to_popup_{false};
    bool show_snap_properties_{false};
    bool artboard_name_editing_{false};
    std::wstring artboard_name_buffer_;
    document::ArtboardId artboard_name_target_{document::kInvalidArtboardId};
    std::unique_ptr<tools::ArtboardToolController> artboard_tool_;
    int artboard_pointer_start_x_{0};
    int artboard_pointer_start_y_{0};
    uint64_t artboard_pointer_revision_{0};
    bool show_page_units_flyout_{false};
    int page_units_flyout_focus_index_{0};
    int style_inspector_scroll_{0};
    LayersDockState layers_state_{};
    int zoom_level_{100}; // 50, 75, 100, 125, 150, 200

    // Multi-Document Tabs
    std::vector<DocumentTab> tabs_;
#ifdef _WIN32
    std::vector<std::shared_ptr<AutosaveJobSession>> autosave_jobs_;
    std::shared_ptr<RecoveryScanJobSession> recovery_scan_job_;
    std::thread recovery_scan_worker_;
    std::shared_ptr<NativeFileJobSession> native_file_job_;
#endif
    bool mcp_enabled_{false};
    bool mcp_edit_access_{false};
    std::unique_ptr<NativeWindowAutomationRuntime> mcp_runtime_;
    std::string mcp_error_text_;
    size_t active_tab_index_{0};
    uint64_t view_generation_{0};
    void initialize_default_tab();
    void register_drawing_actions(CompositionRoot& root);
    void bind_drawing_tool_to_active_document();

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

    // Recovery Review modal state; candidates contain metadata from validated snapshots.
    std::vector<document::RecoveryInfo> recovery_candidates_;
    std::unordered_set<std::string> restored_recovery_paths_;
    bool recovery_candidates_truncated_{false};
    bool show_recovery_modal_{false};
    bool recovery_scan_pending_{false};
    bool recovery_scan_show_when_found_{false};
    bool recovery_scan_force_modal_{false};
    bool recovery_scan_only_if_idle_{false};
    bool recovery_scan_user_activity_{false};
    bool native_file_job_pending_{false};
    bool native_file_job_user_activity_{false};
    std::string recovery_scan_idle_document_id_;
    uint64_t recovery_scan_idle_epoch_{0};
    uint64_t recovery_scan_idle_revision_{0};
    uint64_t recovery_scan_idle_view_generation_{0};
    size_t recovery_scan_idle_tab_count_{0};
    size_t recovery_selected_index_{0};
    size_t recovery_first_visible_{0};
    int recovery_focus_index_{1}; // 0: list, 1: Recover, 2: Discard, 3: Later

    // Page Preset Dropdown List State (Corel / Illustrator Property Bar)
    bool show_page_preset_dropdown_{false};

    // Aspect ratio lock (property bar lock)
    bool aspect_ratio_locked_{true};

    // Rotation Mode & Pivot Point State
    bool is_rotate_mode_{false};
    std::optional<geometry::Point2D> custom_pivot_world_{};

    // Solid & Shaping state
    bool keep_source_object_{false};
    bool keep_target_object_{false};

    // Knife tool cutting state
    bool is_knife_cutting_{false};
    bool is_scissors_click_{false};
    bool is_trim_role_click_{false};
    bool is_smart_trim_click_{false};
    bool is_shape_builder_click_{false};
    bool is_smart_fill_click_{false};
    bool is_raster_eraser_stroke_{false};
    std::vector<geometry::Point2D> shape_builder_seed_points_;
    std::vector<geometry::Point2D> shape_builder_gesture_;
    bool shape_builder_gesture_overflow_{false};
    bool shape_builder_erase_gesture_{false};
    bool knife_freehand_mode_{false};
    double knife_gap_mm_{0.0};
    double vector_eraser_diameter_mm_{2.0};
    bool vector_eraser_round_nib_{true};
    geometry::Point2D knife_start_world_{};
    geometry::Point2D knife_current_world_{};
    std::vector<geometry::Point2D> knife_stroke_;
    bool knife_stroke_overflow_{false};
    std::vector<geometry::Point2D> eraser_stroke_;
    bool eraser_stroke_overflow_{false};
    std::vector<geometry::Point2D> raster_eraser_stroke_;
    bool raster_eraser_stroke_overflow_{false};
    double raster_eraser_diameter_px_{12.0};
    document::ObjectId trim_objects_target_id_{document::kInvalidObjectId};
    std::vector<document::ObjectId> trim_objects_source_ids_;
    bool trim_objects_keep_sources_{true};

    // Interactive Contour Dragging State (CorelDRAW / Adobe Reference)
    bool is_contour_dragging_{false};
    geometry::Point2D contour_drag_start_world_{};
    bool contour_applied_{false};

    // Inline Text Editing & Typography State (Corel / Word Reference)
    bool is_inline_text_editing_{false};
    document::ObjectId inline_editing_text_id_{document::kInvalidObjectId};
    std::optional<tools::CanvasTextEditor> text_editor_;
    std::string inline_text_buffer_;
    geometry::Point2D inline_text_pos_mm_{};
    int font_family_index_{0}; // 0: "Be Vietnam Pro", 1: "Segoe UI", 2: "Arial", 3: "Times New Roman", 4: "JetBrains Mono"
    double font_size_pt_{24.0};
    bool font_bold_{false}, font_italic_{false}, font_underline_{false}, font_strikethrough_{false}, show_non_printing_characters_{false};
    typography::TextAlignment font_alignment_{typography::TextAlignment::Left};
    int table_rows_{3}, table_cols_{3};
    std::unique_ptr<NativeFontPicker> font_picker_;

    // Align & Distribute State (Corel / Illustrator)
    geometry::AlignReference align_reference_{geometry::AlignReference::Selection};
    double distribute_spacing_mm_{10.0};
    std::optional<document::ObjectId> key_object_id_{std::nullopt};

    // Shortcuts Preset Choice (0: CorelDRAW, 1: Illustrator, 2: Default PikDraw)
    int shortcut_profile_{0};

    // Main Menu Bar State (CorelDRAW Reference)
    int open_menu_id_{-1};
    int hovered_menu_bar_item_{-1};
    int hovered_menu_item_{-1};
    int open_submenu_action_{-1};
    int hovered_submenu_item_{-1};

    // File Menu Dropdown State (Corel Reference - Compatibility)
    bool show_file_menu_{false};
    int hovered_file_item_{-1};

    // Home Screen State (Trang Home)
    bool show_home_screen_{false};
    int active_home_tab_{0}; // 0: Bắt đầu, 1: Tệp gần đây, 2: Mẫu thi công, 3: Phím tắt Corel

    // Settings Modal State
    bool show_settings_modal_{false};
    int settings_active_tab_{0}; // 0: Giao diện, 1: Đơn vị & Lưới, 2: Quản lý màu & ICC, 3: Thi công & CNC, 4: Hiệu năng, 5: Phím tắt
    int settings_theme_choice_{1}; // 0: Dark Mode, 1: Light Mode, 2: Auto OS
    bool settings_artboard_overview_choice_{false};
    int settings_accent_choice_{0}; // 0: Cyber Mint, 1: Sky Blue, 2: Purple Tech, 3: Amber CAD
    int settings_unit_choice_{0}; // mm, cm, in, pt, m, ft, yd, pc
    int settings_tac_scope_choice_{0}; // 0: document, 1: active page, 2: selection
    int settings_tac_focus_index_{0}; // Document, page, selection, limit, check, fix
    double settings_tac_limit_percent_{300.0};
    std::optional<color::TACSummary> settings_tac_summary_;
    std::string settings_tac_error_;
    uint64_t settings_tac_report_epoch_{0};
    uint64_t settings_tac_report_revision_{0};
    std::string settings_tac_report_document_id_;
    document::PageId settings_tac_report_active_page_{0};
    std::vector<document::ObjectId> settings_tac_report_selection_;
    double settings_tac_report_limit_percent_{0.0};
    int settings_tac_report_scope_{-1};
    geometry::units::LengthUnit display_length_unit_{geometry::units::LengthUnit::Millimeter};
    bool settings_mcp_enabled_choice_{false};
    bool settings_mcp_edit_choice_{false};
    int settings_ai_focus_index_{0}; // Read, edit, copy URL, copy key, reset, cancel, apply

    // Group Isolation Mode Stack
    std::vector<document::ObjectId> isolation_stack_;
    std::string isolation_document_id_;
    uint64_t isolation_epoch_{0}, isolation_page_{0}, isolation_generation_{0};
    bool isolation_reveal_{true}, isolation_frame_{false};
    std::vector<document::ObjectId> isolation_restore_selection_;
    int powerclip_target_pick_{0};
    std::vector<document::ObjectId> powerclip_pick_selection_;
    std::optional<vector::GestureStamp> powerclip_pick_stamp_;
    rendering::RenderViewContext cached_clip_view_;

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
    bool force_live_text_scale_update_{false};
    bool is_creating_shape_{false};
    unsigned shape_drag_modifiers_{0};
    uint64_t pointer_start_revision_{0};
    uint64_t smart_guide_reference_revision_{0};
    document::PageId smart_guide_reference_page_{document::kInvalidPageId};
    bool smart_guide_reference_artboard_mode_{false};
    std::vector<document::ObjectId> smart_guide_reference_selection_roots_;
    document::ObjectId smart_guide_reference_isolation_{document::kInvalidObjectId};
    int smart_guide_reference_active_layer_{-1};
    bool smart_guide_reference_locked_{false};
    bool smart_guide_reference_current_layer_only_{false};
    bool smart_guide_reference_hidden_{false};
    std::vector<vector::precision::snap::LayoutSnapReference> smart_guide_references_;
    std::array<std::optional<vector::precision::snap::LayoutSnapKey>, 2> smart_guide_acquisition_{};
    vector::precision::snap::LayoutSnapSolution smart_guide_solution_;
    std::optional<geometry::Rect2D> smart_guide_moving_bounds_;
    int last_pointer_x_{0};
    int last_pointer_y_{0};
    int drag_start_x_{0};
    int drag_start_y_{0};
    double drag_init_shape_x_{100.0};
    double drag_init_shape_y_{150.0};
    geometry::Point2D pointer_down_world_{};
    geometry::Point2D pointer_current_world_{};
    geometry::Point2D drag_preview_delta_{};
    std::optional<geometry::Point2D> shape_snap_effective_world_;
    std::optional<geometry::Rect2D> drag_preview_bounds_;
    bool is_marquee_{false};
    bool pointer_preserve_aspect_{false};
    int resize_handle_{-1};
    bool is_corner_dragging_in_pick_tool_{false};
    vector::SelectionMode pointer_selection_mode_{vector::SelectionMode::Replace};
    vector::GestureStamp pointer_stamp_;
    std::optional<document::ObjectId> hover_object_;
    EditorStyle default_style_;
    bool palette_stroke_{false};
    std::optional<bool> palette_show_cmyk_;
    std::shared_ptr<const color::IccPreview> color_preview_;
    std::shared_ptr<const color::IccPreview> render_color_preview_;
    std::shared_ptr<const color::ProfileResource> rgb_preview_resource_;
    std::shared_ptr<const color::ProfileResource> cmyk_preview_resource_;
    std::shared_ptr<const color::ProfileResource> display_preview_resource_;
    std::filesystem::path rgb_preview_profile_, cmyk_preview_profile_;
    std::filesystem::path display_preview_profile_path_;
    std::wstring display_preview_device_;
    bool proof_colors_enabled_{false};
    bool gamut_warning_enabled_{false};
    color::IccRenderingIntent proof_intent_{
        color::IccRenderingIntent::RelativeColorimetric};
    bool overprint_preview_enabled_{false};
    bool page_color_preview_available_{true};
    [[nodiscard]] const std::shared_ptr<const color::IccPreview>&
    effective_color_preview_owner() const noexcept {
        return render_color_preview_ ? render_color_preview_ : color_preview_;
    }
    [[nodiscard]] const color::IccPreview* effective_color_preview() const noexcept {
        return effective_color_preview_owner().get();
    }
    void update_color_preview_options() noexcept;
    std::unique_ptr<CanvasFrameCache> canvas_frame_;
    std::string document_path_;
    std::unique_ptr<tools::DrawingToolController> drawing_tool_;
    std::unique_ptr<tools::PenToolController> pen_tool_;
    std::unique_ptr<tools::NodeToolController> node_tool_;
    std::unique_ptr<tools::ContourToolController> contour_tool_;
    std::unique_ptr<tools::GradientToolController> gradient_tool_;
    std::unique_ptr<tools::DimensionToolController> dimension_tool_;
    std::unique_ptr<tools::AnnotationToolController> annotation_tool_;
    HCURSOR annotation_placement_cursor_{nullptr};
#ifdef _WIN32
    std::shared_ptr<ImportJobSession> import_job_;
    std::unique_ptr<ImportPlacementSession> import_placement_;
    bool last_import_succeeded_{false};
    std::shared_ptr<ExportJobSession> export_job_;
    std::shared_ptr<RasterExportJobSession> raster_export_job_;
    std::shared_ptr<RasterProfileConversionJobSession> raster_profile_conversion_job_;
    std::shared_ptr<TestSheetJobSession> test_sheet_job_;
    std::function<std::optional<std::filesystem::path>(bool)>
        raster_profile_file_picker_;
    std::function<std::optional<std::filesystem::path>()>
        raster_bitmap_master_file_picker_;
    std::unique_ptr<raster::RasterEditPreviewService> raster_preview_service_;
    std::unique_ptr<raster::RasterHistogramService> raster_histogram_service_;
    std::unique_ptr<raster::RasterHistogramService> raster_source_histogram_service_;
    std::array<bool, 4> raster_histogram_channels_visible_{{true, true, true, true}};
    document::RasterTonalRange raster_color_balance_active_range_{
        document::RasterTonalRange::Midtones};
    uint8_t raster_adjustment_inspector_tab_{1};
#endif
    uint8_t raster_hsl_active_channel_{0}; // 0: Master, 1..6: R, Y, G, C, B, M
    document::RasterToneChannel raster_levels_active_channel_{
        document::RasterToneChannel::Composite};
    document::RasterToneChannel raster_curves_active_channel_{
        document::RasterToneChannel::Composite};
    int raster_dock_drag_control_{0};
    int properties_dock_scroll_{0};
    int eyedropper_mode_{0}; // 0: Solid Color, 1: Copy Gradient, 2: Copy Object Style
    bool eyedropper_has_picked_sample_{false};
    CanvasColorSample eyedropper_picked_sample_{};
    CanvasColorSample hover_color_sample_{};
    std::string pen_numeric_buffer_;
    std::string pen_numeric_length_;
    std::string pen_numeric_angle_;
    geometry::Point2D pen_last_pointer_world_{};
    int pen_pointer_start_x_{0};
    int pen_pointer_start_y_{0};
    bool pen_pointer_dragged_{false};
    bool pen_close_candidate_{false};
    int last_canvas_pointer_x_{0};
    int last_canvas_pointer_y_{0};
    PointerMoveCoalescer pointer_move_coalescer_;
    GradientRampDragKind gradient_ramp_drag_kind_{GradientRampDragKind::None};
    uint64_t gradient_ramp_drag_stop_id_{0};
    bool gradient_ramp_drag_moved_{false};

    std::string status_msg_{"Ready | Click canvas or tools to start vector editing"};
    bool auto_accept_import_review_{false};

    // Hover Tooltip State
    TooltipInfo tooltip_{};

    // Guideline State
    std::vector<Guideline> guidelines_;
    uint64_t next_guideline_id_{1};
    bool show_guidelines_{true};
    bool is_dragging_guideline_{false};
    GuidelineOrientation drag_guideline_orientation_{GuidelineOrientation::Horizontal};
    double drag_guideline_pos_mm_{0.0};
    int selected_guideline_index_{-1};
    int previous_drawing_tool_{0};

    // Numeric Input State (Direct typing on Property Bar & Modals)
    struct NumericInputContext {
        const void* root_identity{};
        uint64_t page_id{};
        uint64_t document_revision{};
        uint64_t view_generation{};
        int active_tool{};
        std::vector<document::ObjectId> selection_ids;
        document::ObjectId contour_target{document::kInvalidObjectId};
        uint64_t gradient_stop_id{};
        document::ArtboardId artboard_id{document::kInvalidArtboardId};
        bool valid{};
    };
    NumericFieldId active_num_field_{NumericFieldId::None};
    std::string num_input_buffer_;
    NumericInputContext numeric_input_context_{};
    std::vector<document::ObjectId> numeric_object_opacity_targets_;
    std::vector<document::ObjectId> numeric_paint_opacity_targets_;
    std::vector<document::ObjectId> numeric_effect_targets_;
    size_t property_effect_index_{0};
    size_t numeric_effect_index_{0};
    bool ribbon_drop_shadow_mode_{false};
    std::string array_input_buffer_;

    // Quick Shape Dimension input (SketchUp style VCB)
    bool quick_dimension_active_{false};
    document::ObjectId quick_dimension_object_id_{document::kInvalidObjectId};
    std::string quick_dimension_buffer_;
    geometry::Point2D quick_dimension_origin_{};
    double quick_dimension_drag_dir_x_{1.0};
    double quick_dimension_drag_dir_y_{1.0};
    double quick_dimension_initial_w_{0.0};
    double quick_dimension_initial_h_{0.0};

    // Damage Tracker for fast incremental culling
    rendering::DamageTracker damage_tracker_;

    // Canvas Viewport Cache (Fast zero-allocation redraw during interaction)
    bool canvas_cache_valid_{false};
    bool direct_present_frame_{false};
    bool direct_present_active_{false};
    bool canvas_overlay_only_{false};
    bool canvas_overlay_paint_pending_{false};
    std::jthread cut_geometry_worker_;
    std::shared_ptr<CutGeometryJobState> cut_geometry_job_;
    struct PendingCutGeometryProposal {
        CutGeometryProposal proposal;
        std::shared_ptr<CutGeometryJobState> state;
    };
    std::optional<PendingCutGeometryProposal> pending_cut_geometry_proposal_;
    std::uint64_t cut_geometry_generation_{0};
    std::optional<geometry::Point2D> smart_trim_hover_world_;
    std::optional<CutGeometryRequest> smart_trim_click_pending_request_;
    uint64_t cached_revision_{0};
    document::SnapshotPtr cached_canvas_snapshot_;
    std::shared_ptr<const color::IccPreview> cached_canvas_profile_;
    double cached_zoom_{0.0};
    double cached_camera_x_{0.0};
    double cached_camera_y_{0.0};
    int cached_canvas_w_{0};
    int cached_canvas_h_{0};
    ThemeMode cached_theme_{ThemeMode::Light};
    bool cached_artboard_overview_{false};
    document::ArtboardId cached_active_artboard_id_{document::kInvalidArtboardId};
    geometry::Point2D artboard_pointer_start_world_{};
    int artboard_resize_handle_{-1};
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
    HFONT font_title_{nullptr};
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

    std::unique_ptr<CanvasTrace> canvas_trace_;
    std::thread canvas_trace_worker_;
    std::atomic<bool> canvas_trace_stop_{false};
    std::wstring canvas_trace_output_path_;
    std::atomic<bool> canvas_trace_output_ready_{false};
    uint64_t next_trace_event_id_{1};
    uint64_t next_trace_frame_id_{1};
    uint64_t pending_trace_event_id_{0};
    uint64_t active_trace_event_id_{0};
    uint64_t active_trace_frame_id_{0};
    uint64_t last_trace_event_id_{0};
    bool trace_first_frame_recorded_{false};

    DesktopWindow shell_;
};

using NumericFieldId = NativeWindow::NumericFieldId;
} // namespace pikdraw::desktop
