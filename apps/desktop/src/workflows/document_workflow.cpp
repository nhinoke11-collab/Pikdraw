#include "pikdraw/desktop/workflows/document_workflow.hpp"
#include "pikdraw/vector/tools/shape_tools.hpp"
#include "pikdraw/vector/gestures/text_tool.hpp"
#include "pikdraw/typography/layout/glyph_outline.hpp"
#include "pikdraw/typography/layout/text_layout.hpp"
#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/commands/transaction_writer.hpp"
#include "pikdraw/color/values/color_value.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace pikdraw::desktop {

namespace {

color::RGBColor parse_hex_color(const std::string& hex) {
    if (hex.length() >= 7 && hex[0] == '#') {
        int r = std::stoi(hex.substr(1, 2), nullptr, 16);
        int g = std::stoi(hex.substr(3, 2), nullptr, 16);
        int b = std::stoi(hex.substr(5, 2), nullptr, 16);
        return color::RGBColor{r / 255.0, g / 255.0, b / 255.0, 1.0};
    }
    return color::RGBColor{0.0, 0.949, 0.765, 1.0}; // Cyber Teal #00f2c3
}

void collect_all_descendants(document::ObjectId id,
                             const document::PersistentObjectMap& map,
                             std::vector<document::ObjectId>& out) {
    const auto obj = map.get(id);
    if (!obj) return;
    if (const auto* grp = std::get_if<document::GroupObject>(&*obj)) {
        for (const auto cid : grp->children_ids) {
            out.push_back(cid);
            collect_all_descendants(cid, map, out);
        }
    }
}

} // namespace

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
    root_.document().execute_command(std::make_unique<document::AddObjectCommand>(std::move(rect_obj)));
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
    root_.document().execute_command(std::make_unique<document::AddObjectCommand>(std::move(ellipse_obj)));
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
    text_obj.name = "Văn bản: " + utf8_text.substr(0, 20);
    text_obj.text = utf8_text;
    text_obj.font = typography::FontIdentity{font_family, font_family + "-Regular", 400, false, font_size_pt};
    text_obj.fill = parse_hex_color(fill_hex);
    text_obj.position = geometry::Point2D{x_mm, y_mm};

    const auto id = text_obj.id;
    root_.document().execute_command(std::make_unique<document::AddObjectCommand>(std::move(text_obj)));
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
    auto cmd = std::make_unique<vector::UpdateTextStyleCommand>(
        text_id, font_family, font_size_pt, bold ? 700 : 400, italic, underline, alignment
    );
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
    auto layout = typography::TextLayout::layout_text(text_obj.text, text_obj.font);
    auto outline_path = typography::text_to_path_outlines(layout);
    if (outline_path.empty()) {
        auto b = text_obj.bounds();
        outline_path = geometry::Path2D::make_rect(b.x, b.y, b.width, b.height);
    } else {
        geometry::Transform2D pos_tx = geometry::Transform2D::translation(text_obj.position.x, text_obj.position.y);
        outline_path = outline_path.transformed(pos_tx * text_obj.transform);
    }

    document::PathObject path_obj;
    path_obj.id = document::generate_object_id();
    path_obj.name = "Đường cong: " + (text_obj.text.empty() ? "Curves" : text_obj.text.substr(0, 16));
    path_obj.path = std::move(outline_path);
    path_obj.fill = text_obj.fill;
    path_obj.stroke = color::RGBColor{0.0, 0.0, 0.0, 1.0};
    path_obj.stroke_width_mm = 0.5;
    path_obj.has_fill = true;
    path_obj.has_stroke = true;

    auto composite = std::make_unique<document::CompositeCommand>("Convert Text to Curves");
    composite->add_command(std::make_unique<document::RemoveObjectCommand>(text_id));
    const auto new_id = path_obj.id;
    composite->add_command(std::make_unique<document::AddObjectCommand>(std::move(path_obj)));
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
            composite->add_command(std::make_unique<document::AddObjectCommand>(std::move(cell_rect)));

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
            composite->add_command(std::make_unique<document::AddObjectCommand>(std::move(cell_text)));
        }
    }

    // 3. Wrap entire table into a GroupObject
    document::GroupObject table_grp;
    table_grp.id = document::generate_object_id();
    table_grp.name = "Bảng (Table) " + std::to_string(rows) + "×" + std::to_string(cols);
    table_grp.children_ids = std::move(cell_ids);

    const auto table_id = table_grp.id;
    composite->add_command(std::make_unique<document::AddObjectCommand>(std::move(table_grp)));

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
    if (is_group(id)) {
        std::vector<document::ObjectId> all_targets = { id };
        collect_all_descendants(id, snapshot->objects(), all_targets);
        auto composite = std::make_unique<document::CompositeCommand>("Transform Group");
        for (auto tid : all_targets) {
            composite->add_command(std::make_unique<document::TransformObjectCommand>(tid, delta));
        }
        const auto res = root_.document().execute_command(std::move(composite));
        sync_status_bar();
        return res.success;
    }

    auto cmd = std::make_unique<document::TransformObjectCommand>(id, delta);
    auto res = root_.document().execute_command(std::move(cmd));
    sync_status_bar();
    return res.success;
}

bool DocumentWorkflow::align_selected(
    geometry::AlignMode mode,
    geometry::AlignReference reference,
    std::optional<document::ObjectId> key_object_id
) {
    const auto& sel = root_.vector_controller().selection().selected_ids();
    if (sel.empty()) return false;

    if (sel.size() < 2 && reference != geometry::AlignReference::Page &&
        mode != geometry::AlignMode::CenterPage &&
        mode != geometry::AlignMode::CenterPageHorizontal &&
        mode != geometry::AlignMode::CenterPageVertical) {
        return false;
    }

    std::vector<geometry::ObjectAlignmentItem> items;
    for (auto id : sel) {
        auto bounds_opt = get_object_bounds(id);
        if (bounds_opt.has_value()) {
            items.push_back({id, *bounds_opt});
        }
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
            composite->add_command(std::make_unique<document::TransformObjectCommand>(
                delta.id, geometry::Transform2D::translation(delta.dx_mm, delta.dy_mm)
            ));
            moved_count++;
        }
    }
    if (moved_count == 0) return true; // Already aligned

    auto res = root_.document().execute_command(std::move(composite));
    sync_status_bar();
    return res.success;
}

bool DocumentWorkflow::distribute_selected(
    geometry::DistributeMode mode,
    double specified_spacing_mm
) {
    const auto& sel = root_.vector_controller().selection().selected_ids();
    if (sel.size() < 2) return false;

    std::vector<geometry::ObjectAlignmentItem> items;
    for (auto id : sel) {
        auto bounds_opt = get_object_bounds(id);
        if (bounds_opt.has_value()) {
            items.push_back({id, *bounds_opt});
        }
    }
    if (items.size() < 2) return false;

    auto deltas = geometry::compute_distribution(items, mode, specified_spacing_mm);
    if (deltas.empty()) return false;

    auto composite = std::make_unique<document::CompositeCommand>("Distribute Objects");
    size_t moved_count = 0;
    for (const auto& delta : deltas) {
        if (std::abs(delta.dx_mm) > 1e-6 || std::abs(delta.dy_mm) > 1e-6) {
            composite->add_command(std::make_unique<document::TransformObjectCommand>(
                delta.id, geometry::Transform2D::translation(delta.dx_mm, delta.dy_mm)
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

void DocumentWorkflow::new_document(double page_width_mm, double page_height_mm) {
    if (!std::isfinite(page_width_mm) || !std::isfinite(page_height_mm) ||
        page_width_mm <= 0.0 || page_height_mm <= 0.0)
        return;

    auto& doc = root_.document();
    auto page = document::Page::make_default();
    page.width_mm = page_width_mm;
    page.height_mm = page_height_mm;
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
    const auto snapshot = doc.current_snapshot();
    auto page = snapshot->page();
    page.width_mm = page_width_mm;
    page.height_mm = page_height_mm;
    auto new_snapshot = std::make_shared<const document::DocumentSnapshot>(
        snapshot->epoch(),
        snapshot->revision() + 1,
        std::move(page),
        snapshot->objects());
    doc.reset_from_snapshot(std::move(new_snapshot));
    sync_status_bar();
}

bool DocumentWorkflow::delete_selected() {
    auto& controller = root_.vector_controller();
    if (controller.selection().empty())
        return false;

    const auto snapshot = root_.document().current_snapshot();
    std::vector<document::ObjectId> to_delete;
    std::unordered_set<document::ObjectId> seen;

    for (const auto id : controller.selection().selected_ids()) {
        if (seen.insert(id).second) {
            to_delete.push_back(id);
            if (is_group(id)) {
                std::vector<document::ObjectId> desc;
                collect_all_descendants(id, snapshot->objects(), desc);
                for (auto cid : desc) {
                    if (seen.insert(cid).second) {
                        to_delete.push_back(cid);
                    }
                }
            }
        }
    }

    auto composite = std::make_unique<document::CompositeCommand>("Delete Selected");
    for (auto id : to_delete) {
        composite->add_command(std::make_unique<document::RemoveObjectCommand>(id));
    }
    controller.selection().clear();
    const auto res = root_.document().execute_command(std::move(composite));
    sync_status_bar();
    return res.success;
}

bool DocumentWorkflow::duplicate_selected(double offset_mm) {
    auto& controller = root_.vector_controller();
    const auto ids = controller.selection().selected_ids();
    if (ids.empty())
        return false;

    const auto snapshot = root_.document().current_snapshot();
    std::vector<document::ObjectId> new_selection_ids;
    auto composite = std::make_unique<document::CompositeCommand>("Duplicate Selected");
    const auto shift = geometry::Transform2D::translation(offset_mm, offset_mm);

    for (const auto id : ids) {
        const auto obj_opt = snapshot->objects().get(id);
        if (!obj_opt)
            continue;

        if (const auto* grp = std::get_if<document::GroupObject>(&*obj_opt)) {
            document::GroupObject clone_grp = *grp;
            const auto new_grp_id = document::generate_object_id();
            clone_grp.id = new_grp_id;
            clone_grp.name += " (Bản sao)";
            clone_grp.transform = shift * clone_grp.transform;
            clone_grp.children_ids.clear();

            for (const auto cid : grp->children_ids) {
                const auto c_opt = snapshot->objects().get(cid);
                if (!c_opt) continue;
                document::ObjectVariant c_clone = *c_opt;
                const auto new_cid = document::generate_object_id();
                std::visit([new_cid, &shift](auto& val) {
                    val.id = new_cid;
                    val.name += " (Bản sao)";
                    val.transform = shift * val.transform;
                }, c_clone);
                clone_grp.children_ids.push_back(new_cid);
                composite->add_command(std::make_unique<document::AddObjectCommand>(std::move(c_clone)));
            }
            composite->add_command(std::make_unique<document::AddObjectCommand>(std::move(clone_grp)));
            new_selection_ids.push_back(new_grp_id);
        } else {
            document::ObjectVariant clone = *obj_opt;
            const auto new_id = document::generate_object_id();
            std::visit([new_id, &shift](auto& val) {
                val.id = new_id;
                val.name += " (Bản sao)";
                val.transform = shift * val.transform;
            }, clone);
            composite->add_command(std::make_unique<document::AddObjectCommand>(std::move(clone)));
            new_selection_ids.push_back(new_id);
        }
    }

    if (!new_selection_ids.empty()) {
        const auto res = root_.document().execute_command(std::move(composite));
        if (res.success) {
            controller.selection().clear();
            for (const auto nid : new_selection_ids) {
                controller.selection().select(nid, vector::SelectionMode::Add);
            }
            sync_status_bar();
            return true;
        }
    }
    return false;
}

document::ObjectId DocumentWorkflow::group_selected() {
    auto& controller = root_.vector_controller();
    const auto selected = controller.selection().selected_ids();
    if (selected.size() < 2)
        return document::kInvalidObjectId;

    const auto snapshot = root_.document().current_snapshot();
    std::vector<document::ObjectId> valid_ids;
    for (auto id : selected) {
        if (snapshot->objects().contains(id)) {
            valid_ids.push_back(id);
        }
    }
    if (valid_ids.size() < 2)
        return document::kInvalidObjectId;

    const auto group_id = document::generate_object_id();
    document::GroupObject grp;
    grp.id = group_id;
    grp.name = "Nhóm (Group " + std::to_string(group_id) + ")";
    grp.children_ids = valid_ids;
    grp.transform = geometry::Transform2D::identity();
    grp.visible = true;
    grp.locked = false;

    auto cmd = std::make_unique<document::AddObjectCommand>(std::move(grp));
    const auto res = root_.document().execute_command(std::move(cmd));
    if (res.success) {
        controller.selection().select(group_id, vector::SelectionMode::Replace);
        sync_status_bar();
        return group_id;
    }
    return document::kInvalidObjectId;
}

bool DocumentWorkflow::ungroup_selected() {
    auto& controller = root_.vector_controller();
    const auto selected = controller.selection().selected_ids();
    if (selected.empty())
        return false;

    const auto snapshot = root_.document().current_snapshot();
    std::vector<document::ObjectId> groups_to_remove;
    std::vector<document::ObjectId> children_to_select;

    for (auto id : selected) {
        const auto obj = snapshot->objects().get(id);
        if (obj && std::holds_alternative<document::GroupObject>(*obj)) {
            groups_to_remove.push_back(id);
            const auto& grp = std::get<document::GroupObject>(*obj);
            for (auto cid : grp.children_ids) {
                if (snapshot->objects().contains(cid)) {
                    children_to_select.push_back(cid);
                }
            }
        }
    }

    if (groups_to_remove.empty())
        return false;

    auto composite = std::make_unique<document::CompositeCommand>("Ungroup Selected");
    for (auto gid : groups_to_remove) {
        composite->add_command(std::make_unique<document::RemoveObjectCommand>(gid));
    }

    const auto res = root_.document().execute_command(std::move(composite));
    if (res.success) {
        controller.selection().clear();
        for (auto cid : children_to_select) {
            controller.selection().select(cid, vector::SelectionMode::Add);
        }
        sync_status_bar();
        return true;
    }
    return false;
}

bool DocumentWorkflow::is_group(document::ObjectId id) const {
    const auto snapshot = root_.document().current_snapshot();
    const auto obj = snapshot->objects().get(id);
    if (!obj) return false;
    return std::holds_alternative<document::GroupObject>(*obj);
}

std::optional<document::ObjectId> DocumentWorkflow::find_parent_group(document::ObjectId child_id) const {
    const auto snapshot = root_.document().current_snapshot();
    for (const auto& [id, obj_var] : snapshot->objects().all()) {
        if (const auto* grp = std::get_if<document::GroupObject>(&obj_var)) {
            if (std::find(grp->children_ids.begin(), grp->children_ids.end(), child_id) != grp->children_ids.end()) {
                return grp->id;
            }
        }
    }
    return std::nullopt;
}

std::optional<geometry::Rect2D> DocumentWorkflow::get_object_bounds(document::ObjectId id) const {
    const auto snapshot = root_.document().current_snapshot();
    const auto obj_opt = snapshot->objects().get(id);
    if (!obj_opt) return std::nullopt;

    if (const auto* path_obj = std::get_if<document::PathObject>(&*obj_opt)) {
        return path_obj->bounds();
    }
    if (const auto* text_obj = std::get_if<document::TextObject>(&*obj_opt)) {
        return text_obj->bounds();
    }
    if (const auto* img_obj = std::get_if<document::ImageObject>(&*obj_opt)) {
        return img_obj->bounds();
    }
    if (const auto* grp = std::get_if<document::GroupObject>(&*obj_opt)) {
        std::optional<geometry::Rect2D> union_bounds;
        for (const auto cid : grp->children_ids) {
            auto cb = get_object_bounds(cid);
            if (cb) {
                union_bounds = union_bounds ? union_bounds->united(*cb) : *cb;
            }
        }
        if (union_bounds) {
            return grp->transform.map_rect(*union_bounds);
        }
    }
    return std::nullopt;
}

bool DocumentWorkflow::resize_object(document::ObjectId id, double new_w_mm, double new_h_mm) {
    if (!std::isfinite(new_w_mm) || !std::isfinite(new_h_mm) || new_w_mm <= 0.0 || new_h_mm <= 0.0)
        return false;

    new_w_mm = std::clamp(new_w_mm, 0.1, geometry::kMaxObjectDimensionMm);
    new_h_mm = std::clamp(new_h_mm, 0.1, geometry::kMaxObjectDimensionMm);

    const auto snapshot = root_.document().current_snapshot();
    const auto bounds_opt = get_object_bounds(id);
    if (!bounds_opt || bounds_opt->width <= 0.0 || bounds_opt->height <= 0.0)
        return false;

    const double sx = new_w_mm / bounds_opt->width;
    const double sy = new_h_mm / bounds_opt->height;

    const auto delta = geometry::Transform2D::translation(bounds_opt->x, bounds_opt->y) *
                       geometry::Transform2D::scale(sx, sy) *
                       geometry::Transform2D::translation(-bounds_opt->x, -bounds_opt->y);

    if (is_group(id)) {
        std::vector<document::ObjectId> all_targets = { id };
        collect_all_descendants(id, snapshot->objects(), all_targets);
        auto composite = std::make_unique<document::CompositeCommand>("Resize Group");
        for (auto tid : all_targets) {
            composite->add_command(std::make_unique<document::TransformObjectCommand>(tid, delta));
        }
        const auto res = root_.document().execute_command(std::move(composite));
        sync_status_bar();
        return res.success;
    }

    auto cmd = std::make_unique<document::TransformObjectCommand>(id, delta);
    const auto res = root_.document().execute_command(std::move(cmd));
    sync_status_bar();
    return res.success;
}

bool DocumentWorkflow::move_object(document::ObjectId id, double new_x_mm, double new_y_mm) {
    if (!std::isfinite(new_x_mm) || !std::isfinite(new_y_mm))
        return false;

    const auto snapshot = root_.document().current_snapshot();
    const auto bounds_opt = get_object_bounds(id);
    if (!bounds_opt)
        return false;

    const double dx = new_x_mm - bounds_opt->x;
    const double dy = new_y_mm - bounds_opt->y;
    const auto delta = geometry::Transform2D::translation(dx, dy);

    if (is_group(id)) {
        std::vector<document::ObjectId> all_targets = { id };
        collect_all_descendants(id, snapshot->objects(), all_targets);
        auto composite = std::make_unique<document::CompositeCommand>("Move Group");
        for (auto tid : all_targets) {
            composite->add_command(std::make_unique<document::TransformObjectCommand>(tid, delta));
        }
        const auto res = root_.document().execute_command(std::move(composite));
        sync_status_bar();
        return res.success;
    }

    auto cmd = std::make_unique<document::TransformObjectCommand>(id, delta);
    const auto res = root_.document().execute_command(std::move(cmd));
    sync_status_bar();
    return res.success;
}

bool DocumentWorkflow::set_selected_fill(const std::string& hex_color) {
    const auto ids = root_.vector_controller().selection().selected_ids();
    if (ids.empty())
        return false;

    const auto col = parse_hex_color(hex_color);
    auto& doc = root_.document();
    const auto snapshot = doc.current_snapshot();
    auto objects = snapshot->objects();

    for (const auto id : ids) {
        std::vector<document::ObjectId> targets = { id };
        collect_all_descendants(id, objects, targets);
        for (const auto tid : targets) {
            const auto obj_opt = objects.get(tid);
            if (!obj_opt)
                continue;

            document::ObjectVariant updated = *obj_opt;
            std::visit([&col](auto& val) {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, document::PathObject>) {
                    val.fill = col;
                    val.has_fill = true;
                } else if constexpr (std::is_same_v<T, document::TextObject>) {
                    val.fill = col;
                }
            }, updated);
            objects = objects.set(tid, std::move(updated));
        }
    }

    doc.apply_mutation(snapshot->page(), std::move(objects));
    sync_status_bar();
    return true;
}

bool DocumentWorkflow::set_selected_stroke(const std::string& hex_color, double stroke_width_mm) {
    const auto ids = root_.vector_controller().selection().selected_ids();
    if (ids.empty())
        return false;

    const auto col = parse_hex_color(hex_color);
    auto& doc = root_.document();
    const auto snapshot = doc.current_snapshot();
    auto objects = snapshot->objects();

    for (const auto id : ids) {
        std::vector<document::ObjectId> targets = { id };
        collect_all_descendants(id, objects, targets);
        for (const auto tid : targets) {
            const auto obj_opt = objects.get(tid);
            if (!obj_opt)
                continue;

            document::ObjectVariant updated = *obj_opt;
            std::visit([&col, stroke_width_mm](auto& val) {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, document::PathObject>) {
                    val.stroke = col;
                    val.stroke_width_mm = stroke_width_mm;
                    val.has_stroke = true;
                }
            }, updated);
            objects = objects.set(tid, std::move(updated));
        }
    }

    doc.apply_mutation(snapshot->page(), std::move(objects));
    sync_status_bar();
    return true;
}

bool DocumentWorkflow::save_native(const std::string& file_path) {
    const auto snapshot = root_.document().current_snapshot();
    auto result = document::SaveRecoveryService::atomic_save(file_path, *snapshot);
    if (result.success) {
        root_.document().mark_saved(snapshot->epoch(), snapshot->revision());
        sync_status_bar();
        return true;
    }
    return false;
}

bool DocumentWorkflow::open_native(const std::string& file_path) {
    std::string err;
    auto loaded_snapshot = document::SaveRecoveryService::load(file_path, &err);
    if (!loaded_snapshot) {
        return false;
    }

    // Replace current document content with loaded snapshot
    if (!root_.document().reset_from_snapshot(loaded_snapshot))
        return false;
    root_.vector_controller().on_escape();
    root_.vector_controller().selection().clear();
    sync_status_bar();
    return true;
}

bool DocumentWorkflow::save_svg(const std::string& file_path) {
    if (file_path.empty())
        return false;

    std::filesystem::path target;
    std::filesystem::path temporary;
    try {
        target = std::filesystem::u8path(file_path);
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        temporary = target;
        temporary += ".tmp-" + std::to_string(nonce);

        const auto contents = export_svg();
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
            return false;
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        output.flush();
        if (!output)
            throw std::runtime_error("SVG write failed");
        output.close();
        if (!output)
            throw std::runtime_error("SVG close failed");

#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), target.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("SVG replace failed");
#else
        std::filesystem::rename(temporary, target);
#endif
        return true;
    } catch (...) {
        if (!temporary.empty()) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
        }
        return false;
    }
}

bool DocumentWorkflow::import_svg(const std::string& svg_xml) {
    auto res = interop::SvgImporter::import_from_string(svg_xml);
    if (!res.success || res.objects.empty()) {
        return false;
    }

    for (auto& obj : res.objects) {
        root_.document().execute_command(std::make_unique<document::AddObjectCommand>(std::move(obj)));
    }
    sync_status_bar();
    return true;
}

std::string DocumentWorkflow::export_svg() {
    return svg_exporter_.export_to_string(*root_.document().current_snapshot());
}

bool DocumentWorkflow::boolean_operation_selected(
    geometry::BooleanOp op,
    bool keep_source,
    bool keep_target
) {
    const auto& sel = root_.vector_controller().selection().selected_ids();
    if (sel.size() < 2) return false;

    const auto snapshot = root_.document().current_snapshot();
    const auto target_id = sel[0];
    const auto source_id = sel[1];

    const auto target_var = snapshot->objects().get(target_id);
    const auto source_var = snapshot->objects().get(source_id);
    if (!target_var || !source_var) return false;

    color::ColorValue fill_col{color::RGBColor{0.0, 0.949, 0.765, 1.0}};
    color::ColorValue stroke_col{color::RGBColor{0.05, 0.65, 0.91, 1.0}};
    double stroke_w = 2.0;
    geometry::Rect2D tb{};
    geometry::Rect2D sb{};

    if (const auto* po = std::get_if<document::PathObject>(&*target_var)) {
        fill_col = po->fill;
        stroke_col = po->stroke;
        stroke_w = po->stroke_width_mm;
        tb = po->bounds();
    } else if (const auto* to = std::get_if<document::TextObject>(&*target_var)) {
        fill_col = to->fill;
        tb = to->bounds();
    } else {
        auto b = get_object_bounds(target_id);
        if (!b) return false;
        tb = *b;
    }

    if (const auto* po = std::get_if<document::PathObject>(&*source_var)) {
        sb = po->bounds();
    } else if (const auto* to = std::get_if<document::TextObject>(&*source_var)) {
        sb = to->bounds();
    } else {
        auto b = get_object_bounds(source_id);
        if (!b) return false;
        sb = *b;
    }

    geometry::BooleanResult res;
    if (op == geometry::BooleanOp::BackMinusFront) {
        res = geometry::boolean_rectangles(sb, tb, geometry::BooleanOp::Difference);
    } else {
        res = geometry::boolean_rectangles(tb, sb, op);
    }

    if (!res.success || res.path.empty()) {
        return false;
    }

    std::vector<std::unique_ptr<document::DocumentCommand>> cmds;
    if (!keep_target && op != geometry::BooleanOp::Boundary) {
        cmds.push_back(std::make_unique<document::RemoveObjectCommand>(target_id));
    }
    if (!keep_source && op != geometry::BooleanOp::Boundary) {
        cmds.push_back(std::make_unique<document::RemoveObjectCommand>(source_id));
    }

    document::PathObject new_obj;
    new_obj.id = document::generate_object_id();
    switch (op) {
        case geometry::BooleanOp::Union: new_obj.name = "Hàn (Weld)"; break;
        case geometry::BooleanOp::Difference: new_obj.name = "Xén (Trim)"; break;
        case geometry::BooleanOp::Intersection: new_obj.name = "Giao (Intersect)"; break;
        case geometry::BooleanOp::Xor: new_obj.name = "Rỗng (Exclude)"; break;
        case geometry::BooleanOp::BackMinusFront: new_obj.name = "Trừ dưới (Back Minus Front)"; break;
        case geometry::BooleanOp::Boundary: new_obj.name = "Đường bao (Boundary)"; break;
        default: new_obj.name = "Tạo hình (Shaping)"; break;
    }
    new_obj.path = std::move(res.path);
    new_obj.transform = geometry::Transform2D::identity();
    new_obj.fill = fill_col;
    new_obj.stroke = stroke_col;
    new_obj.stroke_width_mm = stroke_w;
    new_obj.has_fill = true;
    new_obj.has_stroke = true;

    const auto new_id = new_obj.id;
    cmds.push_back(std::make_unique<document::AddObjectCommand>(std::move(new_obj)));

    auto composite = std::make_unique<document::CompositeCommand>("Boolean Operation");
    for (auto& cmd : cmds) {
        composite->add_command(std::move(cmd));
    }
    root_.document().execute_command(std::move(composite));

    root_.vector_controller().selection().clear();
    root_.vector_controller().selection().select(new_id);
    sync_status_bar();
    return true;
}

document::ObjectId DocumentWorkflow::add_bitmap_image(
    double x_mm, double y_mm, double width_mm, double height_mm,
    const std::string& name
) {
    auto id = document::generate_object_id();
    auto img = document::ImageObject::make_procedural_photo(id, x_mm, y_mm, width_mm, height_mm, name);
    auto cmd = std::make_unique<document::AddObjectCommand>(std::move(img));
    root_.document().execute_command(std::move(cmd));
    sync_status_bar();
    return id;
}

bool DocumentWorkflow::split_objects_by_line(
    const geometry::Point2D& p1,
    const geometry::Point2D& p2,
    double separate_offset_mm
) {
    const auto snapshot = root_.document().current_snapshot();
    const auto& sel = root_.vector_controller().selection().selected_ids();

    std::vector<document::ObjectId> candidate_ids;
    if (!sel.empty()) {
        candidate_ids = sel;
    } else {
        for (const auto& [id, obj] : snapshot->objects().all()) {
            if (std::holds_alternative<document::PathObject>(obj) ||
                std::holds_alternative<document::TextObject>(obj) ||
                std::holds_alternative<document::ImageObject>(obj)) {
                candidate_ids.push_back(id);
            }
        }
    }

    std::vector<std::unique_ptr<document::DocumentCommand>> cmds;
    std::vector<document::ObjectId> new_ids;

    for (const auto id : candidate_ids) {
        const auto obj_var = snapshot->objects().get(id);
        if (!obj_var) continue;
        if (const auto* po = std::get_if<document::PathObject>(&*obj_var)) {
            const auto bounds = po->bounds();
            auto [split_ok, pieces] = geometry::split_rect_by_line(bounds, p1, p2);
            if (split_ok && pieces.size() >= 2) {
                auto b0 = pieces[0].bounds();
                auto b1 = pieces[1].bounds();
                if (b0.x > b1.x) {
                    std::swap(pieces[0], pieces[1]);
                }

                cmds.push_back(std::make_unique<document::RemoveObjectCommand>(id));
                int idx = 1;
                for (auto& piece_path : pieces) {
                    document::PathObject piece_obj;
                    piece_obj.id = document::generate_object_id();
                    piece_obj.name = po->name + " (Mảnh " + std::to_string(idx) + ")";
                    piece_obj.path = std::move(piece_path);
                    if (idx == 1) {
                        piece_obj.fill = po->fill;
                        piece_obj.stroke = po->stroke;
                        if (separate_offset_mm > 0.0) {
                            piece_obj.transform = geometry::Transform2D::translation(-separate_offset_mm * 0.5, 0.0);
                        }
                    } else {
                        // Contrasting color for piece 2 to prove distinct objects
                        piece_obj.fill = color::ColorValue{color::RGBColor{0.96, 0.62, 0.04, 1.0}};
                        piece_obj.stroke = color::ColorValue{color::RGBColor{0.85, 0.47, 0.02, 1.0}};
                        if (separate_offset_mm > 0.0) {
                            piece_obj.transform = geometry::Transform2D::translation(separate_offset_mm * 0.5, 0.0);
                        }
                    }
                    piece_obj.stroke_width_mm = po->stroke_width_mm;
                    piece_obj.has_fill = po->has_fill;
                    piece_obj.has_stroke = po->has_stroke;
                    new_ids.push_back(piece_obj.id);
                    cmds.push_back(std::make_unique<document::AddObjectCommand>(std::move(piece_obj)));
                    idx++;
                }
            }
        } else if (const auto* im = std::get_if<document::ImageObject>(&*obj_var)) {
            const auto bounds = im->bounds();
            auto [split_ok, pieces] = geometry::split_rect_by_line(bounds, p1, p2);
            if (split_ok && pieces.size() >= 2) {
                auto r1 = pieces[0].bounds();
                auto r2 = pieces[1].bounds();
                if (r1.x > r2.x) {
                    std::swap(r1, r2);
                }

                cmds.push_back(std::make_unique<document::RemoveObjectCommand>(id));

                int w_px = im->width_px;
                int h_px = im->height_px;
                int w_px1 = std::clamp<int>(static_cast<int>(std::round(w_px * (r1.width / bounds.width))), 2, w_px - 2);
                int w_px2 = w_px - w_px1;

                document::ImageObject im1;
                im1.id = document::generate_object_id();
                im1.name = im->name + " (Mảnh 1)";
                im1.bounds_rect = r1;
                im1.width_px = w_px1;
                im1.height_px = h_px;
                im1.pixels_rgba.resize(static_cast<size_t>(w_px1) * h_px);
                for (int y = 0; y < h_px; ++y) {
                    for (int x = 0; x < w_px1; ++x) {
                        im1.pixels_rgba[static_cast<size_t>(y) * w_px1 + x] =
                            im->pixels_rgba[static_cast<size_t>(y) * w_px + x];
                    }
                }

                document::ImageObject im2;
                im2.id = document::generate_object_id();
                im2.name = im->name + " (Mảnh 2)";
                im2.bounds_rect = r2;
                im2.width_px = w_px2;
                im2.height_px = h_px;
                im2.pixels_rgba.resize(static_cast<size_t>(w_px2) * h_px);
                for (int y = 0; y < h_px; ++y) {
                    for (int x = 0; x < w_px2; ++x) {
                        im2.pixels_rgba[static_cast<size_t>(y) * w_px2 + x] =
                            im->pixels_rgba[static_cast<size_t>(y) * w_px + (w_px1 + x)];
                    }
                }

                if (separate_offset_mm > 0.0) {
                    im1.transform = geometry::Transform2D::translation(-separate_offset_mm * 0.5, 0.0);
                    im2.transform = geometry::Transform2D::translation(separate_offset_mm * 0.5, 0.0);
                }

                new_ids.push_back(im1.id);
                new_ids.push_back(im2.id);
                cmds.push_back(std::make_unique<document::AddObjectCommand>(std::move(im1)));
                cmds.push_back(std::make_unique<document::AddObjectCommand>(std::move(im2)));
            }
        } else if (const auto* to = std::get_if<document::TextObject>(&*obj_var)) {
            const auto bounds = to->bounds();
            auto [split_ok, pieces] = geometry::split_rect_by_line(bounds, p1, p2);
            if (split_ok && pieces.size() >= 2) {
                auto b0 = pieces[0].bounds();
                auto b1 = pieces[1].bounds();
                if (b0.x > b1.x) {
                    std::swap(pieces[0], pieces[1]);
                }

                cmds.push_back(std::make_unique<document::RemoveObjectCommand>(id));
                int idx = 1;
                for (auto& piece_path : pieces) {
                    document::PathObject piece_obj;
                    piece_obj.id = document::generate_object_id();
                    piece_obj.name = to->name + " (Mảnh " + std::to_string(idx) + ")";
                    piece_obj.path = std::move(piece_path);
                    piece_obj.fill = to->fill;
                    piece_obj.stroke = color::ColorValue{color::RGBColor{0.0, 0.0, 0.0, 1.0}};
                    piece_obj.stroke_width_mm = 0.5;
                    piece_obj.has_fill = true;
                    piece_obj.has_stroke = true;
                    if (idx == 1) {
                        if (separate_offset_mm > 0.0) {
                            piece_obj.transform = geometry::Transform2D::translation(-separate_offset_mm * 0.5, 0.0);
                        }
                    } else {
                        piece_obj.fill = color::ColorValue{color::RGBColor{0.96, 0.62, 0.04, 1.0}};
                        if (separate_offset_mm > 0.0) {
                            piece_obj.transform = geometry::Transform2D::translation(separate_offset_mm * 0.5, 0.0);
                        }
                    }
                    const auto nid = piece_obj.id;
                    cmds.push_back(std::make_unique<document::AddObjectCommand>(std::move(piece_obj)));
                    new_ids.push_back(nid);
                    idx++;
                }
            }
        }
    }

    if (cmds.empty()) return false;

    auto composite = std::make_unique<document::CompositeCommand>("Split Objects");
    for (auto& cmd : cmds) {
        composite->add_command(std::move(cmd));
    }
    root_.document().execute_command(std::move(composite));

    root_.vector_controller().selection().clear();
    for (const auto nid : new_ids) {
        root_.vector_controller().selection().select(nid, vector::SelectionMode::Add);
    }
    sync_status_bar();
    return true;
}

bool DocumentWorkflow::trim_bitmap_with_vector(
    document::ObjectId bitmap_id,
    document::ObjectId vector_cutter_id,
    double separate_offset_mm
) {
    const auto snapshot = root_.document().current_snapshot();
    const auto im_var = snapshot->objects().get(bitmap_id);
    const auto vec_var = snapshot->objects().get(vector_cutter_id);
    if (!im_var || !vec_var) return false;

    const auto* im = std::get_if<document::ImageObject>(&*im_var);
    if (!im) return false;

    auto vb = get_object_bounds(vector_cutter_id);
    if (!vb) return false;

    auto ib = im->bounds();
    if (!ib.intersects(*vb)) return false;

    auto overlap = geometry::Rect2D{
        std::max(ib.x, vb->x),
        std::max(ib.y, vb->y),
        std::min(ib.x + ib.width, vb->x + vb->width) - std::max(ib.x, vb->x),
        std::min(ib.y + ib.height, vb->y + vb->height) - std::max(ib.y, vb->y)
    };
    if (overlap.width <= 1.0 || overlap.height <= 1.0) return false;

    int src_x0 = std::clamp<int>(static_cast<int>((overlap.x - ib.x) / ib.width * im->width_px), 0, im->width_px - 1);
    int src_y0 = std::clamp<int>(static_cast<int>((overlap.y - ib.y) / ib.height * im->height_px), 0, im->height_px - 1);
    int sub_w = std::clamp<int>(static_cast<int>(overlap.width / ib.width * im->width_px), 2, im->width_px - src_x0);
    int sub_h = std::clamp<int>(static_cast<int>(overlap.height / ib.height * im->height_px), 2, im->height_px - src_y0);

    document::ImageObject patch;
    patch.id = document::generate_object_id();
    patch.name = "Bitmap Patch (Trimmed)";
    patch.bounds_rect = overlap;
    patch.width_px = sub_w;
    patch.height_px = sub_h;
    patch.pixels_rgba.resize(static_cast<size_t>(sub_w) * sub_h);

    for (int y = 0; y < sub_h; ++y) {
        for (int x = 0; x < sub_w; ++x) {
            patch.pixels_rgba[static_cast<size_t>(y) * sub_w + x] =
                im->pixels_rgba[static_cast<size_t>(src_y0 + y) * im->width_px + (src_x0 + x)];
        }
    }

    if (separate_offset_mm > 0.0) {
        patch.transform = geometry::Transform2D::translation(separate_offset_mm, separate_offset_mm * 0.4);
    }

    auto cmd = std::make_unique<document::AddObjectCommand>(std::move(patch));
    root_.document().execute_command(std::move(cmd));
    sync_status_bar();
    return true;
}

bool DocumentWorkflow::auto_weld_overlapping() {
    const auto snapshot = root_.document().current_snapshot();
    std::vector<document::ObjectId> all_ids;
    for (const auto& [id, obj] : snapshot->objects().all()) {
        if (std::holds_alternative<document::PathObject>(obj)) {
            all_ids.push_back(id);
        }
    }
    if (all_ids.size() < 2) return false;

    for (size_t i = 0; i < all_ids.size(); ++i) {
        auto bi = get_object_bounds(all_ids[i]);
        if (!bi) continue;
        for (size_t j = i + 1; j < all_ids.size(); ++j) {
            auto bj = get_object_bounds(all_ids[j]);
            if (!bj) continue;
            if (bi->intersects(*bj)) {
                root_.vector_controller().selection().clear();
                root_.vector_controller().selection().select(all_ids[i], vector::SelectionMode::Add);
                root_.vector_controller().selection().select(all_ids[j], vector::SelectionMode::Add);
                return boolean_operation_selected(geometry::BooleanOp::Union);
            }
        }
    }
    return false;
}

void DocumentWorkflow::render_snapshot(
    int32_t width_px, int32_t height_px, std::vector<uint8_t>& out_buffer
) {
    if (width_px <= 0 || height_px <= 0 ||
        static_cast<uint64_t>(width_px) * static_cast<uint64_t>(height_px) > 8388608) {
        out_buffer.clear();
        return;
    }
    const size_t expected = static_cast<size_t>(width_px) * height_px * 4;
    if (out_buffer.size() != expected) {
        out_buffer.resize(expected, 0);
    }
    rendering::PixelBufferTarget target{out_buffer.data(), width_px, height_px, width_px * 4};
    root_.renderer().render_frame(*root_.document().current_snapshot(), target);
}

std::vector<uint8_t> DocumentWorkflow::render_snapshot(
    int32_t width_px, int32_t height_px
) {
    std::vector<uint8_t> buffer;
    render_snapshot(width_px, height_px, buffer);
    return buffer;
}

void DocumentWorkflow::sync_status_bar() {
    auto& status = root_.window().status_bar();
    const auto snap = root_.document().current_snapshot();
    status.set_document_state(snap->revision(), root_.document().is_dirty());
    status.set_selection_count(root_.vector_controller().selection().count());
}

} // namespace pikdraw::desktop
