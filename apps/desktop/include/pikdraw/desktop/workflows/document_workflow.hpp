#pragma once

#include "pikdraw/desktop/runtime/composition_root.hpp"
#include "pikdraw/interop/adapters/svg_importer.hpp"
#include "pikdraw/interop/writers/svg_exporter.hpp"
#include "pikdraw/rendering/frame_target.hpp"
#include "pikdraw/geometry/primitives/transform.hpp"

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

    /// Synchronize shell status bar with active document metrics
    void sync_status_bar();

private:
    CompositionRoot& root_;
    interop::SvgImporter svg_importer_;
    interop::SvgExporter svg_exporter_;
};

} // namespace pikdraw::desktop
