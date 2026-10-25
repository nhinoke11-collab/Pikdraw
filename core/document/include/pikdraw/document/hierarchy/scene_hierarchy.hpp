#pragma once

#include "pikdraw/document/model/document_page.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <span>
#include <unordered_set>
#include <vector>

namespace pikdraw::document {

// Immutable, snapshot-owned hierarchy data. Matrices use the document's Y-up basis.
class SceneHierarchy {
public:
    struct Entry {
        ObjectId parent{kInvalidObjectId};
        size_t layer{0};
        geometry::Transform2D parent_world;
        geometry::Transform2D world;
        std::optional<geometry::Rect2D> bounds;
        bool visible{true};
        bool locked{false};
        bool mask{false};
    };

    SceneHierarchy(const Page& page, const PersistentObjectMap& objects) {
        build(page, objects);
    }

    SceneHierarchy(const Page& page, const PersistentObjectMap& objects,
                   const SceneHierarchy& previous, const Page& previous_page,
                   const PersistentObjectMap& previous_objects,
                   std::span<const ObjectId> changed_ids)
        : entries_(previous.entries_) {
        if (!same_layer_topology(page, previous_page)) {
            build(page, objects);
            return;
        }

        if (changed_ids.empty()) {
            if (!objects.shares_root_with(previous_objects)) build(page, objects);
            return;
        }

        std::unordered_set<ObjectId> changed(changed_ids.begin(), changed_ids.end());
        for (const auto id : changed) {
            const auto* old_object = previous_objects.lookup_ref(id);
            const auto* new_object = objects.lookup_ref(id);
            if (!old_object || !new_object || old_object->index() != new_object->index() ||
                !same_group_topology(*old_object, *new_object)) {
                build(page, objects);
                return;
            }
        }

        std::vector<ObjectId> roots;
        roots.reserve(changed.size());
        for (const auto id : changed) {
            bool covered_by_changed_ancestor = false;
            for (const auto* entry = previous.find(id); entry && entry->parent;
                 entry = previous.find(entry->parent)) {
                if (changed.contains(entry->parent)) {
                    covered_by_changed_ancestor = true;
                    break;
                }
            }
            if (!covered_by_changed_ancestor) roots.push_back(id);
        }

        std::unordered_set<ObjectId> ancestors;
        for (const auto id : roots) {
            const auto* old_entry = previous.find(id);
            if (!old_entry) {
                build(page, objects);
                return;
            }
            const ObjectId parent = old_entry->parent;
            geometry::Transform2D parent_world;
            bool visible = true;
            bool locked = false;
            bool mask = old_entry->mask;
            if (parent) {
                const auto* parent_entry = find(parent);
                if (!parent_entry) {
                    build(page, objects);
                    return;
                }
                parent_world = parent_entry->world;
                visible = parent_entry->visible;
                locked = parent_entry->locked;
            } else {
                if (old_entry->layer >= page.layers.size()) {
                    build(page, objects);
                    return;
                }
                visible = page.layers[old_entry->layer].visible;
                locked = page.layers[old_entry->layer].locked;
            }
            if (!rebuild_subtree(objects, id, parent, old_entry->layer,
                                 parent_world, visible, locked, mask)) {
                build(page, objects);
                return;
            }
            for (ObjectId ancestor = parent; ancestor;) {
                ancestors.insert(ancestor);
                const auto* entry = find(ancestor);
                ancestor = entry ? entry->parent : kInvalidObjectId;
            }
        }

        std::vector<std::pair<size_t, ObjectId>> ordered_ancestors;
        ordered_ancestors.reserve(ancestors.size());
        for (const auto id : ancestors) {
            size_t distance_to_root = 0;
            for (auto* entry = find(id); entry && entry->parent;
                 entry = find(entry->parent)) ++distance_to_root;
            ordered_ancestors.emplace_back(distance_to_root, id);
        }
        std::sort(ordered_ancestors.begin(), ordered_ancestors.end(),
                  [](const auto& lhs, const auto& rhs) { return lhs.first > rhs.first; });
        for (const auto& [depth, id] : ordered_ancestors) {
            (void)depth;
            if (!recompute_group_bounds(objects, id)) {
                build(page, objects);
                return;
            }
        }
    }

    [[nodiscard]] const Entry* find(ObjectId id) const noexcept {
        return entries_.find(id);
    }
    [[nodiscard]] bool shares_storage_with(const SceneHierarchy& other) const noexcept {
        return entries_.shares_root_with(other.entries_);
    }
    [[nodiscard]] bool contains(ObjectId owner, ObjectId id) const noexcept {
        if (!owner) return find(id) != nullptr;
        for (auto entry = find(id); entry; entry = find(entry->parent)) {
            if (id == owner) return true;
            id = entry->parent;
        }
        return false;
    }
    [[nodiscard]] bool editable(ObjectId id) const noexcept {
        const auto* entry = find(id);
        return entry && entry->visible && !entry->locked &&
               entry->world.is_finite() && entry->world.is_invertible();
    }
    [[nodiscard]] std::vector<ObjectId> roots(std::span<const ObjectId> ids) const {
        std::unordered_set<ObjectId> selected(ids.begin(), ids.end()), emitted;
        std::vector<ObjectId> result;
        for (auto id : ids) {
            if (!find(id) || !emitted.insert(id).second) continue;
            bool covered = false;
            for (auto e = find(id); e && e->parent; e = find(e->parent))
                if (selected.contains(e->parent)) { covered = true; break; }
            if (!covered) result.push_back(id);
        }
        return result;
    }

private:
    class EntryMap {
        struct Node;
        using NodePtr = std::shared_ptr<const Node>;
        struct Node {
            ObjectId id;
            Entry value;
            NodePtr left;
            NodePtr right;
            int height;
            Node(ObjectId key, Entry entry, NodePtr lhs, NodePtr rhs)
                : id(key), value(std::move(entry)), left(std::move(lhs)),
                  right(std::move(rhs)),
                  height(1 + std::max(left ? left->height : 0,
                                      right ? right->height : 0)) {}
        };

    public:
        [[nodiscard]] const Entry* find(ObjectId id) const noexcept {
            const auto* node = root_.get();
            while (node) {
                if (id == node->id) return &node->value;
                node = id < node->id ? node->left.get() : node->right.get();
            }
            return nullptr;
        }
        void set(ObjectId id, Entry value) {
            const bool exists = find(id) != nullptr;
            root_ = insert(root_, id, std::move(value));
            if (!exists) ++count_;
        }
        [[nodiscard]] bool shares_root_with(const EntryMap& other) const noexcept {
            return root_ == other.root_;
        }
        void clear() noexcept { root_.reset(); count_ = 0; }

    private:
        static int height(const NodePtr& node) noexcept { return node ? node->height : 0; }
        static NodePtr make(ObjectId id, Entry value, NodePtr left, NodePtr right) {
            return std::make_shared<const Node>(id, std::move(value),
                                                std::move(left), std::move(right));
        }
        static NodePtr rotate_left(const NodePtr& node) {
            const auto& pivot = node->right;
            return make(pivot->id, pivot->value,
                        make(node->id, node->value, node->left, pivot->left),
                        pivot->right);
        }
        static NodePtr rotate_right(const NodePtr& node) {
            const auto& pivot = node->left;
            return make(pivot->id, pivot->value, pivot->left,
                        make(node->id, node->value, pivot->right, node->right));
        }
        static NodePtr balance(NodePtr node) {
            const auto delta = height(node->left) - height(node->right);
            if (delta > 1) {
                if (height(node->left->right) > height(node->left->left))
                    node = make(node->id, node->value, rotate_left(node->left), node->right);
                return rotate_right(node);
            }
            if (delta < -1) {
                if (height(node->right->left) > height(node->right->right))
                    node = make(node->id, node->value, node->left, rotate_right(node->right));
                return rotate_left(node);
            }
            return node;
        }
        static NodePtr insert(const NodePtr& node, ObjectId id, Entry value) {
            if (!node) return make(id, std::move(value), {}, {});
            if (id == node->id)
                return make(id, std::move(value), node->left, node->right);
            if (id < node->id)
                return balance(make(node->id, node->value,
                                    insert(node->left, id, std::move(value)), node->right));
            return balance(make(node->id, node->value, node->left,
                                insert(node->right, id, std::move(value))));
        }
        NodePtr root_;
        size_t count_{0};
    };

    static bool same_layer_topology(const Page& page, const Page& previous) {
        if (page.layers.size() != previous.layers.size()) return false;
        for (size_t index = 0; index < page.layers.size(); ++index) {
            const auto& current = page.layers[index];
            const auto& old = previous.layers[index];
            if (current.visible != old.visible || current.locked != old.locked ||
                current.object_ids != old.object_ids) return false;
        }
        return true;
    }

    static bool same_group_topology(const ObjectVariant& old_object,
                                   const ObjectVariant& new_object) {
        const auto* old_group = std::get_if<GroupObject>(&old_object);
        const auto* new_group = std::get_if<GroupObject>(&new_object);
        if (!old_group || !new_group) return old_group == nullptr && new_group == nullptr;
        const ObjectId old_mask = old_group->clip ? old_group->clip->mask_object_id : 0;
        const ObjectId new_mask = new_group->clip ? new_group->clip->mask_object_id : 0;
        return old_group->children_ids == new_group->children_ids && old_mask == new_mask;
    }

    void build(const Page& page, const PersistentObjectMap& objects) {
        entries_.clear();
        for (size_t layer = 0; layer < page.layers.size(); ++layer)
            for (auto id : page.layers[layer].object_ids)
                visit(objects, id, 0, layer, {}, page.layers[layer].visible,
                      page.layers[layer].locked, false);
    }

    std::optional<geometry::Rect2D> visit(
        const PersistentObjectMap& objects, ObjectId id, ObjectId parent,
        size_t layer, geometry::Transform2D parent_world, bool visible,
        bool locked, bool mask) {
        if (entries_.find(id)) throw std::invalid_argument("Invalid scene ownership");
        return rebuild_subtree(objects, id, parent, layer, parent_world,
                               visible, locked, mask);
    }

    std::optional<geometry::Rect2D> rebuild_subtree(
        const PersistentObjectMap& objects, ObjectId id, ObjectId parent,
        size_t layer, geometry::Transform2D parent_world, bool visible,
        bool locked, bool mask) {
        const auto* object = objects.lookup_ref(id);
        if (!object) throw std::invalid_argument("Invalid scene ownership");
        Entry entry;
        entry.parent = parent;
        entry.layer = layer;
        entry.parent_world = parent_world;
        entry.mask = mask;
        std::visit([&](const auto& value) {
            entry.world = parent_world * value.transform;
            entry.visible = visible && value.visible;
            entry.locked = locked || value.locked;
        }, *object);

        std::optional<geometry::Rect2D> bounds;
        if (const auto* group = std::get_if<GroupObject>(object)) {
            for (auto child : group->children_ids) {
                const auto child_bounds = rebuild_subtree(objects, child, id, layer,
                    entry.world, entry.visible, entry.locked,
                    group->clip && group->clip->mask_object_id == child);
                if (child_bounds)
                    bounds = bounds ? bounds->united(*child_bounds) : child_bounds;
            }
            if (group->clip) {
                if (const auto* frame = find(group->clip->mask_object_id))
                    bounds = frame->bounds;
            }
        } else {
            bounds = leaf_bounds(*object, entry.world);
        }
        entry.bounds = bounds;
        entries_.set(id, std::move(entry));
        return bounds;
    }

    static std::optional<geometry::Rect2D> leaf_bounds(
        const ObjectVariant& object, geometry::Transform2D world) {
        try {
            return std::visit([&](const auto& value) -> std::optional<geometry::Rect2D> {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, GroupObject>) {
                    return std::nullopt;
                } else if constexpr (std::is_same_v<T, PathObject>) {
                    return value.path.transformed(world).bounds();
                } else if constexpr (std::is_same_v<T, ImageObject>) {
                    return world.map_rect(value.bounds_rect);
                } else {
                    auto transformed = value;
                    transformed.transform = world;
                    return transformed.bounds();
                }
            }, object);
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }

    bool recompute_group_bounds(const PersistentObjectMap& objects, ObjectId id) {
        const auto* object = objects.lookup_ref(id);
        const auto* old_entry = find(id);
        const auto* group = object ? std::get_if<GroupObject>(object) : nullptr;
        if (!old_entry || !group) return false;
        Entry updated = *old_entry;
        std::optional<geometry::Rect2D> bounds;
        if (group->clip) {
            if (const auto* mask = find(group->clip->mask_object_id)) bounds = mask->bounds;
        } else {
            for (const auto child : group->children_ids) {
                if (const auto* entry = find(child); entry && entry->bounds)
                    bounds = bounds ? bounds->united(*entry->bounds) : entry->bounds;
            }
        }
        updated.bounds = bounds;
        entries_.set(id, std::move(updated));
        return true;
    }

    EntryMap entries_;
};

} // namespace pikdraw::document
