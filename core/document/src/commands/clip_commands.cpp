#include "pikdraw/document/commands/clip_commands.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/document/hierarchy/frame_geometry.hpp"
#include "pikdraw/document/model/clone_utils.hpp"
#include <algorithm>
#include <unordered_set>

namespace pikdraw::document {
namespace {
using geometry::Transform2D;
using geometry::Rect2D;

bool replace_owner(Page& page, PersistentObjectMap& objects, ObjectId parent, size_t layer,
                   const std::vector<ObjectId>& children) {
    if (!parent) { page.layers.at(layer).object_ids = children; return true; }
    auto object = objects.get(parent);
    auto* group = object ? std::get_if<GroupObject>(&*object) : nullptr;
    if (!group) return false;
    group->children_ids = children;
    if (group->clip) group->clip->is_empty_frame = children.size() == 1;
    objects = objects.set(parent, *object);
    return true;
}
std::vector<ObjectId> owner_children(const Page& page, const PersistentObjectMap& objects,
                                    ObjectId parent, size_t layer) {
    if (!parent) return page.layers.at(layer).object_ids;
    return std::get<GroupObject>(*objects.get(parent)).children_ids;
}
void transform(PersistentObjectMap& objects, ObjectId id, const Transform2D& delta) {
    auto object = *objects.get(id);
    std::visit([&](auto& value) { value.transform = delta * value.transform; }, object);
    objects = objects.set(id, object);
}
std::optional<Rect2D> local_bounds(const PersistentObjectMap& objects, ObjectId id,
                                 const Transform2D& parent) {
    const auto* object = objects.lookup_ref(id);
    if (!object) return {};
    if (const auto* group = std::get_if<GroupObject>(object)) {
        std::optional<Rect2D> bounds;
        for (auto child : group->children_ids) {
            if (group->clip && child != group->clip->mask_object_id) continue;
            const auto b = local_bounds(objects, child, parent * group->transform);
            if (b) bounds = bounds ? bounds->united(*b) : b;
        }
        return bounds;
    }
    return std::visit([&](const auto& value) -> std::optional<Rect2D> {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, ImageObject>)
            return (parent * value.transform).map_rect(value.bounds_rect);
        else if constexpr (!std::is_same_v<T, GroupObject>) {
            auto copy = value;
            copy.transform = parent * value.transform;
            return copy.bounds();
        }
        return {};
    }, *object);
}
bool fit_contents(PersistentObjectMap& objects, GroupObject& group, ClipContentFitMode mode) {
    if (mode == ClipContentFitMode::CurrentPosition) { group.clip->fit_mode = mode; return true; }
    const auto mask = frame_geometry(*objects.get(group.clip->mask_object_id));
    if (!mask) return false;
    const auto inverse = mask->transform.inverted();
    if (!inverse) return false;
    const auto frame = mask->path.bounds();
    std::optional<Rect2D> content;
    for (auto id : group.children_ids) {
        if (id == group.clip->mask_object_id) continue;
        auto bounds = local_bounds(objects, id, *inverse);
        if (bounds) content = content ? content->united(*bounds) : bounds;
    }
    if (!content || frame.width <= 0 || frame.height <= 0 || content->width <= 0 || content->height <= 0)
        return false;
    double sx = 1, sy = 1;
    if (mode != ClipContentFitMode::CenterContents) {
        sx = frame.width / content->width; sy = frame.height / content->height;
        if (mode == ClipContentFitMode::FitProportionally) sx = sy = std::min(sx, sy);
        if (mode == ClipContentFitMode::FillProportionally) sx = sy = std::max(sx, sy);
    }
    const auto fc = frame.center(), cc = content->center();
    const auto delta = mask->transform * Transform2D::translation(fc.x, fc.y) *
        Transform2D::scale(sx, sy) * Transform2D::translation(-cc.x, -cc.y) * *inverse;
    if (!delta.is_finite() || !delta.is_invertible()) return false;
    for (auto id : group.children_ids)
        if (id != group.clip->mask_object_id) transform(objects, id, delta);
    group.clip->fit_mode = mode;
    return true;
}
} // namespace

ClipCommand::ClipCommand(ClipOperation operation, ObjectId frame, std::vector<ObjectId> contents,
                         ClipContentFitMode fit, bool flag)
    : operation_(operation), frame_(frame), result_id_(generate_object_id()),
      contents_(std::move(contents)), fit_(fit), flag_(flag) {}

CommandResult ClipCommand::execute(Document& doc) {
    if (after_page_ && after_objects_) {
        doc.apply_mutation(*after_page_, *after_objects_);
        return CommandResult::ok();
    }
    const auto snapshot = doc.current_snapshot();
    const auto* owner = snapshot->hierarchy().find(frame_);
    if (snapshot->is_content_locked() || !owner || !snapshot->hierarchy().editable(frame_) || owner->mask)
        return CommandResult::fail("Frame is missing, hidden, locked or is already a mask");
    auto page = doc.page();
    auto objects = doc.objects();
    auto frame = objects.get(frame_);
    GroupObject group;
    bool existing = false;
    if (const auto* value = std::get_if<GroupObject>(&*frame); value && value->clip) {
        group = *value; existing = true;
    }
    if (existing && ((operation_ == ClipOperation::Fit && fit_ == ClipContentFitMode::CurrentPosition) ||
        (operation_ == ClipOperation::SetLock && group.clip->lock_contents_to_frame == flag_) ||
        (operation_ == ClipOperation::SetAppearance && group.clip->preserve_frame_appearance == flag_)))
        return CommandResult::no_change();
    if (operation_ == ClipOperation::Place || operation_ == ClipOperation::CopyContents) {
        auto ids = snapshot->hierarchy().roots(contents_);
        if (ids.size() != contents_.size()) return CommandResult::fail("Overlapping content selection");
        if (operation_ == ClipOperation::CopyContents && !existing)
            return CommandResult::fail("Copy target must be a PowerClip frame");
        for (auto id : ids) {
            const auto* entry = snapshot->hierarchy().find(id);
            if (!entry || !snapshot->hierarchy().editable(id) || entry->mask ||
                id == frame_ || snapshot->hierarchy().contains(id, frame_))
                return CommandResult::fail("Invalid content or cyclic ownership");
        }
        if (!existing) {
            if (!frame_geometry(*frame)) return CommandResult::fail("Unsupported frame geometry");
            group.id = result_id_; group.name = "PowerClip";
            group.children_ids = {frame_};
            group.clip = ClipProperties{};
            group.clip->mask_object_id = frame_;
            group.clip->preserve_frame_appearance = flag_;
            auto siblings = owner_children(page, objects, owner->parent, owner->layer);
            std::replace(siblings.begin(), siblings.end(), frame_, group.id);
            replace_owner(page, objects, owner->parent, owner->layer, siblings);
        }
        const auto group_world = existing ? owner->world : owner->parent_world;
        const auto inverse = group_world.inverted();
        if (!inverse) return CommandResult::fail("Singular frame transform");
        if (operation_ == ClipOperation::CopyContents) {
            // Clone once, with stable IDs retained for Redo by the command's saved result.
            const auto cloned = clone_hierarchy(ids, *snapshot, {});
            for (const auto& object : cloned.cloned_objects)
                objects = objects.set(std::visit([](const auto& value) { return value.id; }, object), object);
            for (size_t i = 0; i < ids.size(); ++i) {
                const auto* entry = snapshot->hierarchy().find(ids[i]);
                transform(objects, cloned.top_level_cloned_ids[i], *inverse * entry->parent_world);
                group.children_ids.push_back(cloned.top_level_cloned_ids[i]);
            }
        } else {
            for (auto id : ids) {
                const auto* entry = snapshot->hierarchy().find(id);
                auto siblings = owner_children(page, objects, entry->parent, entry->layer);
                std::erase(siblings, id);
                replace_owner(page, objects, entry->parent, entry->layer, siblings);
                transform(objects, id, *inverse * entry->parent_world);
                std::erase(group.children_ids,id);
                group.children_ids.push_back(id);
            }
        }
        group.clip->is_empty_frame = group.children_ids.size() == 1;
        if (!fit_contents(objects, group, fit_)) return CommandResult::fail("Cannot fit this frame/content geometry");
    } else {
        if (!existing) return CommandResult::fail("PowerClip frame not found");
        if (operation_ == ClipOperation::Fit) {
            if (!fit_contents(objects, group, fit_)) return CommandResult::fail("Cannot fit empty or singular content");
        } else if (operation_ == ClipOperation::SetLock) group.clip->lock_contents_to_frame = flag_;
        else if (operation_ == ClipOperation::SetAppearance) group.clip->preserve_frame_appearance = flag_;
        else {
            auto siblings = owner_children(page, objects, owner->parent, owner->layer);
            auto at = std::find(siblings.begin(), siblings.end(), frame_);
            std::vector<ObjectId> released;
            for (auto id : group.children_ids) {
                if (operation_ == ClipOperation::Extract && id == group.clip->mask_object_id) continue;
                transform(objects, id, group.transform);
                released.push_back(id);
            }
            if (operation_ == ClipOperation::Release) at = siblings.erase(at);
            else ++at;
            siblings.insert(at, released.begin(), released.end());
            replace_owner(page, objects, owner->parent, owner->layer, siblings);
            if (operation_ == ClipOperation::Release) objects = objects.remove(frame_);
            else {
                group.children_ids = {group.clip->mask_object_id};
                group.clip->is_empty_frame = true;
            }
        }
    }
    if (operation_ != ClipOperation::Release) objects = objects.set(group.id, group);
    before_page_ = doc.page(); before_objects_ = doc.objects();
    after_page_ = page; after_objects_ = objects;
    doc.apply_mutation(std::move(page), std::move(objects));
    result_id_ = group.id;
    return CommandResult::ok();
}
CommandResult ClipCommand::undo(Document& doc) {
    if (!before_page_ || !before_objects_) return CommandResult::fail("No PowerClip state to restore");
    doc.apply_mutation(*before_page_, *before_objects_);
    return CommandResult::ok();
}
CommandResult AddScopedObjectCommand::execute(Document& doc) {
    auto page = doc.page(); auto objects = doc.objects();
    const auto snapshot = doc.current_snapshot();
    const auto id = std::visit([](const auto& value) { return value.id; }, object_);
    if (snapshot->is_content_locked() || objects.contains(id)) return CommandResult::fail("Invalid new object");
    auto object = object_;
    if (owner_) {
        const auto* entry = snapshot->hierarchy().find(owner_);
        auto group_object = objects.get(owner_);
        auto* group = group_object ? std::get_if<GroupObject>(&*group_object) : nullptr;
        if (!entry || !snapshot->hierarchy().editable(owner_) || !group) return CommandResult::fail("Invalid scope");
        const auto inverse = entry->world.inverted();
        if (!inverse) return CommandResult::fail("Singular scope");
        std::visit([&](auto& value) { value.transform = *inverse * value.transform; }, object);
        group->children_ids.push_back(id);
        if (group->clip) group->clip->is_empty_frame = false;
        objects = objects.set(owner_, *group_object);
    } else {
        if (layer_ >= page.layers.size() || page.layers[layer_].locked || !page.layers[layer_].visible)
            return CommandResult::fail("Invalid destination layer");
        page.layers[layer_].object_ids.push_back(id);
    }
    before_page_ = doc.page(); before_objects_ = doc.objects();
    doc.apply_mutation(std::move(page), objects.set(id, object));
    return CommandResult::ok();
}
CommandResult AddScopedObjectCommand::undo(Document& doc) {
    if (!before_page_ || !before_objects_) return CommandResult::fail("No scoped object to restore");
    doc.apply_mutation(*before_page_, *before_objects_); return CommandResult::ok();
}
} // namespace pikdraw::document
