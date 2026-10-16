#pragma once

#include "pikdraw/desktop/runtime/composition_root.hpp"
#include "pikdraw/interop/adapters/svg_importer.hpp"
#include "pikdraw/interop/writers/svg_exporter.hpp"
#include "pikdraw/rendering/frame_target.hpp"
#include "pikdraw/geometry/primitives/transform.hpp"
#include "pikdraw/geometry/operations/boolean.hpp"
#include "pikdraw/geometry/operations/align.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace pikdraw::desktop {

/// High-level user workflows implementing the M1 end-to-end vector pipeline
class DocumentWorkflow {
public:
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
    void new_document(double page_width_mm = 297.0, double page_height_mm = 210.0);

    /// Resize document page size without clearing objects
    void set_page_size(double page_width_mm, double page_height_mm);

    /// Delete all currently selected objects
    bool delete_selected();

    /// Duplicate all currently selected objects with delta offset
    bool duplicate_selected(double offset_mm = 10.0);

    /// Group currently selected objects into a GroupObject
    document::ObjectId group_selected();

    /// Ungroup currently selected GroupObject(s)
    bool ungroup_selected();

    /// Check if object is a GroupObject
    [[nodiscard]] bool is_group(document::ObjectId id) const;

    /// Find parent group ID for a child object (if any)
    [[nodiscard]] std::optional<document::ObjectId> find_parent_group(document::ObjectId child_id) const;

    /// Compute composite bounds of any object (including GroupObject)
    [[nodiscard]] std::optional<geometry::Rect2D> get_object_bounds(document::ObjectId id) const;

    /// Align currently selected objects
    bool align_selected(
        geometry::AlignMode mode,
        geometry::AlignReference reference = geometry::AlignReference::Selection,
        std::optional<document::ObjectId> key_object_id = std::nullopt
    );

    /// Distribute currently selected objects
    bool distribute_selected(
        geometry::DistributeMode mode,
        double specified_spacing_mm = -1.0
    );

    /// Resize a specific object to new absolute width and height (mm)
    bool resize_object(document::ObjectId id, double new_w_mm, double new_h_mm);

    /// Move a specific object to new absolute position (mm)
    bool move_object(document::ObjectId id, double new_x_mm, double new_y_mm);

    /// Change fill color of selected objects
    bool set_selected_fill(const std::string& hex_color);

    /// Change stroke color and width of selected objects
    bool set_selected_stroke(const std::string& hex_color, double stroke_width_mm);

    /// Perform a Solid / Boolean operation on selected objects
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

    /// Trim / Cookie-cutter a bitmap with a vector contour and optionally offset the extracted piece
    bool trim_bitmap_with_vector(
        document::ObjectId bitmap_id,
        document::ObjectId vector_cutter_id,
        double separate_offset_mm = 25.0
    );

    /// Automatic weld of all overlapping shapes (CNC helper)
    bool auto_weld_overlapping();

    /// Save native binary .pikdraw container
    bool save_native(const std::string& file_path);

    /// Reopen native binary .pikdraw container
    bool open_native(const std::string& file_path);

    /// Atomically export the current snapshot as SVG.
    bool save_svg(const std::string& file_path);

    /// Import SVG XML subset
    bool import_svg(const std::string& svg_xml);

    /// Export SVG XML string
    [[nodiscard]] std::string export_svg();

    /// Render current document snapshot into pixel buffer (RGBA 32-bit)
    [[nodiscard]] std::vector<uint8_t> render_snapshot(int32_t width_px, int32_t height_px);
    void render_snapshot(int32_t width_px, int32_t height_px, std::vector<uint8_t>& out_buffer);

    /// Synchronize shell status bar with active document metrics
    void sync_status_bar();

private:
    CompositionRoot& root_;
    interop::SvgImporter svg_importer_;
    interop::SvgExporter svg_exporter_;
};

} // namespace pikdraw::desktop
