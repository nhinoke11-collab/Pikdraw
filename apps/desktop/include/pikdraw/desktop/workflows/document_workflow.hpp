#pragma once

#include "pikdraw/desktop/runtime/composition_root.hpp"
#include "pikdraw/color/preview/icc_preview.hpp"
#include "pikdraw/color/preview/profile_color_transform.hpp"
#include "pikdraw/geometry/units/length_unit.hpp"
#include "pikdraw/interop/writers/svg_exporter.hpp"
#include "pikdraw/rendering/frame_target.hpp"
#include "pikdraw/geometry/primitives/transform.hpp"
#include "pikdraw/geometry/operations/boolean.hpp"
#include "pikdraw/geometry/operations/align.hpp"
#include "pikdraw/document/effects/effect_stack.hpp"
#include "pikdraw/document/commands/import_content_command.hpp"
#include "pikdraw/document/model/clone_utils.hpp"
#include "pikdraw/geometry/primitives/point.hpp"
#include "pikdraw/desktop/workflows/cut_geometry_job.hpp"

#include <functional>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <stop_token>
#include <vector>

namespace pikdraw::vector {
struct TextPropertiesDelta;
}
namespace pikdraw::typography {
enum class ChangeCaseMode;
}

namespace pikdraw::desktop {

enum class ImportFormat : uint8_t { Svg, Dxf };
enum class PdfOpenIntent : uint8_t { EditableObjects, LockedSourceForPrint };
enum class ImportProgressStage : uint8_t {
    Snapshotting, StartingWorker, Reading, Validating, WaitingForMemory, Complete
};

enum class SelectSameCriterion : uint8_t {
    FillColor,
    StrokeColor,
    FillAndStroke,
    AnyPaintColor,
    ColorSpace,
    CmykOnly,
    RgbOnly,
    SpotOnly
};

struct ImportProgress {
    ImportProgressStage stage{ImportProgressStage::Snapshotting};
    size_t records_received{0};
    uint64_t source_bytes_copied{0};
    uint64_t source_bytes_total{0};
};

struct ImportIssueSummary {
    enum class Severity : uint8_t { Info, Warning, Unsupported };

    Severity severity{Severity::Warning};
    std::string source_locator;
    std::string feature_code;
    std::string diagnostic_code;
};

struct ImportedContentProposal {
    std::string format_name;
    std::string profile_id;
    uint64_t source_bytes{0};
    double source_page_width_mm{0};
    double source_page_height_mm{0};
    std::string coverage_status;
    std::string appearance_status;
    std::string editability_status;
    std::string production_status;
    std::vector<ImportIssueSummary> issues;
    // Populated by page-set profiles; legacy single-page profiles keep using
    // the flat layer/object fields below.
    std::vector<document::ImportedPageContent> pages;
    document::DocumentContentMode content_mode{document::DocumentContentMode::Editable};
    std::shared_ptr<const document::PdfSourceAsset> pdf_source_asset;
    std::vector<document::Layer> layers;
    std::vector<document::ObjectVariant> objects;
};

struct ImportWorkerResult {
    bool success{false};
    bool cancelled{false};
    std::string error_message;
    ImportedContentProposal proposal;
};

[[nodiscard]] ImportWorkerResult run_import_worker_job(
    const std::wstring& source_path,
    ImportFormat format,
    const document::AsyncProposalMetadata& job,
    std::stop_token cancellation,
    const std::function<void(ImportProgress)>& on_progress = {},
    PdfOpenIntent pdf_open_intent = PdfOpenIntent::EditableObjects,
    uint32_t raster_frame_index = 0);

/// High-level user workflows implementing the M1 end-to-end vector pipeline
class DocumentWorkflow {
public:
    enum class PastePlacement : uint8_t {
        CenterOfViewport,
        InPlace,
        InFront,
        InBack,
        Offset
    };

    struct ArraySession {
        bool active{false};
        std::vector<document::ObjectId> source_ids;
        geometry::Point2D base_delta{0.0, 0.0};
        std::vector<document::ObjectId> current_array_ids;
        bool is_multiplier{true};
        int current_count{1};
    };

    enum class EffectDraftField : uint8_t {
        None,
        SigmaX,
        SigmaY,
        OffsetX,
        OffsetY,
        Opacity,
    };

    enum class SelectedEffectActivation : uint8_t {
        Unavailable,
        Existing,
        Added,
    };

    struct EffectDraftView {
        document::EffectParameters parameters;
        bool preview_enabled{false};
        bool editing_existing{false};
        EffectDraftField active_field{EffectDraftField::None};
        std::string numeric_buffer;
    };

    explicit DocumentWorkflow(CompositionRoot& root);

    /// Create and add a rectangle in mm units
    document::ObjectId add_rectangle(
        double x_mm, double y_mm, double width_mm, double height_mm,
        const std::string& fill_hex = "#00f2c3",
        const std::string& stroke_hex = "#ffffff",
        double stroke_width_mm = 1.0
    );

    /// Create and add an ellipse in mm units
    document::ObjectId add_ellipse(
        double cx_mm, double cy_mm, double rx_mm, double ry_mm,
        const std::string& fill_hex = "#00f2c3",
        const std::string& stroke_hex = "#ffffff",
        double stroke_width_mm = 1.0
    );

    /// Create and add Vietnamese Unicode text
    document::ObjectId add_vietnamese_text(
        double x_mm, double y_mm,
        const std::string& utf8_text,
        const std::string& font_family = "Plus Jakarta Sans",
        double font_size_pt = 24.0,
        const std::string& fill_hex = "#ffffff"
    );

    /// Update text object content atomically with undo/redo
    bool update_text_content(document::ObjectId text_id, const std::string& new_text);

    /// Update text object typography styles (font family, size, bold, italic, underline, alignment)
    bool update_text_style(
        document::ObjectId text_id,
        const std::string& font_family,
        double font_size_pt,
        bool bold,
        bool italic,
        bool underline,
        typography::TextAlignment alignment
    );

    /// Convert text object to vector path outlines (Curves / CorelDRAW Ctrl+Q)
    bool convert_text_to_curves(document::ObjectId text_id);

    /// Create a professional vector table object with rows and columns (CorelDRAW Table Tool)
    document::ObjectId create_table(
        double x_mm, double y_mm, double width_mm, double height_mm,
        int rows = 3, int cols = 3,
        const std::string& border_hex = "#0ea5e9",
        const std::string& fill_hex = "#f8fafc",
        double border_w_mm = 0.8
    );

    /// Select object by ID
    void select_object(document::ObjectId id, bool toggle = false);

    /// Transform object by delta in mm
    bool transform_object(document::ObjectId id, double dx_mm, double dy_mm);

    /// Undo last transaction
    bool undo();

    /// Redo last transaction
    bool redo();

    /// Reset to an empty page and begin a new document epoch. New documents default to A4 landscape.
    void new_document(double page_width_mm = 297.0, double page_height_mm = 210.0,
        color::DocumentColorMode mode = color::DocumentColorMode::Rgb, int32_t dpi = 0,
        geometry::units::LengthUnit display_unit = geometry::units::LengthUnit::Millimeter);
    bool set_page_display_length_unit(document::PageId page_id,
        geometry::units::LengthUnit unit);
    bool convert_document_color_mode(color::DocumentColorMode mode,
        std::shared_ptr<const color::IccPreview> icc = {});
    bool set_document_raster_dpi(int32_t dpi);
    bool set_page_color_mode(document::PageId page_id, color::DocumentColorMode mode,
        std::shared_ptr<const color::IccPreview> icc = {});
    bool convert_page_color_profile(document::PageId page_id,
        std::shared_ptr<const color::ProfileColorTransform> transform);
    bool assign_selected_objects_color_profile(
        std::shared_ptr<const color::ProfileResource> resource);
    bool convert_selected_objects_color_profile(document::PageId page_id,
        std::shared_ptr<const color::ProfileColorTransform> transform);
    bool convert_selected_objects_color_profile(document::PageId page_id,
        std::shared_ptr<const color::ProfileResource> destination,
        std::vector<std::shared_ptr<const color::ProfileColorTransform>> transforms);
    bool convert_document_color_profile(
        std::shared_ptr<const color::ProfileResource> destination,
        std::vector<std::shared_ptr<const color::ProfileColorTransform>> transforms);
    bool set_page_raster_dpi(document::PageId page_id, int32_t dpi);
    bool convert_selected_objects_color_mode(color::DocumentColorMode mode,
        std::shared_ptr<const color::IccPreview> icc = {});
    bool convert_active_page_color_mode(color::DocumentColorMode mode,
        std::shared_ptr<const color::IccPreview> icc = {});
    [[nodiscard]] const std::optional<color::ColorConversionSummary>&
    last_color_conversion_summary() const noexcept { return last_color_conversion_summary_; }
    [[nodiscard]] const std::string& last_color_conversion_error() const noexcept {
        return last_color_conversion_error_;
    }

    /// Find objects matching color / Select Same
    [[nodiscard]] std::vector<document::ObjectId> find_objects_matching_color(
        const document::ObjectId* reference_id,
        const color::ColorValue* reference_color,
        SelectSameCriterion criterion) const;
    bool select_same(SelectSameCriterion criterion);
    bool select_by_color(const color::ColorValue& color,
        SelectSameCriterion criterion = SelectSameCriterion::FillColor);

    /// Resize document page size without clearing objects
    void set_page_size(double page_width_mm, double page_height_mm);

    /// Delete all currently selected objects
    bool delete_selected();

    /// Copy currently selected objects to system clipboard (supports native snapshot + SVG + text)
    bool copy_selected(void* hwnd = nullptr);

    /// Cut currently selected objects to system clipboard
    bool cut_selected(void* hwnd = nullptr);

    /// Paste from system clipboard
    bool paste(void* hwnd = nullptr, PastePlacement placement = PastePlacement::CenterOfViewport, double offset_x = 5.0, double offset_y = 5.0);

    /// Paste in place (identical coordinates)
    bool paste_in_place(void* hwnd = nullptr);

    /// Paste directly in front of source z-order
    bool paste_in_front(void* hwnd = nullptr);

    /// Paste directly behind source z-order
    bool paste_in_back(void* hwnd = nullptr);

    /// Duplicate all currently selected objects with (offset_x, offset_y) delta in mm
    bool duplicate_selected(double offset_x, double offset_y);

    /// Backward compatibility overload: duplicate with uniform offset_mm
    bool duplicate_selected(double offset_mm = 10.0);

    /// Duplicate selected objects in place at origin (0, 0)
    bool duplicate_at_origin();

    /// Check if dynamic array session is currently active
    [[nodiscard]] bool is_array_session_active() const noexcept;

    /// Get current array session information
    [[nodiscard]] const ArraySession& array_session() const noexcept;

    /// Start an array session after duplicate or Alt-drag
    void start_array_session(
        std::vector<document::ObjectId> source_ids,
        const geometry::Point2D& base_delta,
        std::vector<document::ObjectId> first_copy_ids
    );

    /// Apply SketchUp-style multiplier (*N or xN) to create N copies spaced by base_delta
    bool apply_array_multiplier(int count);

    /// Apply SketchUp-style divider (/N) to divide total base_delta into N equal divisions
    bool apply_array_divider(int divisions);

    /// End/commit current array session
    void end_array_session() noexcept;

    /// Group currently selected objects into a GroupObject
    [[nodiscard]] bool can_group_selected() const;
    document::ObjectId group_selected();

    /// Ungroup currently selected GroupObject(s)
    bool ungroup_selected();

    /// Check if object is a GroupObject
    [[nodiscard]] bool is_group(document::ObjectId id) const;

    /// PowerClip / Clipping Mask methods
    [[nodiscard]] bool can_make_clipping_mask_selected() const;
    document::ObjectId make_clipping_mask_selected(bool preserve_appearance = true);
    bool release_clipping_mask_selected();
    bool extract_powerclip_contents(document::ObjectId frame_id);
    document::ObjectId create_empty_powerclip(document::ObjectId frame_id);
    bool copy_powerclip_contents(document::ObjectId source_id, document::ObjectId target_id);
    bool set_powerclip_preserve_appearance(document::ObjectId frame_id, bool preserve);
    [[nodiscard]] bool is_clipping_container(document::ObjectId id) const;
    document::ObjectId place_inside_powerclip(
        document::ObjectId content_id,
        document::ObjectId frame_id,
        document::ClipContentFitMode fit_mode = document::ClipContentFitMode::CurrentPosition);
    bool fit_powerclip_contents(document::ObjectId group_id, document::ClipContentFitMode mode);
    bool set_powerclip_lock_contents(document::ObjectId group_id, bool locked);

    /// Find parent group ID for a child object (if any)
    [[nodiscard]] std::optional<document::ObjectId> find_parent_group(document::ObjectId child_id) const;

    /// Compute composite bounds of any object (including GroupObject)
    [[nodiscard]] std::optional<geometry::Rect2D> get_object_bounds(document::ObjectId id) const;

    /// Align currently selected objects
    [[nodiscard]] bool can_align_selected(
        geometry::AlignMode mode,
        geometry::AlignReference reference = geometry::AlignReference::Selection,
        std::optional<document::ObjectId> key_object_id = std::nullopt
    ) const;
    bool align_selected(
        geometry::AlignMode mode,
        geometry::AlignReference reference = geometry::AlignReference::Selection,
        std::optional<document::ObjectId> key_object_id = std::nullopt
    );

    /// Distribute currently selected objects
    [[nodiscard]] bool can_distribute_selected(
        geometry::DistributeMode mode,
        double specified_spacing_mm = -1.0
    ) const;
    bool distribute_selected(
        geometry::DistributeMode mode,
        double specified_spacing_mm = -1.0
    );

    /// Resize a specific object to new absolute width and height (mm)
    bool resize_object(document::ObjectId id, double new_w_mm, double new_h_mm);

    /// Move a specific object to new absolute position (mm)
    bool move_object(document::ObjectId id, double new_x_mm, double new_y_mm);

    /// Flip currently selected objects horizontally (around selection center)
    bool flip_selected_horizontal();

    /// Flip currently selected objects vertically (around selection center)
    bool flip_selected_vertical();

    /// Rotate currently selected objects by angle in degrees around pivot (or selection center if nullopt)
    bool rotate_selected_degrees(double degrees, std::optional<geometry::Point2D> pivot = std::nullopt);

    /// Rotate currently selected objects 90 degrees clockwise (around pivot or selection center)
    bool rotate_selected_90_cw(std::optional<geometry::Point2D> pivot = std::nullopt);

    /// Rotate currently selected objects 90 degrees counter-clockwise (around pivot or selection center)
    bool rotate_selected_90_ccw(std::optional<geometry::Point2D> pivot = std::nullopt);

    /// Rotate currently selected objects 180 degrees (around pivot or selection center)
    bool rotate_selected_180(std::optional<geometry::Point2D> pivot = std::nullopt);

    /// Apply arbitrary 2D transform to currently selected objects
    bool transform_selected(const geometry::Transform2D& delta, const std::string& command_name = "Transform Selected");

    /// Change fill color of selected objects
    bool set_selected_fill(const std::string& hex_color);

    /// Change stroke color and width of selected objects
    bool set_selected_stroke(const std::string& hex_color, double stroke_width_mm);

    /// Whether the complete current selection supports atomic object-opacity editing.
    [[nodiscard]] bool can_set_selected_object_opacity() const;
    /// Apply normalized [0,1] object opacity to every selected Path/Text in one command.
    bool set_selected_object_opacity(double normalized_opacity);
    /// Whether every selected unlocked Path/Text supports fill-paint opacity editing.
    [[nodiscard]] bool can_set_selected_fill_opacity() const;
    /// Apply normalized [0,1] fill opacity atomically without changing source color alpha.
    bool set_selected_fill_opacity(double normalized_opacity);
    /// Whether every selected target is an unlocked Path with editable stroke paint.
    [[nodiscard]] bool can_set_selected_stroke_opacity() const;
    /// Apply normalized [0,1] stroke opacity atomically without changing source color alpha.
    bool set_selected_stroke_opacity(double normalized_opacity);

    /// Whether the complete selection can receive atomic Path/Text Appearance updates.
    [[nodiscard]] bool can_edit_selected_appearance() const;
    [[nodiscard]] bool can_add_selected_effect() const;
    [[nodiscard]] bool can_add_selected_effect(const document::EffectParameters& parameters) const;
    [[nodiscard]] std::optional<size_t> selected_effect_index(
        const document::EffectParameters& parameters) const;
    [[nodiscard]] bool can_activate_selected_effect(
        const document::EffectParameters& parameters) const;
    SelectedEffectActivation activate_selected_effect(
        document::EffectParameters parameters, size_t& effect_index);
    [[nodiscard]] bool has_effect_draft() const noexcept;
    [[nodiscard]] std::optional<EffectDraftView> effect_draft() const;
    [[nodiscard]] bool effect_draft_is_current() const;
    /// Discard a draft when the document, revision, or full selection no longer matches.
    bool cancel_stale_effect_draft();
    bool begin_selected_effect_draft(document::EffectParameters parameters);
    bool begin_selected_effect_edit_draft(size_t effect_index);
    bool set_effect_draft_parameters(document::EffectParameters parameters);
    bool set_effect_draft_preview(bool enabled);
    bool apply_effect_draft();
    void cancel_effect_draft() noexcept;
    bool start_effect_draft_numeric_input(EffectDraftField field);
    bool focus_next_effect_draft_numeric_field();
    [[nodiscard]] bool effect_draft_numeric_input_active() const noexcept;
    bool input_effect_draft_character(char character);
    bool backspace_effect_draft_numeric_input();
    bool commit_effect_draft_numeric_input();
    /// Returns true when the key was handled by the active draft session.
    bool handle_effect_draft_keydown(uint32_t key);
    /// Returns the immutable candidate snapshot only while Preview is enabled and current.
    [[nodiscard]] document::SnapshotPtr snapshot_for_canvas();
    bool preview_text_edit(const document::TextObject& text, document::SnapshotPtr base,
                           bool existing, document::ObjectId parent = document::kInvalidObjectId);
    bool commit_text_edit(const document::TextObject& text, document::SnapshotPtr base,
                          bool existing, document::ObjectId parent = document::kInvalidObjectId);
    void clear_text_preview() noexcept { text_preview_.reset(); text_preview_base_.reset(); }
    bool style_selected_text(const vector::TextPropertiesDelta& delta);
    bool set_selected_text_font(const vector::TextPropertiesDelta& delta);
    bool set_selected_text_layout(std::optional<double> frame_width_mm,
                                  std::optional<double> line_spacing_ratio);
    bool update_selected_text_properties(const vector::TextPropertiesDelta& delta);
    bool change_case_selected_text(typography::ChangeCaseMode mode);
    bool link_selected_text_frames();
    bool unlink_selected_text_frame();
    bool combine_selected_text_objects();
    bool split_selected_text_lines();
    bool attach_selected_text_to_path();
    bool detach_selected_text_from_path();
    bool set_selected_text_path_offset(double offset_mm);
    bool toggle_selected_text_path_reverse();
    bool set_selected_text_path_baseline_offset(double offset_mm);
    [[nodiscard]] bool can_clear_selected_effects() const;
    [[nodiscard]] bool can_edit_selected_effect(size_t effect_index) const;
    bool set_selected_blend_mode(document::BlendMode mode);
    bool add_selected_effect(document::EffectParameters parameters);
    bool clear_selected_effects();
    [[nodiscard]] bool can_clear_all_effects() const;
    bool clear_all_effects();
    bool remove_selected_effect(size_t effect_index);
    [[nodiscard]] bool can_move_selected_effect(size_t effect_index,
                                                int direction) const;
    bool move_selected_effect(size_t effect_index, int direction);
    bool set_selected_effect_enabled(size_t effect_index, bool enabled);
    bool set_selected_effect_sigma(size_t effect_index, bool x_axis, double sigma_mm);
    bool set_selected_effect_offset(size_t effect_index, bool x_axis, double offset_mm);
    bool set_selected_effect_parameters(size_t effect_index,
                                        document::EffectParameters parameters);
    [[nodiscard]] bool can_set_selected_effect_blend_mode(
        size_t effect_index, document::BlendMode mode) const;
    bool set_selected_effect_blend_mode(size_t effect_index,
                                        document::BlendMode mode);
    bool set_selected_effect_opacity(size_t effect_index, double normalized_opacity);
    bool set_selected_effect_color(size_t effect_index, color::ColorValue color);

    /// Perform a Solid / Boolean operation on selected objects
    [[nodiscard]] bool can_boolean_operation_selected(geometry::BooleanOp op) const;
    bool boolean_operation_selected(
        geometry::BooleanOp op,
        bool keep_source = false,
        bool keep_target = false
    );

    /// Add procedural raster bitmap photo asset
    document::ObjectId add_bitmap_image(
        double x_mm, double y_mm, double width_mm, double height_mm,
        const std::string& name = "Bitmap Photo"
    );

    /// Split objects intersecting a knife cut line (supports vectors and bitmaps, optional separation offset)
    bool split_objects_by_line(
        const geometry::Point2D& p1,
        const geometry::Point2D& p2,
        double separate_offset_mm = 0.0
    );

    /// Knife cut along captured freehand points in world millimeters.
    bool split_objects_by_polyline(
        const std::vector<geometry::Point2D>& points,
        double gap_mm = 0.0,
        double separate_offset_mm = 0.0
    );

    /// Knife cut on selected or candidate objects using true geometric path bisecting
    bool cut_objects_by_line(
        const geometry::Point2D& p1,
        const geometry::Point2D& p2,
        double separate_offset_mm = 0.0,
        double gap_mm = 0.0
    );

    bool cut_objects_by_polyline(
        const std::vector<geometry::Point2D>& points,
        double separate_offset_mm = 0.0,
        double gap_mm = 0.0
    );
    bool apply_cut_geometry_proposal(CutGeometryProposal proposal,
                                     std::string* error = nullptr);

    /// Vector Eraser sweeping along stroke
    bool erase_objects_by_stroke(
        const std::vector<geometry::Point2D>& stroke,
        double diameter_mm = 2.0,
        bool round_nib = true
    );

    /// Split the editable path nearest to a screen-space hit expressed in world mm.
    bool scissors_split_at_point(const geometry::Point2D& point_world,
                                 double hit_tolerance_mm);

    /// Remove the intersection-bounded editable stroke interval under a hit.
    /// Ambiguous/overlapping intersections fail without mutating the document.
    bool smart_trim_at_point(const geometry::Point2D& point_world,
                             double hit_tolerance_mm,
                             std::string* error = nullptr);

    /// Resolve a bounded face formed by visible editable closed filled paths.
    bool resolve_smart_fill_face(const geometry::Point2D& point_world,
                                 geometry::Path2D& face,
                                 std::vector<document::ObjectId>& source_ids,
                                 document::ObjectId& style_source_id,
                                 std::string* error = nullptr);
    bool smart_fill_at_point(const geometry::Point2D& point_world,
                             std::string* error = nullptr);
    bool shape_builder_apply_faces(const std::vector<geometry::Point2D>& seed_points,
                                   bool erase_faces,
                                   std::string* error = nullptr);

    /// Trim target object by subtracting source objects
    bool trim_objects_source_target(
        document::ObjectId target_id,
        const std::vector<document::ObjectId>& source_ids,
        bool keep_sources = true
    );

    /// Trim / Cookie-cutter a bitmap with a vector contour and optionally offset the extracted piece
    bool create_bitmap_crop_from_vector_bounds(
        document::ObjectId bitmap_id,
        document::ObjectId vector_cutter_id,
        double separate_offset_mm = 25.0
    );

    /// Automatic weld of all overlapping shapes (CNC helper)
    bool auto_weld_overlapping();

    /// Save native binary .pr container
    bool save_native(const std::string& file_path);

    /// Reopen native binary .pr container
    bool open_native(const std::string& file_path);

    /// Atomically export the current snapshot as SVG.
    bool save_svg(const std::string& file_path);

    /// Import SVG XML subset
    bool import_svg(const std::string& svg_xml);

    /// Capture, cancel and atomically commit a background SVG/DXF import job.
    [[nodiscard]] document::AsyncProposalMetadata begin_import_job(uint64_t job_id);
    void cancel_import_job(uint64_t job_id);
    [[nodiscard]] document::ProposalStatus commit_import_proposal(
        document::AsyncProposalMetadata metadata, ImportedContentProposal proposal,
        bool resize_page_to_source = false);

    /// Export SVG XML string
    [[nodiscard]] std::string export_svg();

    /// Render current document snapshot into pixel buffer (RGBA 32-bit)
    [[nodiscard]] std::vector<uint8_t> render_snapshot(int32_t width_px, int32_t height_px);
    void render_snapshot(int32_t width_px, int32_t height_px, std::vector<uint8_t>& out_buffer);

    /// Synchronize shell status bar with active document metrics
    void sync_status_bar();

    [[nodiscard]] size_t active_layer_index() const noexcept {
        const auto snapshot = root_.document().current_snapshot();
        if (!snapshot || snapshot->page().layers.empty()) return 0;
        return std::min(active_layer_index_, snapshot->page().layers.size() - 1);
    }
    void set_active_layer_index(size_t index) noexcept {
        const auto snapshot = root_.document().current_snapshot();
        active_layer_index_ = !snapshot || snapshot->page().layers.empty()
            ? 0 : std::min(index, snapshot->page().layers.size() - 1);
    }

private:
    struct EffectDraftState {
        document::EffectParameters parameters;
        bool preview_enabled{false};
        std::string document_id;
        uint64_t epoch{0};
        uint64_t revision{0};
        std::vector<document::ObjectId> target_ids;
        std::optional<size_t> edit_effect_index;
        document::SnapshotPtr preview_snapshot;
        EffectDraftField active_field{EffectDraftField::None};
        std::string numeric_buffer;
    };

    bool update_selected_appearance(const std::function<bool(document::Appearance &)> &update);
    bool rebuild_effect_draft_preview();
    bool set_effect_draft_numeric_value(EffectDraftField field, double value);
    std::vector<EffectDraftField> effect_draft_numeric_fields() const;
    size_t active_layer_index_{0};
    CompositionRoot& root_;
    interop::SvgExporter svg_exporter_;
    std::optional<EffectDraftState> effect_draft_;
    document::SnapshotPtr text_preview_, text_preview_base_;
    ArraySession array_session_{};
    std::optional<color::ColorConversionSummary> last_color_conversion_summary_;
    std::string last_color_conversion_error_;
};

} // namespace pikdraw::desktop
