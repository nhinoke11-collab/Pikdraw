#pragma once

#include "pikdraw/automation/automation_service.hpp"
#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/model/path_adapter.hpp"
#include "pikdraw/geometry/primitives/validation.hpp"
#include "pikdraw/typography/layout/text_layout.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <variant>
#include <vector>

namespace pikdraw::automation::detail {

std::optional<size_t> root_layer_index(const document::DocumentSnapshot& snapshot,
                                       uint64_t object_id) {
    for (size_t index = 0; index < snapshot.page().layers.size(); ++index) {
        const auto& ids = snapshot.page().layers[index].object_ids;
        if (std::find(ids.begin(), ids.end(), object_id) != ids.end()) return index;
    }
    return std::nullopt;
}

inline bool object_locked(const document::ObjectVariant& object) {
    return std::visit([](const auto& value) { return value.locked; }, object);
}

inline bool transform_is_identity(const geometry::Transform2D& transform) {
    constexpr double tolerance = 1e-12;
    return std::abs(transform.a - 1.0) <= tolerance &&
           std::abs(transform.b) <= tolerance && std::abs(transform.c) <= tolerance &&
           std::abs(transform.d - 1.0) <= tolerance && std::abs(transform.tx) <= tolerance &&
           std::abs(transform.ty) <= tolerance;
}

inline bool supported_edit_target(const document::ObjectVariant& object) {
    return !std::holds_alternative<document::GroupObject>(object);
}

inline geometry::Rect2D object_rect(const document::ObjectVariant& object) {
    try {
        return std::visit([](const auto& value) -> geometry::Rect2D {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, document::PathObject> ||
                          std::is_same_v<T, document::ImageObject>) return value.bounds();
            if constexpr (std::is_same_v<T, document::TextObject>) {
                const auto layout = typography::TextLayout::layout_text(value.text, value.font);
                const auto transform = value.transform *
                    geometry::Transform2D::translation(value.position.x, value.position.y) *
                    geometry::Transform2D::scale(1.0, -1.0);
                return transform.map_rect(layout.bounds());
            }
            return {};
        }, object);
    } catch (...) {
        const auto invalid = std::numeric_limits<double>::quiet_NaN();
        return {invalid, invalid, invalid, invalid};
    }
}

class SetObjectStyleCommand final : public document::DocumentCommand {
public:
    explicit SetObjectStyleCommand(SetObjectStyle patch) : patch_(std::move(patch)) {}
    std::string_view name() const noexcept override { return "Set Object Style"; }

    document::CommandResult execute(document::Document& doc) override {
        const auto original = doc.objects().get(patch_.target_ids.front());
        if (!original) return document::CommandResult::fail("Style target no longer exists");
        auto updated = *original;
        const bool supported = std::visit([&](auto& object) {
            using T = std::decay_t<decltype(object)>;
            if constexpr (std::is_same_v<T, document::PathObject>) {
                if (patch_.fill) { object.fill = *patch_.fill; object.has_fill = true; }
                if (patch_.stroke) { object.stroke = *patch_.stroke; object.has_stroke = true; }
                if (patch_.stroke_width_mm) object.stroke_width_mm = *patch_.stroke_width_mm;
                return true;
            } else if constexpr (std::is_same_v<T, document::TextObject>) {
                if (patch_.fill) object.fill = *patch_.fill;
                return !patch_.stroke && !patch_.stroke_width_mm;
            } else {
                return false;
            }
        }, updated);
        if (!supported) return document::CommandResult::fail("Style is unsupported for this object type");
        before_page_ = doc.page();
        before_objects_ = doc.objects();
        doc.apply_mutation(doc.page(), doc.objects().set(patch_.target_ids.front(), std::move(updated)));
        executed_ = true;
        return document::CommandResult::ok();
    }

    document::CommandResult undo(document::Document& doc) override {
        if (!executed_ || !before_page_ || !before_objects_)
            return document::CommandResult::fail("Style command has no committed state to restore");
        doc.apply_mutation(*before_page_, *before_objects_);
        executed_ = false;
        return document::CommandResult::ok();
    }

private:
    SetObjectStyle patch_;
    std::optional<document::Page> before_page_;
    std::optional<document::PersistentObjectMap> before_objects_;
    bool executed_{false};
};

class GroupTopLevelObjectsCommand final : public document::DocumentCommand {
public:
    GroupTopLevelObjectsCommand(document::GroupObject group, std::vector<uint64_t> children)
        : group_(std::move(group)), children_(std::move(children)) {}
    std::string_view name() const noexcept override { return "Group Objects"; }

    document::CommandResult execute(document::Document& doc) override {
        if (executed_ || children_.size() < 2 || group_.id == document::kInvalidObjectId ||
            doc.objects().contains(group_.id))
            return document::CommandResult::fail("A group requires two existing objects and a new ID");
        auto page = doc.page();
        auto objects = doc.objects();
        const std::unordered_set<uint64_t> selected(children_.begin(), children_.end());
        if (selected.size() != children_.size())
            return document::CommandResult::fail("Group target IDs must be unique");
        size_t owner_layer = page.layers.size();
        std::vector<size_t> positions;
        for (size_t layer_index = 0; layer_index < page.layers.size(); ++layer_index) {
            const auto& layer = page.layers[layer_index];
            for (size_t position = 0; position < layer.object_ids.size(); ++position) {
                if (!selected.contains(layer.object_ids[position])) continue;
                if (owner_layer == page.layers.size()) owner_layer = layer_index;
                if (owner_layer != layer_index)
                    return document::CommandResult::fail("Objects must be in the same layer to group");
                positions.push_back(position);
            }
        }
        if (positions.size() != children_.size())
            return document::CommandResult::fail("Only top-level sibling objects can be grouped");
        auto& layer = page.layers[owner_layer];
        if (layer.locked) return document::CommandResult::fail("The target layer is locked");
        const auto insertion = *std::min_element(positions.begin(), positions.end());
        std::vector<uint64_t> remaining;
        std::vector<uint64_t> ordered_children;
        remaining.reserve(layer.object_ids.size() - children_.size() + 1);
        ordered_children.reserve(children_.size());
        for (size_t position = 0; position < layer.object_ids.size(); ++position) {
            const auto id = layer.object_ids[position];
            if (position == insertion) remaining.push_back(group_.id);
            if (selected.contains(id)) ordered_children.push_back(id);
            else remaining.push_back(id);
        }
        group_.children_ids = std::move(ordered_children);
        layer.object_ids = std::move(remaining);
        before_page_ = doc.page();
        before_objects_ = doc.objects();
        doc.apply_mutation(std::move(page), objects.set(group_.id, group_));
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
    std::vector<uint64_t> children_;
    std::optional<document::Page> before_page_;
    std::optional<document::PersistentObjectMap> before_objects_;
    bool executed_{false};
};

class UngroupTopLevelObjectsCommand final : public document::DocumentCommand {
public:
    explicit UngroupTopLevelObjectsCommand(std::vector<uint64_t> groups) : groups_(std::move(groups)) {}
    std::string_view name() const noexcept override { return "Ungroup Objects"; }

    document::CommandResult execute(document::Document& doc) override {
        if (executed_ || groups_.empty())
            return document::CommandResult::fail("At least one top-level group is required");
        const std::unordered_set<uint64_t> selected(groups_.begin(), groups_.end());
        if (selected.size() != groups_.size())
            return document::CommandResult::fail("Group target IDs must be unique");
        auto page = doc.page();
        auto objects = doc.objects();
        std::unordered_set<uint64_t> found;
        for (auto& layer : page.layers) {
            std::vector<uint64_t> roots;
            roots.reserve(layer.object_ids.size());
            for (const auto id : layer.object_ids) {
                if (!selected.contains(id)) { roots.push_back(id); continue; }
                const auto object = objects.get(id);
                const auto* group = object ? std::get_if<document::GroupObject>(&*object) : nullptr;
                if (!group) return document::CommandResult::fail("Only top-level groups can be ungrouped");
                if (layer.locked || group->locked)
                    return document::CommandResult::fail("A selected group or its layer is locked");
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
    std::vector<uint64_t> groups_;
    std::optional<document::Page> before_page_;
    std::optional<document::PersistentObjectMap> before_objects_;
    bool executed_{false};
};

inline geometry::Transform2D transform_delta(const TransformObjects& request) {
    const auto& pivot = request.pivot_mm;
    return geometry::Transform2D::translation(request.translate_x_mm, request.translate_y_mm) *
        geometry::Transform2D::translation(pivot.x, pivot.y) *
        geometry::Transform2D::rotation_degrees(request.rotation_degrees) *
        geometry::Transform2D::scale(request.scale_x, request.scale_y) *
        geometry::Transform2D::translation(-pivot.x, -pivot.y);
}


} // namespace pikdraw::automation::detail
