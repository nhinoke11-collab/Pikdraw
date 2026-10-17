#include "pikdraw/desktop/workflows/document_workflow.hpp"
#include "pikdraw/vector/tools/shape_tools.hpp"
#include "pikdraw/vector/gestures/text_tool.hpp"
#include "pikdraw/typography/layout/glyph_outline.hpp"
#include "pikdraw/typography/layout/text_layout.hpp"
#include "pikdraw/document/commands/command.hpp"
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

std::vector<document::ObjectId> filter_top_level_roots(
    const std::vector<document::ObjectId>& ids,
    const document::PersistentObjectMap& objects
) {
    std::unordered_set<document::ObjectId> all_children;
    for (const auto id : ids) {
        if (const auto obj = objects.get(id)) {
            if (std::holds_alternative<document::GroupObject>(*obj)) {
                std::vector<document::ObjectId> descendants;
                collect_all_descendants(id, objects, descendants);
                all_children.insert(descendants.begin(), descendants.end());
            }
        }
    }
    std::vector<document::ObjectId> roots;
    std::unordered_set<document::ObjectId> seen;
    for (const auto id : ids) {
        if (!all_children.contains(id) && seen.insert(id).second) {
            roots.push_back(id);
        }
    }
    return roots;
}

class AddClonedHierarchyCommand final : public document::DocumentCommand {
public:
    AddClonedHierarchyCommand(
        std::vector<document::ObjectVariant> cloned_objects,
        std::vector<document::ObjectId> top_level_ids,
        size_t layer_idx = 0,
        std::optional<size_t> insert_position = std::nullopt,
        std::string command_name = "Add Cloned Hierarchy"
    )
        : cloned_objects_(std::move(cloned_objects)),
          top_level_ids_(std::move(top_level_ids)),
          layer_idx_(layer_idx),
          insert_position_(insert_position),
          name_(std::move(command_name)) {}

    std::string_view name() const noexcept override { return name_; }

    document::CommandResult execute(document::Document& doc) override {
        if (executed_) return document::CommandResult::fail("AddClonedHierarchyCommand already executed");
        auto page = doc.page();
        if (layer_idx_ >= page.layers.size()) return document::CommandResult::fail("Invalid layer index");
        if (page.layers[layer_idx_].locked) return document::CommandResult::fail("Layer is locked");

        auto objects = doc.objects();
        for (const auto& obj : cloned_objects_) {
            const auto id = std::visit([](const auto& o) { return o.id; }, obj);
            if (objects.contains(id)) return document::CommandResult::fail("Object ID collision");
            objects = objects.set(id, obj);
            document::reserve_object_id(id);
            if (const auto* path = std::get_if<document::PathObject>(&obj)) {
                if (path->authoring_source) {
                    for (const auto& subpath : path->authoring_source->subpaths) {
                        document::reserve_path_element_id(subpath.id);
                        for (const auto& node : subpath.nodes) document::reserve_path_element_id(node.id);
                        for (const auto& segment : subpath.segments) document::reserve_path_element_id(segment.id);
                    }
                }
            }
        }

        auto& layer_roots = page.layers[layer_idx_].object_ids;
        if (insert_position_ && *insert_position_ <= layer_roots.size()) {
            layer_roots.insert(layer_roots.begin() + *insert_position_, top_level_ids_.begin(), top_level_ids_.end());
        } else {
            layer_roots.insert(layer_roots.end(), top_level_ids_.begin(), top_level_ids_.end());
        }

        before_page_ = doc.page();
        before_objects_ = doc.objects();
        doc.apply_mutation(std::move(page), std::move(objects));
        executed_ = true;
        return document::CommandResult::ok();
    }

    document::CommandResult undo(document::Document& doc) override {
        if (!executed_ || !before_page_ || !before_objects_) {
            return document::CommandResult::fail("No state to undo");
        }
        doc.apply_mutation(*before_page_, *before_objects_);
        executed_ = false;
        return document::CommandResult::ok();
    }

private:
    std::vector<document::ObjectVariant> cloned_objects_;
    std::vector<document::ObjectId> top_level_ids_;
    size_t layer_idx_{0};
    std::optional<size_t> insert_position_;
    std::string name_;
    std::optional<document::Page> before_page_;
    std::optional<document::PersistentObjectMap> before_objects_;
    bool executed_{false};
};

class ReplaceClonedHierarchyCommand final : public document::DocumentCommand {
public:
    ReplaceClonedHierarchyCommand(
        std::vector<document::ObjectId> ids_to_remove,
        std::vector<document::ObjectVariant> new_objects,
        std::vector<document::ObjectId> new_top_level_ids,
        size_t layer_idx = 0,
        std::string command_name = "Array Instances"
    )
        : ids_to_remove_(std::move(ids_to_remove)),
          new_objects_(std::move(new_objects)),
          new_top_level_ids_(std::move(new_top_level_ids)),
          layer_idx_(layer_idx),
          name_(std::move(command_name)) {}

    std::string_view name() const noexcept override { return name_; }

    document::CommandResult execute(document::Document& doc) override {
        if (executed_) return document::CommandResult::fail("ReplaceClonedHierarchyCommand already executed");
        auto page = doc.page();
        if (layer_idx_ >= page.layers.size()) return document::CommandResult::fail("Invalid layer index");
        if (page.layers[layer_idx_].locked) return document::CommandResult::fail("Layer is locked");

        auto objects = doc.objects();

        std::unordered_set<document::ObjectId> all_to_remove(ids_to_remove_.begin(), ids_to_remove_.end());
        for (const auto id : ids_to_remove_) {
            std::vector<document::ObjectId> desc;
            collect_all_descendants(id, objects, desc);
            all_to_remove.insert(desc.begin(), desc.end());
        }

        for (const auto id : all_to_remove) {
            objects = objects.remove(id);
        }

        auto& layer_roots = page.layers[layer_idx_].object_ids;
        std::vector<document::ObjectId> updated_roots;
        updated_roots.reserve(layer_roots.size());
        for (const auto id : layer_roots) {
            if (!all_to_remove.contains(id)) {
                updated_roots.push_back(id);
            }
        }
        layer_roots = std::move(updated_roots);

        for (const auto& obj : new_objects_) {
            const auto id = std::visit([](const auto& o) { return o.id; }, obj);
            objects = objects.set(id, obj);
            document::reserve_object_id(id);
            if (const auto* path = std::get_if<document::PathObject>(&obj)) {
                if (path->authoring_source) {
                    for (const auto& subpath : path->authoring_source->subpaths) {
                        document::reserve_path_element_id(subpath.id);
                        for (const auto& node : subpath.nodes) document::reserve_path_element_id(node.id);
                        for (const auto& segment : subpath.segments) document::reserve_path_element_id(segment.id);
                    }
                }
            }
        }

        layer_roots.insert(layer_roots.end(), new_top_level_ids_.begin(), new_top_level_ids_.end());

        before_page_ = doc.page();
        before_objects_ = doc.objects();
        doc.apply_mutation(std::move(page), std::move(objects));
        executed_ = true;
        return document::CommandResult::ok();
    }

    document::CommandResult undo(document::Document& doc) override {
        if (!executed_ || !before_page_ || !before_objects_) {
            return document::CommandResult::fail("No state to undo");
        }
        doc.apply_mutation(*before_page_, *before_objects_);
        executed_ = false;
        return document::CommandResult::ok();
    }

private:
    std::vector<document::ObjectId> ids_to_remove_;
    std::vector<document::ObjectVariant> new_objects_;
    std::vector<document::ObjectId> new_top_level_ids_;
    size_t layer_idx_{0};
    std::string name_;
    std::optional<document::Page> before_page_;
    std::optional<document::PersistentObjectMap> before_objects_;
    bool executed_{false};
};

bool selection_has_editable_bounds(
    const std::vector<document::ObjectId>& selected,
    const document::DocumentSnapshot& snapshot,
    const DocumentWorkflow& workflow
) {
    if (selected.empty()) return false;
    for (const auto id : selected) {
        const auto object = snapshot.objects().get(id);
        if (!object || std::visit([](const auto& value) { return value.locked; }, *object)) return false;
        const auto bounds = workflow.get_object_bounds(id);
        if (!bounds || !bounds->is_finite()) return false;
    }
    return true;
}

bool is_supported_boolean_operation(geometry::BooleanOp op) noexcept {
    switch (op) {
    case geometry::BooleanOp::Union:
    case geometry::BooleanOp::Difference:
    case geometry::BooleanOp::Intersection:
    case geometry::BooleanOp::Xor:
    case geometry::BooleanOp::BackMinusFront:
        return true;
    case geometry::BooleanOp::Boundary:
    case geometry::BooleanOp::Simplify:
        return false;
    }
    return false;
}
bool valid_effect_parameters(const document::EffectParameters &parameters) {
    return std::visit([](const auto &effect) {
        using T = std::decay_t<decltype(effect)>;
        if constexpr (std::is_same_v<T, document::GaussianBlurEffect>) {
            return std::isfinite(effect.sigma_x_mm) && std::isfinite(effect.sigma_y_mm) &&
                   effect.sigma_x_mm >= 0.0 && effect.sigma_y_mm >= 0.0 &&
                   effect.sigma_x_mm <= geometry::limits::kCanvasMaxMm &&
                   effect.sigma_y_mm <= geometry::limits::kCanvasMaxMm;
        } else {
            return std::isfinite(effect.dx_mm) && std::isfinite(effect.dy_mm) &&
                   std::abs(effect.dx_mm) <= geometry::limits::kCanvasMaxMm &&
                   std::abs(effect.dy_mm) <= geometry::limits::kCanvasMaxMm &&
                   std::isfinite(effect.sigma_x_mm) && std::isfinite(effect.sigma_y_mm) &&
                   effect.sigma_x_mm >= 0.0 && effect.sigma_y_mm >= 0.0 &&
                   effect.sigma_x_mm <= geometry::limits::kCanvasMaxMm &&
                   effect.sigma_y_mm <= geometry::limits::kCanvasMaxMm &&
                   effect.color.is_valid() && std::isfinite(effect.opacity) &&
                   effect.opacity >= 0.0 && effect.opacity <= 1.0 &&
                   static_cast<uint8_t>(effect.blend_mode) <=
                       static_cast<uint8_t>(document::BlendMode::Lighten);
        }
    }, parameters);
}

std::optional<double> parse_effect_draft_number(const std::string &text) {
    if (text.empty()) return std::nullopt;
    std::string normalized = text;
    std::replace(normalized.begin(), normalized.end(), ',', '.');
    try {
        size_t parsed = 0;
        const double value = std::stod(normalized, &parsed);
        if (parsed != normalized.size() || !std::isfinite(value)) return std::nullopt;
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<geometry::BooleanResult> compute_selected_boolean(
    const document::DocumentSnapshot& snapshot,
    const std::vector<document::ObjectId>& selected,
    geometry::BooleanOp op
) {
    if (selected.size() != 2 || !is_supported_boolean_operation(op)) return std::nullopt;
    const auto target_object = snapshot.objects().get(selected[0]);
    const auto source_object = snapshot.objects().get(selected[1]);
    if (!target_object || !source_object) return std::nullopt;
    const auto* target = std::get_if<document::PathObject>(&*target_object);
    const auto* source = std::get_if<document::PathObject>(&*source_object);
    if (!target || !source || target->locked || source->locked) return std::nullopt;
    try {
        auto target_path = target->path.transformed(target->transform);
        auto source_path = source->path.transformed(source->transform);
        auto result = op == geometry::BooleanOp::BackMinusFront
            ? geometry::boolean_paths(source_path, target_path, geometry::BooleanOp::Difference)
            : geometry::boolean_paths(target_path, source_path, op);
        if (!result.success || result.path.empty()) return std::nullopt;
        return result;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

class GroupObjectsCommand final : public document::DocumentCommand {
public:
    GroupObjectsCommand(document::GroupObject group, std::vector<document::ObjectId> children)
        : group_(std::move(group)), children_(std::move(children)) {}

    std::string_view name() const noexcept override { return "Group Objects"; }

    document::CommandResult execute(document::Document& doc) override {
        if (executed_)
            return document::CommandResult::fail("Group command was already executed");
        if (children_.size() < 2 || group_.id == document::kInvalidObjectId ||
            doc.objects().contains(group_.id))
            return document::CommandResult::fail("A group requires two existing objects and a new ID");

        auto page = doc.page();
        auto objects = doc.objects();
        const std::unordered_set<document::ObjectId> selected(children_.begin(), children_.end());
        if (selected.size() != children_.size())
            return document::CommandResult::fail("Group selection contains duplicate objects");

        size_t owner_layer = page.layers.size();
        std::vector<size_t> positions;
        positions.reserve(children_.size());
        for (size_t layer_index = 0; layer_index < page.layers.size(); ++layer_index) {
            const auto& layer = page.layers[layer_index];
            for (size_t index = 0; index < layer.object_ids.size(); ++index) {
                if (!selected.contains(layer.object_ids[index])) continue;
                if (owner_layer == page.layers.size()) owner_layer = layer_index;
                if (owner_layer != layer_index)
                    return document::CommandResult::fail("Objects must be in the same layer to group");
                positions.push_back(index);
            }
        }
        if (positions.size() != children_.size())
            return document::CommandResult::fail("Only sibling objects can be grouped");
        auto& layer = page.layers[owner_layer];
        if (layer.locked || !layer.visible)
            return document::CommandResult::fail("The selected layer is hidden or locked");

        const auto insertion = *std::min_element(positions.begin(), positions.end());
        auto& roots = layer.object_ids;
        std::vector<document::ObjectId> remaining;
        std::vector<document::ObjectId> ordered_children;
        remaining.reserve(roots.size() - children_.size() + 1);
        ordered_children.reserve(children_.size());
        for (size_t index = 0; index < roots.size(); ++index) {
            const auto id = roots[index];
            if (index == insertion) remaining.push_back(group_.id);
            if (selected.contains(id))
                ordered_children.push_back(id);
            else
                remaining.push_back(id);
        }
        group_.children_ids = std::move(ordered_children);
        layer.object_ids = std::move(remaining);

        before_page_ = doc.page();
        before_objects_ = doc.objects();
        objects = objects.set(group_.id, group_);
        doc.apply_mutation(std::move(page), std::move(objects));
        executed_ = true;
        return document::CommandResult::ok();
    }

    document::CommandResult undo(document::Document& doc) override {
        if (!executed_ || !before_page_ || !before_objects_)
            return document::CommandResult::fail("Group command has no committed state to restore");
        doc.apply_mutation(*before_page_, *before_objects_);
        executed_ = false;
        return document::CommandResult::ok();
    }

private:
    document::GroupObject group_;
    std::vector<document::ObjectId> children_;
    std::optional<document::Page> before_page_;
    std::optional<document::PersistentObjectMap> before_objects_;
    bool executed_{false};
};

class UngroupObjectsCommand final : public document::DocumentCommand {
public:
    explicit UngroupObjectsCommand(std::vector<document::ObjectId> groups)
        : groups_(std::move(groups)) {}

    std::string_view name() const noexcept override { return "Ungroup Objects"; }

    document::CommandResult execute(document::Document& doc) override {
        if (executed_)
            return document::CommandResult::fail("Ungroup command was already executed");
        if (groups_.empty())
            return document::CommandResult::fail("No groups were selected");
        const std::unordered_set<document::ObjectId> selected(groups_.begin(), groups_.end());
        if (selected.size() != groups_.size())
            return document::CommandResult::fail("Ungroup selection contains duplicate groups");

        auto page = doc.page();
        auto objects = doc.objects();
        std::unordered_set<document::ObjectId> found;
        for (auto& layer : page.layers) {
            std::vector<document::ObjectId> roots;
            roots.reserve(layer.object_ids.size());
            for (const auto id : layer.object_ids) {
                if (!selected.contains(id)) {
                    roots.push_back(id);
                    continue;
                }
                const auto object = objects.get(id);
                const auto* group = object ? std::get_if<document::GroupObject>(&*object) : nullptr;
                if (!group)
                    return document::CommandResult::fail("Only top-level groups can be ungrouped");
                if (layer.locked || !layer.visible || group->locked)
                    return document::CommandResult::fail("A selected group or its layer is hidden or locked");
                roots.insert(roots.end(), group->children_ids.begin(), group->children_ids.end());
                found.insert(id);
                objects = objects.remove(id);
            }
            layer.object_ids = std::move(roots);
        }
        if (found.size() != selected.size())
            return document::CommandResult::fail("Only top-level groups can be ungrouped");

        before_page_ = doc.page();
        before_objects_ = doc.objects();
        doc.apply_mutation(std::move(page), std::move(objects));
        executed_ = true;
        return document::CommandResult::ok();
    }

    document::CommandResult undo(document::Document& doc) override {
        if (!executed_ || !before_page_ || !before_objects_)
            return document::CommandResult::fail("Ungroup command has no committed state to restore");
        doc.apply_mutation(*before_page_, *before_objects_);
        executed_ = false;
        return document::CommandResult::ok();
    }

private:
    std::vector<document::ObjectId> groups_;
    std::optional<document::Page> before_page_;
    std::optional<document::PersistentObjectMap> before_objects_;
    bool executed_{false};
};

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
    for (auto id : sel) {
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
    for (auto id : sel) {
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
    if (!doc.reset_from_snapshot(std::move(new_snapshot)))
        return;
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

bool DocumentWorkflow::copy_selected(void* hwnd) {
    const auto& selected = root_.vector_controller().selection().selected_ids();
    if (selected.empty())
        return false;

    const auto snapshot = root_.document().current_snapshot();
    const auto root_ids = filter_top_level_roots(selected, snapshot->objects());
    if (root_ids.empty())
        return false;

    document::Page clip_page = snapshot->page();
    clip_page.layers.clear();
    document::Layer clip_layer;
    clip_layer.name = "Clipboard";
    clip_layer.visible = true;
    clip_layer.locked = false;
    clip_layer.object_ids = root_ids;

    document::PersistentObjectMap clip_objects;
    std::string text_accumulator;

    for (const auto rid : root_ids) {
        if (auto obj = snapshot->objects().get(rid)) {
            clip_objects = clip_objects.set(rid, *obj);
            if (const auto* txt = std::get_if<document::TextObject>(&*obj)) {
                if (!text_accumulator.empty()) text_accumulator += "\n";
                text_accumulator += txt->text;
            }
        }
        std::vector<document::ObjectId> descendants;
        collect_all_descendants(rid, snapshot->objects(), descendants);
        for (const auto cid : descendants) {
            if (auto cobj = snapshot->objects().get(cid)) {
                clip_objects = clip_objects.set(cid, *cobj);
                if (const auto* txt = std::get_if<document::TextObject>(&*cobj)) {
                    if (!text_accumulator.empty()) text_accumulator += "\n";
                    text_accumulator += txt->text;
                }
            }
        }
    }
    clip_page.layers.push_back(std::move(clip_layer));

    auto clip_snapshot = std::make_shared<const document::DocumentSnapshot>(
        snapshot->epoch(),
        snapshot->revision(),
        std::move(clip_page),
        std::move(clip_objects),
        snapshot->document_id().empty() ? std::string("clipboard-document") : snapshot->document_id()
    );

    const auto binary_bytes = document::NativeFormat::serialize(*clip_snapshot);
    const auto svg_str = svg_exporter_.export_to_string(*clip_snapshot);

    return NativeClipboard::write(
        static_cast<HWND>(hwnd),
        std::string_view(reinterpret_cast<const char*>(binary_bytes.data()), binary_bytes.size()),
        svg_str,
        text_accumulator
    );
}

bool DocumentWorkflow::cut_selected(void* hwnd) {
    if (!copy_selected(hwnd))
        return false;
    return delete_selected();
}

bool DocumentWorkflow::paste(void* hwnd, PastePlacement placement, double offset_x, double offset_y) {
    (void)hwnd;
    // 1. Try native snapshot
    if (NativeClipboard::has_native_snapshot()) {
        auto raw_bytes = NativeClipboard::read_native_snapshot();
        if (raw_bytes && !raw_bytes->empty()) {
            std::string err;
            auto clip_snap = document::NativeFormat::deserialize(
                reinterpret_cast<const uint8_t*>(raw_bytes->data()),
                raw_bytes->size(),
                &err
            );
            if (clip_snap && !clip_snap->page().layers.empty()) {
                const auto& clip_root_ids = clip_snap->page().layers[0].object_ids;
                if (!clip_root_ids.empty()) {
                    geometry::Transform2D shift;
                    if (placement == PastePlacement::InPlace || placement == PastePlacement::InFront || placement == PastePlacement::InBack) {
                        shift = geometry::Transform2D::identity();
                    } else if (placement == PastePlacement::Offset) {
                        shift = geometry::Transform2D::translation(offset_x, offset_y);
                    } else {
                        // CenterOfViewport / default paste: offset by (+5, +5)
                        shift = geometry::Transform2D::translation(offset_x, offset_y);
                    }

                    auto cloned = document::clone_hierarchy(clip_root_ids, *clip_snap, shift);
                    if (!cloned.top_level_cloned_ids.empty()) {
                        std::optional<size_t> insert_pos = std::nullopt;
                        const auto current_snap = root_.document().current_snapshot();
                        if (!current_snap->page().layers.empty()) {
                            const auto& layer_roots = current_snap->page().layers[0].object_ids;
                            if (placement == PastePlacement::InFront) {
                                size_t max_idx = 0;
                                bool found = false;
                                for (size_t i = 0; i < layer_roots.size(); ++i) {
                                    if (std::find(clip_root_ids.begin(), clip_root_ids.end(), layer_roots[i]) != clip_root_ids.end()) {
                                        max_idx = (std::max)(max_idx, i);
                                        found = true;
                                    }
                                }
                                if (found) insert_pos = max_idx + 1;
                            } else if (placement == PastePlacement::InBack) {
                                size_t min_idx = layer_roots.size();
                                bool found = false;
                                for (size_t i = 0; i < layer_roots.size(); ++i) {
                                    if (std::find(clip_root_ids.begin(), clip_root_ids.end(), layer_roots[i]) != clip_root_ids.end()) {
                                        min_idx = (std::min)(min_idx, i);
                                        found = true;
                                    }
                                }
                                if (found) insert_pos = min_idx;
                            }
                        }

                        auto cmd = std::make_unique<AddClonedHierarchyCommand>(
                            std::move(cloned.cloned_objects),
                            cloned.top_level_cloned_ids,
                            0,
                            insert_pos,
                            "Paste"
                        );
                        auto& controller = root_.vector_controller();
                        const auto res = root_.document().execute_command(std::move(cmd));
                        if (res.success) {
                            controller.selection().clear();
                            for (auto nid : cloned.top_level_cloned_ids) {
                                controller.selection().select(nid, vector::SelectionMode::Add);
                            }
                            sync_status_bar();
                            return true;
                        }
                    }
                }
            }
        }
    }

    // 2. Try SVG vector data
    if (NativeClipboard::has_svg()) {
        auto svg_str = NativeClipboard::read_svg();
        if (svg_str && !svg_str->empty()) {
            auto res = interop::SvgImporter::import_from_string(*svg_str);
            if (res.success && !res.objects.empty()) {
                auto composite = std::make_unique<document::CompositeCommand>("Paste SVG");
                std::vector<document::ObjectId> new_ids;
                geometry::Transform2D shift = geometry::Transform2D::identity();
                if (placement == PastePlacement::Offset || placement == PastePlacement::CenterOfViewport) {
                    shift = geometry::Transform2D::translation(offset_x, offset_y);
                }
                for (auto& obj : res.objects) {
                    std::visit([&shift](auto& o) { o.transform = shift * o.transform; }, obj);
                    const auto nid = std::visit([](const auto& o) { return o.id; }, obj);
                    composite->add_command(std::make_unique<document::AddObjectCommand>(std::move(obj), 0));
                    new_ids.push_back(nid);
                }
                const auto cmd_res = root_.document().execute_command(std::move(composite));
                if (cmd_res.success) {
                    auto& controller = root_.vector_controller();
                    controller.selection().clear();
                    for (auto nid : new_ids) {
                        controller.selection().select(nid, vector::SelectionMode::Add);
                    }
                    sync_status_bar();
                    return true;
                }
            }
        }
    }

    // 3. Try plain text
    if (NativeClipboard::has_text()) {
        auto text = NativeClipboard::read_text();
        if (text && !text->empty()) {
            const auto snap = root_.document().current_snapshot();
            const double px = snap->page().width_mm * 0.5 - 20.0;
            const double py = snap->page().height_mm * 0.5;
            auto tid = add_vietnamese_text(px, py, *text);
            return tid != document::kInvalidObjectId;
        }
    }

    return false;
}

bool DocumentWorkflow::paste_in_place(void* hwnd) {
    return paste(hwnd, PastePlacement::InPlace, 0.0, 0.0);
}

bool DocumentWorkflow::paste_in_front(void* hwnd) {
    return paste(hwnd, PastePlacement::InFront, 0.0, 0.0);
}

bool DocumentWorkflow::paste_in_back(void* hwnd) {
    return paste(hwnd, PastePlacement::InBack, 0.0, 0.0);
}

bool DocumentWorkflow::duplicate_selected(double offset_x, double offset_y) {
    auto& controller = root_.vector_controller();
    const auto ids = controller.selection().selected_ids();
    if (ids.empty())
        return false;

    const auto snapshot = root_.document().current_snapshot();
    const auto root_ids = filter_top_level_roots(ids, snapshot->objects());
    if (root_ids.empty())
        return false;

    const auto shift = geometry::Transform2D::translation(offset_x, offset_y);
    auto cloned = document::clone_hierarchy(root_ids, *snapshot, shift);
    if (cloned.top_level_cloned_ids.empty())
        return false;

    auto cmd = std::make_unique<AddClonedHierarchyCommand>(
        std::move(cloned.cloned_objects),
        cloned.top_level_cloned_ids,
        0,
        std::nullopt,
        "Duplicate Selected"
    );

    const auto res = root_.document().execute_command(std::move(cmd));
    if (res.success) {
        controller.selection().clear();
        for (const auto nid : cloned.top_level_cloned_ids) {
            controller.selection().select(nid, vector::SelectionMode::Add);
        }

        // Start dynamic array session (SketchUp *N / /N)
        if (std::abs(offset_x) > 1e-6 || std::abs(offset_y) > 1e-6) {
            start_array_session(
                root_ids,
                geometry::Point2D{offset_x, offset_y},
                cloned.top_level_cloned_ids
            );
        } else {
            end_array_session();
        }

        sync_status_bar();
        return true;
    }
    return false;
}

bool DocumentWorkflow::duplicate_selected(double offset_mm) {
    return duplicate_selected(offset_mm, offset_mm);
}

bool DocumentWorkflow::duplicate_at_origin() {
    return duplicate_selected(0.0, 0.0);
}

bool DocumentWorkflow::is_array_session_active() const noexcept {
    return array_session_.active;
}

const DocumentWorkflow::ArraySession& DocumentWorkflow::array_session() const noexcept {
    return array_session_;
}

void DocumentWorkflow::start_array_session(
    std::vector<document::ObjectId> source_ids,
    const geometry::Point2D& base_delta,
    std::vector<document::ObjectId> first_copy_ids
) {
    array_session_.active = true;
    array_session_.source_ids = std::move(source_ids);
    array_session_.base_delta = base_delta;
    array_session_.current_array_ids = std::move(first_copy_ids);
    array_session_.is_multiplier = true;
    array_session_.current_count = 1;
}

bool DocumentWorkflow::apply_array_multiplier(int count) {
    if (!array_session_.active || array_session_.source_ids.empty())
        return false;
    if (count < 1 || count > 1000)
        return false;

    const auto snapshot = root_.document().current_snapshot();

    std::vector<document::ObjectVariant> all_new_objects;
    std::vector<document::ObjectId> all_new_top_level_ids;

    for (int k = 1; k <= count; ++k) {
        const double factor = static_cast<double>(k);
        const auto shift = geometry::Transform2D::translation(
            array_session_.base_delta.x * factor,
            array_session_.base_delta.y * factor
        );
        auto cloned = document::clone_hierarchy(array_session_.source_ids, *snapshot, shift);
        for (auto& obj : cloned.cloned_objects) {
            all_new_objects.push_back(std::move(obj));
        }
        for (auto tid : cloned.top_level_cloned_ids) {
            all_new_top_level_ids.push_back(tid);
        }
    }

    auto cmd = std::make_unique<ReplaceClonedHierarchyCommand>(
        array_session_.current_array_ids,
        std::move(all_new_objects),
        all_new_top_level_ids,
        0,
        "Array Multiplier (*" + std::to_string(count) + ")"
    );

    const auto res = root_.document().execute_command(std::move(cmd));
    if (res.success) {
        array_session_.current_array_ids = all_new_top_level_ids;
        array_session_.is_multiplier = true;
        array_session_.current_count = count;

        root_.vector_controller().selection().clear();
        for (auto nid : all_new_top_level_ids) {
            root_.vector_controller().selection().select(nid, vector::SelectionMode::Add);
        }
        sync_status_bar();
        return true;
    }
    return false;
}

bool DocumentWorkflow::apply_array_divider(int divisions) {
    if (!array_session_.active || array_session_.source_ids.empty())
        return false;
    if (divisions < 1 || divisions > 1000)
        return false;

    const auto snapshot = root_.document().current_snapshot();

    std::vector<document::ObjectVariant> all_new_objects;
    std::vector<document::ObjectId> all_new_top_level_ids;

    for (int k = 1; k <= divisions; ++k) {
        const double factor = static_cast<double>(k) / static_cast<double>(divisions);
        const auto shift = geometry::Transform2D::translation(
            array_session_.base_delta.x * factor,
            array_session_.base_delta.y * factor
        );
        auto cloned = document::clone_hierarchy(array_session_.source_ids, *snapshot, shift);
        for (auto& obj : cloned.cloned_objects) {
            all_new_objects.push_back(std::move(obj));
        }
        for (auto tid : cloned.top_level_cloned_ids) {
            all_new_top_level_ids.push_back(tid);
        }
    }

    auto cmd = std::make_unique<ReplaceClonedHierarchyCommand>(
        array_session_.current_array_ids,
        std::move(all_new_objects),
        all_new_top_level_ids,
        0,
        "Array Divider (/" + std::to_string(divisions) + ")"
    );

    const auto res = root_.document().execute_command(std::move(cmd));
    if (res.success) {
        array_session_.current_array_ids = all_new_top_level_ids;
        array_session_.is_multiplier = false;
        array_session_.current_count = divisions;

        root_.vector_controller().selection().clear();
        for (auto nid : all_new_top_level_ids) {
            root_.vector_controller().selection().select(nid, vector::SelectionMode::Add);
        }
        sync_status_bar();
        return true;
    }
    return false;
}

void DocumentWorkflow::end_array_session() noexcept {
    array_session_.active = false;
    array_session_.source_ids.clear();
    array_session_.current_array_ids.clear();
    array_session_.base_delta = {0.0, 0.0};
    array_session_.current_count = 1;
}

bool DocumentWorkflow::can_group_selected() const {
    const auto& selected = root_.vector_controller().selection().selected_ids();
    if (selected.size() < 2) return false;

    const auto snapshot = root_.document().current_snapshot();
    const std::unordered_set<document::ObjectId> selected_set(selected.begin(), selected.end());
    if (selected_set.size() != selected.size()) return false;

    size_t owner_layer = snapshot->page().layers.size();
    size_t found = 0;
    for (size_t layer_index = 0; layer_index < snapshot->page().layers.size(); ++layer_index) {
        const auto& layer = snapshot->page().layers[layer_index];
        for (const auto id : layer.object_ids) {
            if (!selected_set.contains(id)) continue;
            if (owner_layer == snapshot->page().layers.size()) owner_layer = layer_index;
            if (owner_layer != layer_index) return false;
            ++found;
        }
    }
    if (found != selected.size()) return false;
    const auto& layer = snapshot->page().layers[owner_layer];
    return !layer.locked && layer.visible;
}

document::ObjectId DocumentWorkflow::group_selected() {
    auto& controller = root_.vector_controller();
    const auto selected = controller.selection().selected_ids();
    if (!can_group_selected())
        return document::kInvalidObjectId;

    const auto valid_ids = selected;

    const auto group_id = document::generate_object_id();
    document::GroupObject grp;
    grp.id = group_id;
    grp.name = "Nhóm (Group " + std::to_string(group_id) + ")";
    grp.children_ids = valid_ids;
    grp.transform = geometry::Transform2D::identity();
    grp.visible = true;
    grp.locked = false;

    auto cmd = std::make_unique<GroupObjectsCommand>(std::move(grp), valid_ids);
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

    auto command = std::make_unique<UngroupObjectsCommand>(groups_to_remove);
    const auto res = root_.document().execute_command(std::move(command));
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

bool DocumentWorkflow::transform_selected(const geometry::Transform2D& delta, const std::string& command_name) {
    if (!delta.is_finite() || !delta.is_invertible())
        return false;
    auto& controller = root_.vector_controller();
    const auto snapshot = root_.document().current_snapshot();
    const auto& selected = controller.selection().selected_ids();
    if (selected.empty())
        return false;

    std::vector<document::ObjectId> targets;
    std::unordered_set<document::ObjectId> seen;
    for (const auto target_id : selected) {
        if (seen.insert(target_id).second) {
            targets.push_back(target_id);
            if (is_group(target_id)) {
                std::vector<document::ObjectId> desc;
                collect_all_descendants(target_id, snapshot->objects(), desc);
                for (const auto cid : desc) {
                    if (seen.insert(cid).second)
                        targets.push_back(cid);
                }
            }
        }
    }
    if (targets.empty())
        return false;

    auto composite = std::make_unique<document::CompositeCommand>(command_name);
    for (const auto target_id : targets) {
        composite->add_command(std::make_unique<document::TransformObjectCommand>(target_id, delta));
    }
    const auto res = root_.document().execute_command(std::move(composite));
    sync_status_bar();
    return res.success;
}

bool DocumentWorkflow::flip_selected_horizontal() {
    auto& controller = root_.vector_controller();
    const auto snapshot = root_.document().current_snapshot();
    const auto bounds = controller.selection().bounds(*snapshot);
    if (!bounds || bounds->width <= 0.0 || bounds->height <= 0.0)
        return false;
    const auto center = bounds->center();
    const auto delta = geometry::Transform2D::translation(center.x, center.y) *
                       geometry::Transform2D::scale(-1.0, 1.0) *
                       geometry::Transform2D::translation(-center.x, -center.y);
    return transform_selected(delta, "Flip Selected Horizontal");
}

bool DocumentWorkflow::flip_selected_vertical() {
    auto& controller = root_.vector_controller();
    const auto snapshot = root_.document().current_snapshot();
    const auto bounds = controller.selection().bounds(*snapshot);
    if (!bounds || bounds->width <= 0.0 || bounds->height <= 0.0)
        return false;
    const auto center = bounds->center();
    const auto delta = geometry::Transform2D::translation(center.x, center.y) *
                       geometry::Transform2D::scale(1.0, -1.0) *
                       geometry::Transform2D::translation(-center.x, -center.y);
    return transform_selected(delta, "Flip Selected Vertical");
}

bool DocumentWorkflow::rotate_selected_degrees(double degrees, std::optional<geometry::Point2D> pivot) {
    if (!std::isfinite(degrees) || std::abs(degrees) < 1e-6)
        return false;
    auto& controller = root_.vector_controller();
    const auto snapshot = root_.document().current_snapshot();
    const auto bounds = controller.selection().bounds(*snapshot);
    if (!bounds || bounds->width <= 0.0 || bounds->height <= 0.0)
        return false;
    const auto pt = pivot.value_or(bounds->center());
    const auto delta = geometry::Transform2D::translation(pt.x, pt.y) *
                       geometry::Transform2D::rotation_degrees(degrees) *
                       geometry::Transform2D::translation(-pt.x, -pt.y);
    return transform_selected(delta, "Rotate Selected");
}

bool DocumentWorkflow::rotate_selected_90_cw(std::optional<geometry::Point2D> pivot) {
    return rotate_selected_degrees(-90.0, pivot);
}

bool DocumentWorkflow::rotate_selected_90_ccw(std::optional<geometry::Point2D> pivot) {
    return rotate_selected_degrees(90.0, pivot);
}

bool DocumentWorkflow::rotate_selected_180(std::optional<geometry::Point2D> pivot) {
    return rotate_selected_degrees(180.0, pivot);
}

namespace {

class SetObjectFillCommand final : public document::DocumentCommand {
public:
    SetObjectFillCommand(std::vector<document::ObjectId> targets, color::ColorValue fill)
        : targets_(std::move(targets)), fill_(std::move(fill)) {}

    std::string_view name() const noexcept override { return "Set Fill"; }

    document::CommandResult execute(document::Document& doc) override {
        if (executed_) return document::CommandResult::fail("Already executed");
        before_objects_ = doc.objects();
        auto objects = doc.objects();
        bool changed = false;
        for (const auto id : targets_) {
            const auto opt = objects.get(id);
            if (!opt) continue;
            auto updated = *opt;
            if (auto* path = std::get_if<document::PathObject>(&updated)) {
                path->fill = fill_;
                path->has_fill = true;
                path->fill_gradient.reset();
                objects = objects.set(id, std::move(updated));
                changed = true;
            } else if (auto* text = std::get_if<document::TextObject>(&updated)) {
                text->fill = fill_;
                objects = objects.set(id, std::move(updated));
                changed = true;
            }
        }
        if (!changed) return document::CommandResult::no_change();
        doc.apply_mutation(doc.page(), std::move(objects));
        executed_ = true;
        return document::CommandResult::ok();
    }

    document::CommandResult undo(document::Document& doc) override {
        if (!executed_ || !before_objects_) return document::CommandResult::fail("No state to undo");
        doc.apply_mutation(doc.page(), *before_objects_);
        executed_ = false;
        return document::CommandResult::ok();
    }

private:
    std::vector<document::ObjectId> targets_;
    color::ColorValue fill_;
    std::optional<document::PersistentObjectMap> before_objects_;
    bool executed_{false};
};

class SetObjectStrokeCommand final : public document::DocumentCommand {
public:
    SetObjectStrokeCommand(std::vector<document::ObjectId> targets, color::ColorValue stroke, double width_mm)
        : targets_(std::move(targets)), stroke_(std::move(stroke)), width_mm_(width_mm) {}

    std::string_view name() const noexcept override { return "Set Stroke"; }

    document::CommandResult execute(document::Document& doc) override {
        if (executed_) return document::CommandResult::fail("Already executed");
        before_objects_ = doc.objects();
        auto objects = doc.objects();
        bool changed = false;
        for (const auto id : targets_) {
            const auto opt = objects.get(id);
            if (!opt) continue;
            auto updated = *opt;
            if (auto* path = std::get_if<document::PathObject>(&updated)) {
                path->stroke = stroke_;
                path->stroke_width_mm = width_mm_;
                path->has_stroke = true;
                objects = objects.set(id, std::move(updated));
                changed = true;
            }
        }
        if (!changed) return document::CommandResult::no_change();
        doc.apply_mutation(doc.page(), std::move(objects));
        executed_ = true;
        return document::CommandResult::ok();
    }

    document::CommandResult undo(document::Document& doc) override {
        if (!executed_ || !before_objects_) return document::CommandResult::fail("No state to undo");
        doc.apply_mutation(doc.page(), *before_objects_);
        executed_ = false;
        return document::CommandResult::ok();
    }

private:
    std::vector<document::ObjectId> targets_;
    color::ColorValue stroke_;
    double width_mm_{0.0};
    std::optional<document::PersistentObjectMap> before_objects_;
    bool executed_{false};
};

} // namespace

bool DocumentWorkflow::set_selected_fill(const std::string& hex_color) {
    const auto ids = root_.vector_controller().selection().selected_ids();
    if (ids.empty())
        return false;

    const auto col = parse_hex_color(hex_color);
    auto& doc = root_.document();
    const auto snapshot = doc.current_snapshot();
    if (!snapshot)
        return false;
    const auto& objects = snapshot->objects();

    std::vector<document::ObjectId> targets;
    for (const auto id : ids) {
        targets.push_back(id);
        collect_all_descendants(id, objects, targets);
    }

    auto cmd = std::make_unique<SetObjectFillCommand>(std::move(targets), col);
    auto res = doc.execute_command(std::move(cmd));
    sync_status_bar();
    return res.success;
}

bool DocumentWorkflow::set_selected_stroke(const std::string& hex_color, double stroke_width_mm) {
    const auto ids = root_.vector_controller().selection().selected_ids();
    if (ids.empty())
        return false;

    const auto col = parse_hex_color(hex_color);
    auto& doc = root_.document();
    const auto snapshot = doc.current_snapshot();
    if (!snapshot)
        return false;
    const auto& objects = snapshot->objects();

    std::vector<document::ObjectId> targets;
    for (const auto id : ids) {
        targets.push_back(id);
        collect_all_descendants(id, objects, targets);
    }

    auto cmd = std::make_unique<SetObjectStrokeCommand>(std::move(targets), col, stroke_width_mm);
    auto res = doc.execute_command(std::move(cmd));
    sync_status_bar();
    return res.success;
}

bool DocumentWorkflow::can_set_selected_object_opacity() const {
    const auto& ids = root_.vector_controller().selection().selected_ids();
    if (ids.empty()) return false;
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot) return false;
    for (const auto id : ids) {
        const auto object = snapshot->objects().get(id);
        if (!object) return false;
        if (std::visit([](const auto& value) { return value.locked; }, *object)) return false;
        if (!std::holds_alternative<document::PathObject>(*object) &&
            !std::holds_alternative<document::TextObject>(*object))
            return false;
    }
    return true;
}

bool DocumentWorkflow::set_selected_object_opacity(double normalized_opacity) {
    if (!std::isfinite(normalized_opacity) || normalized_opacity < 0.0 || normalized_opacity > 1.0 ||
        !can_set_selected_object_opacity())
        return false;

    const auto ids = root_.vector_controller().selection().selected_ids();
    const auto snapshot = root_.document().current_snapshot();
    std::vector<document::AppearanceReplacement> replacements;
    replacements.reserve(ids.size());
    for (const auto id : ids) {
        const auto object = snapshot->objects().get(id);
        if (!object) return false;
        document::Appearance appearance;
        if (const auto* path = std::get_if<document::PathObject>(&*object))
            appearance = path->appearance;
        else if (const auto* text = std::get_if<document::TextObject>(&*object))
            appearance = text->appearance;
        else
            return false;
        appearance.object_opacity = normalized_opacity;
        replacements.push_back({id, std::move(appearance)});
    }

    const auto result = root_.document().execute_command(
        std::make_unique<document::SetAppearanceCommand>(std::move(replacements)));
    sync_status_bar();
    return result.success;
}

bool DocumentWorkflow::can_set_selected_fill_opacity() const {
    return can_edit_selected_appearance();
}

bool DocumentWorkflow::set_selected_fill_opacity(double normalized_opacity) {
    if (!std::isfinite(normalized_opacity) || normalized_opacity < 0.0 ||
        normalized_opacity > 1.0 || !can_set_selected_fill_opacity())
        return false;
    return update_selected_appearance([normalized_opacity](document::Appearance &appearance) {
        appearance.fill_opacity = normalized_opacity;
        return true;
    });
}

bool DocumentWorkflow::can_set_selected_stroke_opacity() const {
    const auto &ids = root_.vector_controller().selection().selected_ids();
    if (ids.empty()) return false;
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot) return false;
    for (const auto id : ids) {
        const auto object = snapshot->objects().get(id);
        const auto *path = object ? std::get_if<document::PathObject>(&*object) : nullptr;
        if (!path || path->locked) return false;
    }
    return true;
}

bool DocumentWorkflow::set_selected_stroke_opacity(double normalized_opacity) {
    if (!std::isfinite(normalized_opacity) || normalized_opacity < 0.0 ||
        normalized_opacity > 1.0 || !can_set_selected_stroke_opacity())
        return false;
    return update_selected_appearance([normalized_opacity](document::Appearance &appearance) {
        appearance.stroke_opacity = normalized_opacity;
        return true;
    });
}

bool DocumentWorkflow::can_edit_selected_appearance() const {
    const auto &ids = root_.vector_controller().selection().selected_ids();
    if (ids.empty()) return false;
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot) return false;
    for (const auto id : ids) {
        const auto object = snapshot->objects().get(id);
        if (!object || std::visit([](const auto &value) { return value.locked; }, *object) ||
            (!std::holds_alternative<document::PathObject>(*object) &&
             !std::holds_alternative<document::TextObject>(*object)))
            return false;
    }
    return true;
}

bool DocumentWorkflow::can_add_selected_effect() const {
    if (!can_edit_selected_appearance()) return false;
    const auto snapshot = root_.document().current_snapshot();
    for (const auto id : root_.vector_controller().selection().selected_ids()) {
        const auto object = snapshot->objects().get(id);
        const auto *appearance = object ? std::visit([](const auto &value) -> const document::Appearance * {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, document::PathObject> ||
                          std::is_same_v<T, document::TextObject>)
                return &value.appearance;
            return nullptr;
        }, *object) : nullptr;
        if (!appearance || appearance->effects.size() >= document::kMaxEffectsPerObject ||
            std::any_of(appearance->effects.begin(), appearance->effects.end(),
                [](const document::EffectNode &node) {
                    const auto *shadow = std::get_if<document::DropShadowEffect>(
                        &node.parameters);
                    return node.enabled && shadow &&
                           shadow->blend_mode != document::BlendMode::Normal;
                }))
            return false;
    }
    return true;
}

bool DocumentWorkflow::can_add_selected_effect(
    const document::EffectParameters& parameters) const {
    if (!valid_effect_parameters(parameters) || !can_add_selected_effect()) return false;
    if (!std::holds_alternative<document::DropShadowEffect>(parameters)) return true;

    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot) return false;
    for (const auto id : root_.vector_controller().selection().selected_ids()) {
        const auto object = snapshot->objects().get(id);
        const auto* appearance = object ? std::visit([](const auto& value) -> const document::Appearance* {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, document::PathObject> ||
                          std::is_same_v<T, document::TextObject>)
                return &value.appearance;
            return nullptr;
        }, *object) : nullptr;
        if (!appearance || std::any_of(appearance->effects.begin(), appearance->effects.end(),
                [](const document::EffectNode& node) {
                    return std::holds_alternative<document::DropShadowEffect>(node.parameters);
                }))
            return false;
    }
    return true;
}

std::optional<size_t> DocumentWorkflow::selected_effect_index(
    const document::EffectParameters& parameters) const {
    if (!can_edit_selected_appearance()) return std::nullopt;
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot) return std::nullopt;

    std::optional<size_t> shared_index;
    for (const auto id : root_.vector_controller().selection().selected_ids()) {
        const auto object = snapshot->objects().get(id);
        const auto* appearance = object ? std::visit([](const auto& value) -> const document::Appearance* {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, document::PathObject> ||
                          std::is_same_v<T, document::TextObject>)
                return &value.appearance;
            return nullptr;
        }, *object) : nullptr;
        if (!appearance) return std::nullopt;

        const auto found = std::find_if(appearance->effects.begin(), appearance->effects.end(),
            [&parameters](const document::EffectNode& node) {
                return node.parameters.index() == parameters.index();
            });
        if (found == appearance->effects.end()) return std::nullopt;
        const size_t index = static_cast<size_t>(found - appearance->effects.begin());
        if (shared_index && *shared_index != index) return std::nullopt;
        shared_index = index;
    }
    if (!shared_index || !can_edit_selected_effect(*shared_index)) return std::nullopt;
    return shared_index;
}

bool DocumentWorkflow::can_activate_selected_effect(
    const document::EffectParameters& parameters) const {
    if (!valid_effect_parameters(parameters) || !can_edit_selected_appearance()) return false;
    if (selected_effect_index(parameters)) return true;

    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot) return false;
    bool matching_effect_found = false;
    std::optional<std::vector<std::tuple<uint32_t, uint32_t, size_t>>> reference_shape;
    for (const auto id : root_.vector_controller().selection().selected_ids()) {
        const auto object = snapshot->objects().get(id);
        const auto* appearance = object ? std::visit([](const auto& value) -> const document::Appearance* {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, document::PathObject> ||
                          std::is_same_v<T, document::TextObject>)
                return &value.appearance;
            return nullptr;
        }, *object) : nullptr;
        if (!appearance) return false;

        matching_effect_found = matching_effect_found || std::any_of(
            appearance->effects.begin(), appearance->effects.end(),
            [&parameters](const document::EffectNode& node) {
                return node.parameters.index() == parameters.index();
            });
        std::vector<std::tuple<uint32_t, uint32_t, size_t>> shape;
        shape.reserve(appearance->effects.size());
        for (const auto& node : appearance->effects)
            shape.emplace_back(node.schema_version, node.schema_minor, node.parameters.index());
        if (!reference_shape) reference_shape = std::move(shape);
        else if (*reference_shape != shape) return false;
    }
    return !matching_effect_found && can_add_selected_effect(parameters);
}

DocumentWorkflow::SelectedEffectActivation DocumentWorkflow::activate_selected_effect(
    document::EffectParameters parameters, size_t& effect_index) {
    if (!can_activate_selected_effect(parameters))
        return SelectedEffectActivation::Unavailable;
    if (const auto existing = selected_effect_index(parameters)) {
        effect_index = *existing;
        return SelectedEffectActivation::Existing;
    }

    const auto snapshot = root_.document().current_snapshot();
    const auto& ids = root_.vector_controller().selection().selected_ids();
    if (!snapshot || ids.empty()) return SelectedEffectActivation::Unavailable;
    const auto object = snapshot->objects().get(ids.front());
    const auto* appearance = object ? std::visit([](const auto& value) -> const document::Appearance* {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, document::PathObject> ||
                      std::is_same_v<T, document::TextObject>)
            return &value.appearance;
        return nullptr;
    }, *object) : nullptr;
    if (!appearance) return SelectedEffectActivation::Unavailable;

    effect_index = appearance->effects.size();
    if (!add_selected_effect(std::move(parameters)))
        return SelectedEffectActivation::Unavailable;
    return SelectedEffectActivation::Added;
}


bool DocumentWorkflow::has_effect_draft() const noexcept {
    return effect_draft_.has_value();
}

std::optional<DocumentWorkflow::EffectDraftView> DocumentWorkflow::effect_draft() const {
    if (!effect_draft_) return std::nullopt;
    return EffectDraftView{effect_draft_->parameters, effect_draft_->preview_enabled,
                           effect_draft_->edit_effect_index.has_value(),
                           effect_draft_->active_field, effect_draft_->numeric_buffer};
}

bool DocumentWorkflow::effect_draft_is_current() const {
    if (!effect_draft_) return false;
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot || snapshot->document_id() != effect_draft_->document_id ||
        snapshot->epoch() != effect_draft_->epoch ||
        snapshot->revision() != effect_draft_->revision ||
        root_.vector_controller().selection().selected_ids() != effect_draft_->target_ids)
        return false;
    if (!effect_draft_->edit_effect_index)
        return can_add_selected_effect(effect_draft_->parameters);
    if (!can_edit_selected_effect(*effect_draft_->edit_effect_index)) return false;
    for (const auto id : effect_draft_->target_ids) {
        const auto object = snapshot->objects().get(id);
        const auto* appearance = object ? std::visit([](const auto& value) -> const document::Appearance* {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, document::PathObject> ||
                          std::is_same_v<T, document::TextObject>)
                return &value.appearance;
            return nullptr;
        }, *object) : nullptr;
        if (!appearance || *effect_draft_->edit_effect_index >= appearance->effects.size() ||
            appearance->effects[*effect_draft_->edit_effect_index].parameters.index() !=
                effect_draft_->parameters.index())
            return false;
    }
    return true;
}

bool DocumentWorkflow::cancel_stale_effect_draft() {
    if (!effect_draft_ || effect_draft_is_current()) return false;
    effect_draft_.reset();
    return true;
}

bool DocumentWorkflow::begin_selected_effect_draft(document::EffectParameters parameters) {
    if (effect_draft_ || !valid_effect_parameters(parameters) ||
        !can_add_selected_effect(parameters))
        return false;
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot) return false;
    EffectDraftState draft;
    draft.parameters = std::move(parameters);
    draft.document_id = snapshot->document_id();
    draft.epoch = snapshot->epoch();
    draft.revision = snapshot->revision();
    draft.target_ids = root_.vector_controller().selection().selected_ids();
    if (draft.target_ids.empty()) return false;
    effect_draft_ = std::move(draft);
    return true;
}

bool DocumentWorkflow::begin_selected_effect_edit_draft(size_t effect_index) {
    if (effect_draft_ || root_.vector_controller().selection().selected_ids().size() != 1 ||
        !can_edit_selected_effect(effect_index))
        return false;
    const auto snapshot = root_.document().current_snapshot();
    const auto& ids = root_.vector_controller().selection().selected_ids();
    if (!snapshot || ids.empty()) return false;
    const auto object = snapshot->objects().get(ids.front());
    const auto* appearance = object ? std::visit([](const auto& value) -> const document::Appearance* {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, document::PathObject> ||
                      std::is_same_v<T, document::TextObject>)
            return &value.appearance;
        return nullptr;
    }, *object) : nullptr;
    if (!appearance || effect_index >= appearance->effects.size()) return false;

    EffectDraftState draft;
    draft.parameters = appearance->effects[effect_index].parameters;
    if (!valid_effect_parameters(draft.parameters)) return false;
    draft.document_id = snapshot->document_id();
    draft.epoch = snapshot->epoch();
    draft.revision = snapshot->revision();
    draft.target_ids = ids;
    draft.edit_effect_index = effect_index;
    effect_draft_ = std::move(draft);
    return true;
}

bool DocumentWorkflow::rebuild_effect_draft_preview() {
    if (!effect_draft_ || !effect_draft_is_current() ||
        !valid_effect_parameters(effect_draft_->parameters))
        return false;
    const auto source = root_.document().current_snapshot();
    if (!source) return false;
    auto objects = source->objects();
    try {
        for (const auto id : effect_draft_->target_ids) {
            auto object = source->objects().get(id);
            if (!object) return false;
            auto *appearance = std::visit([](auto &value) -> document::Appearance * {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, document::PathObject> ||
                              std::is_same_v<T, document::TextObject>)
                    return &value.appearance;
                return nullptr;
            }, *object);
            if (!appearance) return false;
            if (effect_draft_->edit_effect_index) {
                const size_t index = *effect_draft_->edit_effect_index;
                if (index >= appearance->effects.size() ||
                    appearance->effects[index].parameters.index() != effect_draft_->parameters.index())
                    return false;
                appearance->effects[index].parameters = effect_draft_->parameters;
            } else {
                if (appearance->effects.size() >= document::kMaxEffectsPerObject) return false;
                document::EffectNode preview_node;
                preview_node.id = document::generate_effect_instance_id();
                preview_node.parameters = effect_draft_->parameters;
                appearance->effects.push_back(std::move(preview_node));
            }
            objects = objects.set(id, std::move(*object));
        }
        effect_draft_->preview_snapshot = std::make_shared<const document::DocumentSnapshot>(
            source->epoch(), source->revision(), source->page(), std::move(objects),
            source->document_id());
        return true;
    } catch (const std::exception &) {
        effect_draft_->preview_snapshot.reset();
        return false;
    }
}

bool DocumentWorkflow::set_effect_draft_parameters(document::EffectParameters parameters) {
    if (!effect_draft_ || !effect_draft_is_current() ||
        parameters.index() != effect_draft_->parameters.index() ||
        !valid_effect_parameters(parameters))
        return false;
    effect_draft_->parameters = std::move(parameters);
    effect_draft_->preview_snapshot.reset();
    return !effect_draft_->preview_enabled || rebuild_effect_draft_preview();
}

bool DocumentWorkflow::set_effect_draft_preview(bool enabled) {
    if (!effect_draft_ || !effect_draft_is_current()) return false;
    if (!enabled) {
        effect_draft_->preview_enabled = false;
        effect_draft_->preview_snapshot.reset();
        return true;
    }
    if (!rebuild_effect_draft_preview()) return false;
    effect_draft_->preview_enabled = true;
    return true;
}

bool DocumentWorkflow::apply_effect_draft() {
    if (!effect_draft_) return false;
    if (effect_draft_->active_field != EffectDraftField::None &&
        !commit_effect_draft_numeric_input())
        return false;
    if (!effect_draft_is_current()) {
        effect_draft_.reset();
        return false;
    }
    const auto parameters = effect_draft_->parameters;
    const bool applied = effect_draft_->edit_effect_index
        ? set_selected_effect_parameters(*effect_draft_->edit_effect_index, parameters)
        : add_selected_effect(parameters);
    if (!applied) return false;
    effect_draft_.reset();
    return true;
}

void DocumentWorkflow::cancel_effect_draft() noexcept {
    effect_draft_.reset();
}

std::vector<DocumentWorkflow::EffectDraftField>
DocumentWorkflow::effect_draft_numeric_fields() const {
    if (!effect_draft_) return {};
    if (std::holds_alternative<document::GaussianBlurEffect>(effect_draft_->parameters))
        return {EffectDraftField::SigmaX, EffectDraftField::SigmaY};
    return {EffectDraftField::OffsetX, EffectDraftField::OffsetY,
            EffectDraftField::SigmaX, EffectDraftField::SigmaY, EffectDraftField::Opacity};
}

bool DocumentWorkflow::start_effect_draft_numeric_input(EffectDraftField field) {
    if (!effect_draft_ || !effect_draft_is_current()) return false;
    const auto fields = effect_draft_numeric_fields();
    if (std::find(fields.begin(), fields.end(), field) == fields.end()) return false;
    effect_draft_->active_field = field;
    effect_draft_->numeric_buffer.clear();
    return true;
}

bool DocumentWorkflow::focus_next_effect_draft_numeric_field() {
    if (!effect_draft_ || effect_draft_->active_field == EffectDraftField::None) return false;
    const auto fields = effect_draft_numeric_fields();
    const auto found = std::find(fields.begin(), fields.end(), effect_draft_->active_field);
    if (found == fields.end() || fields.empty()) return false;
    const size_t current = static_cast<size_t>(found - fields.begin());
    effect_draft_->active_field = fields[(current + 1) % fields.size()];
    effect_draft_->numeric_buffer.clear();
    return true;
}

bool DocumentWorkflow::effect_draft_numeric_input_active() const noexcept {
    return effect_draft_ && effect_draft_->active_field != EffectDraftField::None;
}

bool DocumentWorkflow::set_effect_draft_numeric_value(EffectDraftField field, double value) {
    if (!effect_draft_ || !effect_draft_is_current() || !std::isfinite(value)) return false;
    auto parameters = effect_draft_->parameters;
    if (auto *blur = std::get_if<document::GaussianBlurEffect>(&parameters)) {
        if (field == EffectDraftField::SigmaX) blur->sigma_x_mm = value;
        else if (field == EffectDraftField::SigmaY) blur->sigma_y_mm = value;
        else return false;
    } else if (auto *shadow = std::get_if<document::DropShadowEffect>(&parameters)) {
        if (field == EffectDraftField::OffsetX) shadow->dx_mm = value;
        else if (field == EffectDraftField::OffsetY) shadow->dy_mm = value;
        else if (field == EffectDraftField::SigmaX) shadow->sigma_x_mm = value;
        else if (field == EffectDraftField::SigmaY) shadow->sigma_y_mm = value;
        else if (field == EffectDraftField::Opacity) shadow->opacity = value / 100.0;
        else return false;
    }
    if (!valid_effect_parameters(parameters)) return false;
    effect_draft_->parameters = std::move(parameters);
    effect_draft_->preview_snapshot.reset();
    return !effect_draft_->preview_enabled || rebuild_effect_draft_preview();
}

bool DocumentWorkflow::input_effect_draft_character(char character) {
    if (!effect_draft_ || !effect_draft_is_current() ||
        effect_draft_->active_field == EffectDraftField::None ||
        effect_draft_->numeric_buffer.size() >= 32)
        return false;
    const bool offset = effect_draft_->active_field == EffectDraftField::OffsetX ||
                        effect_draft_->active_field == EffectDraftField::OffsetY;
    const bool sign = character == '-' && offset && effect_draft_->numeric_buffer.empty();
    if (!sign && character != '.' && character != ',' && (character < '0' || character > '9'))
        return false;
    effect_draft_->numeric_buffer.push_back(character);
    if (const auto value = parse_effect_draft_number(effect_draft_->numeric_buffer))
        (void)set_effect_draft_numeric_value(effect_draft_->active_field, *value);
    return true;
}

bool DocumentWorkflow::backspace_effect_draft_numeric_input() {
    if (!effect_draft_ || effect_draft_->active_field == EffectDraftField::None ||
        effect_draft_->numeric_buffer.empty())
        return false;
    effect_draft_->numeric_buffer.pop_back();
    if (const auto value = parse_effect_draft_number(effect_draft_->numeric_buffer))
        (void)set_effect_draft_numeric_value(effect_draft_->active_field, *value);
    return true;
}

bool DocumentWorkflow::commit_effect_draft_numeric_input() {
    if (!effect_draft_ || effect_draft_->active_field == EffectDraftField::None) return false;
    if (effect_draft_->numeric_buffer.empty()) {
        effect_draft_->active_field = EffectDraftField::None;
        return true;
    }
    const auto value = parse_effect_draft_number(effect_draft_->numeric_buffer);
    if (!value || !set_effect_draft_numeric_value(effect_draft_->active_field, *value)) return false;
    effect_draft_->active_field = EffectDraftField::None;
    effect_draft_->numeric_buffer.clear();
    return true;
}

bool DocumentWorkflow::handle_effect_draft_keydown(uint32_t key) {
    if (!effect_draft_) return false;
    if (key == 0x1b) {
        cancel_effect_draft();
        return true;
    }
    if (key == 0x08 && effect_draft_numeric_input_active())
        return backspace_effect_draft_numeric_input();
    if (key == 0x09 && effect_draft_numeric_input_active())
        return focus_next_effect_draft_numeric_field();
    if (key == 0x0d) {
        if (effect_draft_numeric_input_active())
            return commit_effect_draft_numeric_input();
        return apply_effect_draft();
    }
    return false;
}

bool DocumentWorkflow::can_clear_selected_effects() const {
    if (!can_edit_selected_appearance()) return false;
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot) return false;
    for (const auto id : root_.vector_controller().selection().selected_ids()) {
        const auto object = snapshot->objects().get(id);
        const auto *appearance = object ? std::visit([](const auto &value) -> const document::Appearance * {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, document::PathObject> ||
                          std::is_same_v<T, document::TextObject>)
                return &value.appearance;
            return nullptr;
        }, *object) : nullptr;
        if (appearance && !appearance->effects.empty()) return true;
    }
    return false;
}

bool DocumentWorkflow::can_edit_selected_effect(size_t effect_index) const {
    if (!can_edit_selected_appearance()) return false;
    const auto snapshot = root_.document().current_snapshot();
    std::vector<std::tuple<uint32_t, uint32_t, size_t>> reference_shape;
    bool have_reference = false;
    for (const auto id : root_.vector_controller().selection().selected_ids()) {
        const auto object = snapshot->objects().get(id);
        const auto *appearance = object ? std::visit([](const auto &value) -> const document::Appearance * {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, document::PathObject> ||
                          std::is_same_v<T, document::TextObject>)
                return &value.appearance;
            return nullptr;
        }, *object) : nullptr;
        if (!appearance || effect_index >= appearance->effects.size()) return false;
        std::vector<std::tuple<uint32_t, uint32_t, size_t>> current_shape;
        current_shape.reserve(appearance->effects.size());
        for (const auto &node : appearance->effects)
            current_shape.emplace_back(node.schema_version, node.schema_minor,
                                        node.parameters.index());
        if (!have_reference) {
            reference_shape = std::move(current_shape);
            have_reference = true;
        } else if (current_shape != reference_shape) {
            return false;
        }
    }
    return have_reference;
}

bool DocumentWorkflow::update_selected_appearance(
    const std::function<bool(document::Appearance &)> &update) {
    if (!update || !can_edit_selected_appearance()) return false;
    const auto ids = root_.vector_controller().selection().selected_ids();
    const auto snapshot = root_.document().current_snapshot();
    std::vector<document::AppearanceReplacement> replacements;
    replacements.reserve(ids.size());
    for (const auto id : ids) {
        const auto object = snapshot->objects().get(id);
        if (!object) return false;
        document::Appearance appearance;
        if (const auto *path = std::get_if<document::PathObject>(&*object))
            appearance = path->appearance;
        else if (const auto *text = std::get_if<document::TextObject>(&*object))
            appearance = text->appearance;
        else
            return false;
        if (!update(appearance)) return false;
        replacements.push_back({id, std::move(appearance)});
    }
    const auto result = root_.document().execute_command(
        std::make_unique<document::SetAppearanceCommand>(std::move(replacements)));
    sync_status_bar();
    return result.success;
}

bool DocumentWorkflow::set_selected_blend_mode(document::BlendMode mode) {
    switch (mode) {
    case document::BlendMode::Normal:
    case document::BlendMode::Multiply:
    case document::BlendMode::Screen:
    case document::BlendMode::Overlay:
    case document::BlendMode::Darken:
    case document::BlendMode::Lighten:
        break;
    default:
        return false;
    }
    return update_selected_appearance([mode](document::Appearance &appearance) {
        appearance.blend_mode = mode;
        return true;
    });
}

bool DocumentWorkflow::add_selected_effect(document::EffectParameters parameters) {
    if (!can_add_selected_effect(parameters)) return false;
    return update_selected_appearance([parameters = std::move(parameters)](
                                          document::Appearance &appearance) {
        document::EffectNode node;
        node.id = document::generate_effect_instance_id();
        node.parameters = parameters;
        if (const auto *shadow =
                std::get_if<document::DropShadowEffect>(&node.parameters);
            shadow && shadow->blend_mode != document::BlendMode::Normal)
            node.schema_minor = document::kDropShadowBlendSchemaMinor;
        appearance.effects.push_back(std::move(node));
        return true;
    });
}

bool DocumentWorkflow::clear_selected_effects() {
    if (!can_clear_selected_effects()) return false;
    return update_selected_appearance([](document::Appearance &appearance) {
        if (appearance.effects.empty()) return true;
        appearance.effects.clear();
        return true;
    });
}

bool DocumentWorkflow::remove_selected_effect(size_t effect_index) {
    if (!can_edit_selected_effect(effect_index)) return false;
    return update_selected_appearance([effect_index](document::Appearance &appearance) {
        if (effect_index >= appearance.effects.size()) return false;
        appearance.effects.erase(appearance.effects.begin() + static_cast<std::ptrdiff_t>(effect_index));
        return true;
    });
}

bool DocumentWorkflow::can_move_selected_effect(size_t effect_index,
                                                int direction) const {
    if ((direction != -1 && direction != 1) ||
        !can_edit_selected_effect(effect_index))
        return false;
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot) return false;
    for (const auto id : root_.vector_controller().selection().selected_ids()) {
        const auto object = snapshot->objects().get(id);
        const auto *appearance = object ? std::visit(
            [](const auto &value) -> const document::Appearance * {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, document::PathObject> ||
                              std::is_same_v<T, document::TextObject>)
                    return &value.appearance;
                return nullptr;
            }, *object) : nullptr;
        if (!appearance || effect_index >= appearance->effects.size())
            return false;
        auto reordered = *appearance;
        const auto destination = static_cast<std::ptrdiff_t>(effect_index) + direction;
        if (destination < 0 ||
            destination >= static_cast<std::ptrdiff_t>(reordered.effects.size()))
            return false;
        std::swap(reordered.effects[effect_index],
                  reordered.effects[static_cast<size_t>(destination)]);
        try {
            document::validate_appearance(reordered);
        } catch (...) {
            return false;
        }
    }
    return true;
}

bool DocumentWorkflow::move_selected_effect(size_t effect_index, int direction) {
    if (!can_move_selected_effect(effect_index, direction)) return false;
    return update_selected_appearance([effect_index, direction](document::Appearance &appearance) {
        const auto destination = static_cast<std::ptrdiff_t>(effect_index) + direction;
        if (destination < 0 || destination >= static_cast<std::ptrdiff_t>(appearance.effects.size()))
            return false;
        std::swap(appearance.effects[effect_index], appearance.effects[static_cast<size_t>(destination)]);
        return true;
    });
}

bool DocumentWorkflow::set_selected_effect_enabled(size_t effect_index, bool enabled) {
    if (!can_edit_selected_effect(effect_index)) return false;
    return update_selected_appearance([effect_index, enabled](document::Appearance &appearance) {
        if (effect_index >= appearance.effects.size()) return false;
        appearance.effects[effect_index].enabled = enabled;
        return true;
    });
}

bool DocumentWorkflow::set_selected_effect_sigma(size_t effect_index, bool x_axis,
                                                  double sigma_mm) {
    if (!std::isfinite(sigma_mm) || sigma_mm < 0.0 ||
        sigma_mm > geometry::limits::kCanvasMaxMm ||
        !can_edit_selected_effect(effect_index))
        return false;
    return update_selected_appearance([effect_index, x_axis, sigma_mm](
                                          document::Appearance &appearance) {
        if (effect_index >= appearance.effects.size()) return false;
        auto &parameters = appearance.effects[effect_index].parameters;
        if (auto *blur = std::get_if<document::GaussianBlurEffect>(&parameters)) {
            (x_axis ? blur->sigma_x_mm : blur->sigma_y_mm) = sigma_mm;
            return true;
        }
        if (auto *shadow = std::get_if<document::DropShadowEffect>(&parameters)) {
            (x_axis ? shadow->sigma_x_mm : shadow->sigma_y_mm) = sigma_mm;
            return true;
        }
        return false;
    });
}

bool DocumentWorkflow::set_selected_effect_offset(size_t effect_index, bool x_axis,
                                                   double offset_mm) {
    if (!std::isfinite(offset_mm) || std::abs(offset_mm) > geometry::limits::kCanvasMaxMm ||
        !can_edit_selected_effect(effect_index))
        return false;
    return update_selected_appearance([effect_index, x_axis, offset_mm](
                                          document::Appearance &appearance) {
        if (effect_index >= appearance.effects.size()) return false;
        auto *shadow = std::get_if<document::DropShadowEffect>(
            &appearance.effects[effect_index].parameters);
        if (!shadow) return false;
        (x_axis ? shadow->dx_mm : shadow->dy_mm) = offset_mm;
        return true;
    });
}

bool DocumentWorkflow::set_selected_effect_parameters(
    size_t effect_index, document::EffectParameters parameters) {
    if (!valid_effect_parameters(parameters) || !can_edit_selected_effect(effect_index))
        return false;
    return update_selected_appearance([effect_index, parameters = std::move(parameters)](
                                          document::Appearance& appearance) {
        if (effect_index >= appearance.effects.size() ||
            appearance.effects[effect_index].parameters.index() != parameters.index())
            return false;
        if (const auto *shadow = std::get_if<document::DropShadowEffect>(&parameters);
            shadow && shadow->blend_mode != document::BlendMode::Normal)
            appearance.effects[effect_index].schema_minor =
                document::kDropShadowBlendSchemaMinor;
        appearance.effects[effect_index].parameters = parameters;
        return true;
    });
}

bool DocumentWorkflow::can_set_selected_effect_blend_mode(
    size_t effect_index, document::BlendMode mode) const {
    if (static_cast<uint8_t>(mode) >
            static_cast<uint8_t>(document::BlendMode::Lighten) ||
        !can_edit_selected_effect(effect_index))
        return false;
    const auto snapshot = root_.document().current_snapshot();
    if (!snapshot) return false;
    for (const auto id : root_.vector_controller().selection().selected_ids()) {
        const auto object = snapshot->objects().get(id);
        const auto *appearance = object ? std::visit(
            [](const auto &value) -> const document::Appearance * {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, document::PathObject> ||
                              std::is_same_v<T, document::TextObject>)
                    return &value.appearance;
                return nullptr;
            }, *object) : nullptr;
        if (!appearance || effect_index >= appearance->effects.size())
            return false;
        const auto &node = appearance->effects[effect_index];
        if (!std::holds_alternative<document::DropShadowEffect>(node.parameters))
            return false;
        if (mode != document::BlendMode::Normal && node.enabled) {
            for (size_t later = effect_index + 1;
                 later < appearance->effects.size(); ++later) {
                if (appearance->effects[later].enabled) return false;
            }
        }
    }
    return true;
}

bool DocumentWorkflow::set_selected_effect_blend_mode(
    size_t effect_index, document::BlendMode mode) {
    if (!can_set_selected_effect_blend_mode(effect_index, mode)) return false;
    return update_selected_appearance([effect_index, mode](
                                          document::Appearance &appearance) {
        if (effect_index >= appearance.effects.size()) return false;
        auto &node = appearance.effects[effect_index];
        auto *shadow = std::get_if<document::DropShadowEffect>(&node.parameters);
        if (!shadow) return false;
        shadow->blend_mode = mode;
        if (mode != document::BlendMode::Normal ||
            node.schema_minor == document::kDropShadowBlendSchemaMinor)
            node.schema_minor = document::kDropShadowBlendSchemaMinor;
        return true;
    });
}

bool DocumentWorkflow::set_selected_effect_opacity(size_t effect_index,
                                                    double normalized_opacity) {
    if (!std::isfinite(normalized_opacity) || normalized_opacity < 0.0 ||
        normalized_opacity > 1.0 || !can_edit_selected_effect(effect_index))
        return false;
    return update_selected_appearance([effect_index, normalized_opacity](
                                          document::Appearance &appearance) {
        if (effect_index >= appearance.effects.size()) return false;
        auto *shadow = std::get_if<document::DropShadowEffect>(
            &appearance.effects[effect_index].parameters);
        if (!shadow) return false;
        shadow->opacity = normalized_opacity;
        return true;
    });
}

bool DocumentWorkflow::set_selected_effect_color(size_t effect_index,
                                                 color::ColorValue color) {
    if (!color.is_valid() || !can_edit_selected_effect(effect_index)) return false;
    return update_selected_appearance([effect_index, color = std::move(color)](
                                          document::Appearance &appearance) {
        if (effect_index >= appearance.effects.size()) return false;
        auto *shadow = std::get_if<document::DropShadowEffect>(
            &appearance.effects[effect_index].parameters);
        if (!shadow) return false;
        shadow->color = color;
        return true;
    });
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
        target = detail::path_from_utf8(file_path);
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        temporary = target;
        temporary += ".tmp-" + std::to_string(nonce);

        const auto export_result = interop::SvgExporter::export_with_report(
            *root_.document().current_snapshot());
        if (!export_result.success || export_result.svg.empty())
            return false;
        const auto& contents = export_result.svg;
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

bool DocumentWorkflow::can_boolean_operation_selected(geometry::BooleanOp op) const {
    const auto& selected = root_.vector_controller().selection().selected_ids();
    if (selected.size() != 2) return false;
    const auto snapshot = root_.document().current_snapshot();
    return compute_selected_boolean(*snapshot, selected, op).has_value();
}

bool DocumentWorkflow::boolean_operation_selected(
    geometry::BooleanOp op,
    bool keep_source,
    bool keep_target
) {
    const auto& sel = root_.vector_controller().selection().selected_ids();
    if (!can_boolean_operation_selected(op)) return false;

    const auto snapshot = root_.document().current_snapshot();
    const auto target_id = sel[0];
    const auto source_id = sel[1];

    const auto target_var = snapshot->objects().get(target_id);
    if (!target_var) return false;
    const auto* target = std::get_if<document::PathObject>(&*target_var);
    if (!target) return false;

    auto result = compute_selected_boolean(*snapshot, sel, op);
    if (!result) return false;

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
    new_obj.path = std::move(result->path);
    new_obj.transform = geometry::Transform2D::identity();
    new_obj.fill = target->fill;
    new_obj.stroke = target->stroke;
    new_obj.stroke_width_mm = target->stroke_width_mm;
    new_obj.has_fill = target->has_fill;
    new_obj.has_stroke = target->has_stroke;

    const auto new_id = new_obj.id;
    cmds.push_back(std::make_unique<document::AddObjectCommand>(std::move(new_obj)));

    auto composite = std::make_unique<document::CompositeCommand>("Boolean Operation");
    for (auto& cmd : cmds) {
        composite->add_command(std::move(cmd));
    }
    const auto command_result = root_.document().execute_command(std::move(composite));
    if (!command_result.success) return false;

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
        (std::max)(ib.x, vb->x),
        (std::max)(ib.y, vb->y),
        (std::min)(ib.x + ib.width, vb->x + vb->width) - (std::max)(ib.x, vb->x),
        (std::min)(ib.y + ib.height, vb->y + vb->height) - (std::max)(ib.y, vb->y)
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

document::SnapshotPtr DocumentWorkflow::snapshot_for_canvas() {
    (void)cancel_stale_effect_draft();
    if (effect_draft_ && effect_draft_->preview_enabled && effect_draft_->preview_snapshot)
        return effect_draft_->preview_snapshot;
    return root_.document().current_snapshot();
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
    const auto snapshot = snapshot_for_canvas();
    if (!snapshot) { out_buffer.clear(); return; }
    root_.renderer().render_frame(*snapshot, target);
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
