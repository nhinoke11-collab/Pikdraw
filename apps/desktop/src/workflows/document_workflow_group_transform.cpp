#include "pikdraw/desktop/workflows/document_workflow.hpp"
#include "document_workflow_internal.hpp"
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
using namespace detail;

namespace {

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
        const auto snapshot=doc.current_snapshot();
        for(auto id:snapshot->hierarchy().roots(groups_)) {
            const auto* entry=snapshot->hierarchy().find(id);
            const auto object=objects.get(id);
            const auto* group=object?std::get_if<document::GroupObject>(&*object):nullptr;
            if(!entry||!group||group->clip||!snapshot->hierarchy().editable(id))
                return document::CommandResult::fail("Use Release Clip for PowerClip containers");
            auto siblings=entry->parent?std::get<document::GroupObject>(*objects.get(entry->parent)).children_ids
                :page.layers[entry->layer].object_ids;
            const auto at=std::find(siblings.begin(),siblings.end(),id);
            const auto index=std::distance(siblings.begin(),at);
            siblings.erase(at);siblings.insert(siblings.begin()+index,group->children_ids.begin(),group->children_ids.end());
            for(auto child_id:group->children_ids) {
                auto child=*objects.get(child_id);
                std::visit([&](auto& value){value.transform=group->transform*value.transform;},child);
                objects=objects.set(child_id,child);
            }
            if(entry->parent){auto parent=std::get<document::GroupObject>(*objects.get(entry->parent));parent.children_ids=siblings;objects=objects.set(parent.id,parent);}
            else page.layers[entry->layer].object_ids=siblings;
            objects=objects.remove(id);
        }

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

bool DocumentWorkflow::can_group_selected() const {
    const auto snapshot=root_.document().current_snapshot();
    const auto& ids=root_.vector_controller().selection().selected_ids();
    if(ids.size()<2||snapshot->is_content_locked())return false;
    const auto* owner=snapshot->hierarchy().find(ids.front());if(!owner)return false;
    std::unordered_set<document::ObjectId> unique;
    for(auto id:ids) {
        const auto* entry=snapshot->hierarchy().find(id);
        if(!entry||entry->parent!=owner->parent||entry->layer!=owner->layer||entry->mask||
            !unique.insert(id).second||!snapshot->hierarchy().editable(id))return false;
    }
    return true;
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
    grp.name = "Nh├│m (Group " + std::to_string(group_id) + ")";
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
    const auto snapshot=root_.document().current_snapshot();
    const auto* entry=snapshot->hierarchy().find(child_id);
    return entry && entry->parent?std::optional(entry->parent):std::nullopt;
}
std::optional<geometry::Rect2D> DocumentWorkflow::get_object_bounds(document::ObjectId id) const {
    const auto snapshot=root_.document().current_snapshot();
    const auto* entry=snapshot->hierarchy().find(id);
    return entry?entry->bounds:std::nullopt;
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

    auto cmd = vector::make_transformed_object_command(id, *snapshot, delta);
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

    auto cmd = vector::make_transformed_object_command(id, *snapshot, delta);
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

    const auto targets = snapshot->hierarchy().roots(selected);
    if (targets.empty())
        return false;

    auto composite = std::make_unique<document::CompositeCommand>(command_name);
    for (const auto target_id : targets) {
        composite->add_command(vector::make_transformed_object_command(target_id, *snapshot, delta));
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

} // namespace pikdraw::desktop
