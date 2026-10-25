#include "pikdraw/desktop/workflows/document_workflow.hpp"
#include "document_workflow_internal.hpp"
#include "pikdraw/vector/tools/shape_tools.hpp"
#include "pikdraw/vector/gestures/text_tool.hpp"
#include "pikdraw/typography/layout/glyph_outline.hpp"
#include "pikdraw/typography/layout/text_layout.hpp"
#include "pikdraw/typography/layout/text_on_path.hpp"
#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/commands/page_commands.hpp"
#include "pikdraw/document/commands/unit_commands.hpp"
#include "pikdraw/document/commands/appearance_commands.hpp"
#include "pikdraw/document/commands/transaction_writer.hpp"
#include "pikdraw/color/values/color_value.hpp"
#include "pikdraw/interop/adapters/svg_importer.hpp"
#include "pikdraw/document/storage/native_format.hpp"
#include "pikdraw/document/model/clone_utils.hpp"
#include "../clipboard/native_clipboard.hpp"
#include "../utf8_path.hpp"

#include <chrono>
#include <algorithm>
#include <cstddef>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <tuple>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace pikdraw::desktop {
using namespace detail;

DocumentWorkflow::DocumentWorkflow(CompositionRoot& root)
    : root_(root) {}

document::ObjectId DocumentWorkflow::add_rectangle(
    double x_mm, double y_mm, double width_mm, double height_mm,
    const std::string& fill_hex,
    const std::string& stroke_hex,
    double stroke_width_mm
) {
    width_mm = std::clamp(width_mm, 0.1, geometry::kMaxObjectDimensionMm);
    height_mm = std::clamp(height_mm, 0.1, geometry::kMaxObjectDimensionMm);
    auto rect_obj = vector::ShapeFactory::create_rectangle(
        geometry::Rect2D{x_mm, y_mm, width_mm, height_mm},
        parse_hex_color(fill_hex),
        parse_hex_color(stroke_hex),
        stroke_width_mm
    );

    const auto id = rect_obj.id;
    root_.document().execute_command(std::make_unique<document::AddObjectCommand>(std::move(rect_obj), active_layer_index()));
    sync_status_bar();
    return id;
}

document::ObjectId DocumentWorkflow::add_ellipse(
    double cx_mm, double cy_mm, double rx_mm, double ry_mm,
    const std::string& fill_hex,
    const std::string& stroke_hex,
    double stroke_width_mm
) {
    rx_mm = std::clamp(rx_mm, 0.1, geometry::kMaxObjectDimensionMm * 0.5);
    ry_mm = std::clamp(ry_mm, 0.1, geometry::kMaxObjectDimensionMm * 0.5);
    auto ellipse_obj = vector::ShapeFactory::create_ellipse(
        geometry::Point2D{cx_mm, cy_mm},
        rx_mm, ry_mm,
        parse_hex_color(fill_hex),
        parse_hex_color(stroke_hex),
        stroke_width_mm
    );

    const auto id = ellipse_obj.id;
    root_.document().execute_command(std::make_unique<document::AddObjectCommand>(std::move(ellipse_obj), active_layer_index()));
    sync_status_bar();
    return id;
}

document::ObjectId DocumentWorkflow::add_vietnamese_text(
    double x_mm, double y_mm,
    const std::string& utf8_text,
    const std::string& font_family,
    double font_size_pt,
    const std::string& fill_hex
) {
    document::TextObject text_obj;
    text_obj.id = document::generate_object_id();
    text_obj.name = "Văn bản: " + detail::utf8_truncate_codepoints(utf8_text, 20);
    text_obj.text = utf8_text;
    text_obj.font = typography::FontIdentity{font_family, font_family + "-Regular", 400, false, font_size_pt};
    text_obj.fill = parse_hex_color(fill_hex);
    text_obj.position = geometry::Point2D{x_mm, y_mm};

    const auto id = text_obj.id;
    root_.document().execute_command(std::make_unique<document::AddObjectCommand>(std::move(text_obj), active_layer_index()));
    sync_status_bar();
    return id;
}

bool DocumentWorkflow::update_text_content(document::ObjectId text_id, const std::string& new_text) {
    auto cmd = std::make_unique<vector::UpdateTextCommand>(text_id, new_text);
    auto res = root_.document().execute_command(std::move(cmd));
    sync_status_bar();
    return res.success;
}

bool DocumentWorkflow::update_text_style(
    document::ObjectId text_id,
    const std::string& font_family,
    double font_size_pt,
    bool bold,
    bool italic,
    bool underline,
    typography::TextAlignment alignment
) {
    vector::TextPropertiesDelta delta;
    if (!font_family.empty()) delta.family = font_family;
    delta.size_pt = font_size_pt;
    delta.weight = bold ? 700 : 400;
    delta.italic = italic;
    delta.underline = underline;
    delta.alignment = alignment;
    auto cmd = std::make_unique<vector::UpdateTextPropertiesCommand>(text_id, std::move(delta));


    auto res = root_.document().execute_command(std::move(cmd));
    sync_status_bar();
    return res.success;
}

bool DocumentWorkflow::convert_text_to_curves(document::ObjectId text_id) {
    auto& doc = root_.document();
    const auto snapshot = doc.current_snapshot();
    auto opt = snapshot->objects().get(text_id);
    if (!opt || !std::holds_alternative<document::TextObject>(*opt)) {
        return false;
    }
    const auto& text_obj = std::get<document::TextObject>(*opt);
    geometry::Path2D outline_path;
    if (text_obj.is_text_on_path()) {
        auto path_opt = snapshot->objects().get(text_obj.path_binding_id);
        if (path_opt && std::holds_alternative<document::PathObject>(*path_opt)) {
            const auto& host_path = std::get<document::PathObject>(*path_opt);
            typography::TextOnPathOptions top_opts;
            top_opts.start_offset_mm = text_obj.path_offset_mm;
            top_opts.baseline_offset_mm = text_obj.path_baseline_offset_mm;
            top_opts.reversed = text_obj.path_reversed;
            top_opts.alignment = text_obj.alignment;
            auto top_res = typography::layout_text_on_path(host_path.path, text_obj.text, text_obj.font, top_opts);
            outline_path = top_res.combined_outlines.transformed(host_path.transform * geometry::Transform2D::scale(1, -1));
        }
    }
    if (outline_path.empty()) {
        auto layout = text_obj.layout();
        outline_path = typography::text_to_path_outlines(layout, text_obj.underline, text_obj.strikethrough);
        if (outline_path.empty()) return false;
        geometry::Transform2D pos_tx = geometry::Transform2D::translation(text_obj.position.x, text_obj.position.y);
        outline_path = outline_path.transformed(text_obj.transform * pos_tx * geometry::Transform2D::scale(1, -1));
    }

    document::PathObject path_obj;
    path_obj.id = document::generate_object_id();
    path_obj.name = "Đường cong: " + (text_obj.text.empty() ? "Curves" : detail::utf8_truncate_codepoints(text_obj.text, 16));
    path_obj.path = std::move(outline_path);
    path_obj.fill = text_obj.fill;
    path_obj.appearance = text_obj.appearance;
    path_obj.stroke_width_mm = 0.0;
    path_obj.has_fill = true;
    path_obj.has_stroke = false;
    path_obj.visible = text_obj.visible;
    path_obj.locked = text_obj.locked;
    auto ep = document::editable_path_from_path2d(path_obj.path, nullptr);
    if (ep) {
        path_obj.set_authoring_source(std::move(*ep));
    }

    auto composite = std::make_unique<document::CompositeCommand>("Convert Text to Curves");
    composite->add_command(std::make_unique<document::RemoveObjectCommand>(text_id));
    const auto new_id = path_obj.id;
    composite->add_command(std::make_unique<document::AddObjectCommand>(std::move(path_obj), active_layer_index()));
    auto res = doc.execute_command(std::move(composite));
    if (res.success) {
        root_.vector_controller().selection().select(new_id);
        sync_status_bar();
        return true;
    }
    return false;
}

document::ObjectId DocumentWorkflow::create_table(
    double x_mm, double y_mm, double width_mm, double height_mm,
    int rows, int cols,
    const std::string& border_hex,
    const std::string& fill_hex,
    double border_w_mm
) {
    if (rows <= 0) rows = 3;
    if (cols <= 0) cols = 3;
    if (width_mm <= 10.0) width_mm = 120.0;
    if (height_mm <= 10.0) height_mm = 60.0;

    auto& doc = root_.document();
    auto composite = std::make_unique<document::CompositeCommand>("Create Table");

    double col_w = width_mm / cols;
    double row_h = height_mm / rows;

    std::vector<document::ObjectId> cell_ids;
    color::RGBColor border_col = parse_hex_color(border_hex);
    color::RGBColor base_fill = parse_hex_color(fill_hex);
    color::RGBColor header_fill{0.90, 0.94, 0.98, 1.0}; // Soft blue header fill
    color::RGBColor alt_fill{0.98, 0.99, 1.0, 1.0};

    const std::vector<std::string> default_headers = {
        "STT", "Hạng mục (Item)", "Số lượng (Qty)", "Đơn giá (Price)", "Thành tiền", "Ghi chú"
    };

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            double cx = x_mm + c * col_w;
            double cy = y_mm + r * row_h;

            // 1. Cell background & border rectangle
            document::PathObject cell_rect;
            cell_rect.id = document::generate_object_id();
            cell_rect.name = "Ô R" + std::to_string(r + 1) + "C" + std::to_string(c + 1);
            cell_rect.path = geometry::Path2D::make_rect(cx, cy, col_w, row_h);
            cell_rect.fill = (r == 0) ? header_fill : ((r % 2 == 1) ? base_fill : alt_fill);
            cell_rect.stroke = border_col;
            cell_rect.stroke_width_mm = border_w_mm;
            cell_rect.has_fill = true;
            cell_rect.has_stroke = true;

            const auto rect_id = cell_rect.id;
            cell_ids.push_back(rect_id);
            composite->add_command(std::make_unique<document::AddObjectCommand>(std::move(cell_rect), active_layer_index()));

            // 2. Cell text
            document::TextObject cell_text;
            cell_text.id = document::generate_object_id();
            cell_text.name = "Chữ R" + std::to_string(r + 1) + "C" + std::to_string(c + 1);
            if (r == 0) {
                cell_text.text = (c < static_cast<int>(default_headers.size())) ? default_headers[c] : ("Cột " + std::to_string(c + 1));
                cell_text.font = typography::FontIdentity{"Segoe UI", "SegoeUI-Bold", 700, false, 11.0};
                cell_text.fill = color::RGBColor{0.05, 0.2, 0.4, 1.0};
            } else {
                cell_text.text = (c == 0) ? std::to_string(r) : ("Dữ liệu R" + std::to_string(r) + "C" + std::to_string(c + 1));
                cell_text.font = typography::FontIdentity{"Segoe UI", "SegoeUI-Regular", 400, false, 10.0};
                cell_text.fill = color::RGBColor{0.15, 0.2, 0.25, 1.0};
            }
            cell_text.position = geometry::Point2D{cx + 2.5, cy + 2.5};
            cell_text.alignment = (r == 0 || c == 0) ? typography::TextAlignment::Center : typography::TextAlignment::Left;

            const auto text_id = cell_text.id;
            cell_ids.push_back(text_id);
            composite->add_command(std::make_unique<document::AddObjectCommand>(std::move(cell_text), active_layer_index()));
        }
    }

    // 3. Wrap entire table into a GroupObject
    document::GroupObject table_grp;
    table_grp.id = document::generate_object_id();
    table_grp.name = "Bảng (Table) " + std::to_string(rows) + "×" + std::to_string(cols);
    table_grp.children_ids = cell_ids;

    const auto table_id = table_grp.id;
    composite->add_command(std::make_unique<GroupObjectsCommand>(std::move(table_grp), cell_ids));

    auto res = doc.execute_command(std::move(composite));
    if (res.success) {
        root_.vector_controller().selection().select(table_id);
        sync_status_bar();
        return table_id;
    }
    return document::kInvalidObjectId;
}

void DocumentWorkflow::select_object(document::ObjectId id, bool toggle) {
    root_.vector_controller().set_selection(
        {id}, *root_.document().current_snapshot(),
        toggle ? vector::SelectionMode::Toggle : vector::SelectionMode::Replace
    );
    sync_status_bar();
}

bool DocumentWorkflow::transform_object(document::ObjectId id, double dx_mm, double dy_mm) {
    const auto snapshot = root_.document().current_snapshot();
    const auto delta = geometry::Transform2D::translation(dx_mm, dy_mm);
    auto cmd = vector::make_transformed_object_command(id, *snapshot, delta);
    auto res = root_.document().execute_command(std::move(cmd));
    sync_status_bar();
    return res.success;
}

bool DocumentWorkflow::can_align_selected(
    geometry::AlignMode mode,
    geometry::AlignReference reference,
    std::optional<document::ObjectId> key_object_id
) const {
    const auto& selected = root_.vector_controller().selection().selected_ids();
    if (selected.empty()) return false;

    const bool center_to_page = mode == geometry::AlignMode::CenterPage ||
        mode == geometry::AlignMode::CenterPageHorizontal ||
        mode == geometry::AlignMode::CenterPageVertical;
    if (selected.size() < 2 && reference != geometry::AlignReference::Page && !center_to_page)
        return false;
    if (reference == geometry::AlignReference::KeyObject &&
        (!key_object_id || std::find(selected.begin(), selected.end(), *key_object_id) == selected.end()))
        return false;

    const auto snapshot = root_.document().current_snapshot();
    return selection_has_editable_bounds(selected, *snapshot, *this);
}

bool DocumentWorkflow::align_selected(
    geometry::AlignMode mode,
    geometry::AlignReference reference,
    std::optional<document::ObjectId> key_object_id
) {
    const auto& sel = root_.vector_controller().selection().selected_ids();
    if (!can_align_selected(mode, reference, key_object_id)) return false;

    std::vector<geometry::ObjectAlignmentItem> items;
    for (auto id : root_.document().current_snapshot()->hierarchy().roots(sel)) {
        auto bounds_opt = get_object_bounds(id);
        if (!bounds_opt) return false;
        items.push_back({id, *bounds_opt});
    }
    if (items.empty()) return false;

    const auto snapshot = root_.document().current_snapshot();
    const auto& page = snapshot->page();
    geometry::Rect2D page_bounds{0.0, 0.0, page.width_mm, page.height_mm};

    auto deltas = geometry::compute_alignment(items, mode, reference, key_object_id, page_bounds);
    if (deltas.empty()) return false;

    auto composite = std::make_unique<document::CompositeCommand>("Align Objects");
    size_t moved_count = 0;
    for (const auto& delta : deltas) {
        if (std::abs(delta.dx_mm) > 1e-6 || std::abs(delta.dy_mm) > 1e-6) {
            composite->add_command(vector::make_transformed_object_command(
                delta.id, *root_.document().current_snapshot(), geometry::Transform2D::translation(delta.dx_mm, delta.dy_mm)
            ));
            moved_count++;
        }
    }
    if (moved_count == 0) return true; // Already aligned

    auto res = root_.document().execute_command(std::move(composite));
    sync_status_bar();
    return res.success;
}

bool DocumentWorkflow::can_distribute_selected(
    geometry::DistributeMode mode,
    double specified_spacing_mm
) const {
    if (!std::isfinite(specified_spacing_mm) ||
        (specified_spacing_mm < 0.0 && specified_spacing_mm != -1.0))
        return false;
    const auto& selected = root_.vector_controller().selection().selected_ids();
    const bool equal_spacing = mode == geometry::DistributeMode::SpacingHorizontal ||
        mode == geometry::DistributeMode::SpacingVertical;
    const size_t minimum_count = equal_spacing && specified_spacing_mm < 0.0 ? 3 : 2;
    if (selected.size() < minimum_count) return false;
    const auto snapshot = root_.document().current_snapshot();
    return selection_has_editable_bounds(selected, *snapshot, *this);
}

bool DocumentWorkflow::distribute_selected(
    geometry::DistributeMode mode,
    double specified_spacing_mm
) {
    const auto& sel = root_.vector_controller().selection().selected_ids();
    if (!can_distribute_selected(mode, specified_spacing_mm)) return false;

    std::vector<geometry::ObjectAlignmentItem> items;
    for (auto id : root_.document().current_snapshot()->hierarchy().roots(sel)) {
        auto bounds_opt = get_object_bounds(id);
        if (!bounds_opt) return false;
        items.push_back({id, *bounds_opt});
    }
    if (items.size() < 2) return false;

    auto deltas = geometry::compute_distribution(items, mode, specified_spacing_mm);
    if (deltas.empty()) return false;

    auto composite = std::make_unique<document::CompositeCommand>("Distribute Objects");
    size_t moved_count = 0;
    for (const auto& delta : deltas) {
        if (std::abs(delta.dx_mm) > 1e-6 || std::abs(delta.dy_mm) > 1e-6) {
            composite->add_command(vector::make_transformed_object_command(
                delta.id, *root_.document().current_snapshot(), geometry::Transform2D::translation(delta.dx_mm, delta.dy_mm)
            ));
            moved_count++;
        }
    }
    if (moved_count == 0) return true; // Already distributed

    auto res = root_.document().execute_command(std::move(composite));
    sync_status_bar();
    return res.success;
}

bool DocumentWorkflow::undo() {
    bool res = root_.document().undo();
    sync_status_bar();
    return res;
}

bool DocumentWorkflow::redo() {
    bool res = root_.document().redo();
    sync_status_bar();
    return res;
}

void DocumentWorkflow::new_document(double page_width_mm, double page_height_mm,
                                   color::DocumentColorMode mode, int32_t dpi,
                                   geometry::units::LengthUnit display_unit) {
    if (!std::isfinite(page_width_mm) || !std::isfinite(page_height_mm) ||
        page_width_mm <= 0.0 || page_height_mm <= 0.0 ||
        !geometry::units::is_physical_length_unit(display_unit))
        return;

    auto& doc = root_.document();
    auto page = document::Page::make_default();
    page.width_mm = page_width_mm;
    page.height_mm = page_height_mm;
    page.color_mode = mode;
    page.color_profile_name = color::default_document_profile(mode);
    page.raster_dpi = dpi == 0 ? (mode == color::DocumentColorMode::Cmyk ? 300 : 72) : dpi;
    page.color_mode_explicit = true;
    page.display_length_unit = display_unit;
    page.display_length_unit_explicit = true;
    if (!color::valid_document_color_mode(mode) ||
        (page.raster_dpi != 72 && page.raster_dpi != 150 && page.raster_dpi != 300)) return;
    auto snapshot = std::make_shared<const document::DocumentSnapshot>(
        doc.epoch() + 1,
        0,
        std::move(page),
        document::PersistentObjectMap{});
    if (!doc.reset_from_snapshot(std::move(snapshot)))
        return;
    root_.vector_controller().on_escape();
    root_.vector_controller().selection().clear();
    sync_status_bar();
}

void DocumentWorkflow::set_page_size(double page_width_mm, double page_height_mm) {
    if (!std::isfinite(page_width_mm) || !std::isfinite(page_height_mm) ||
        page_width_mm <= 0.0 || page_height_mm <= 0.0)
        return;

    page_width_mm = std::clamp(page_width_mm, 10.0, geometry::kMaxCanvasDimensionMm);
    page_height_mm = std::clamp(page_height_mm, 10.0, geometry::kMaxCanvasDimensionMm);

    auto& doc = root_.document();
    (void)doc.execute_command(std::make_unique<document::ResizePageCommand>(
        doc.active_page_id(), page_width_mm, page_height_mm));
    sync_status_bar();
}


} // namespace pikdraw::desktop
