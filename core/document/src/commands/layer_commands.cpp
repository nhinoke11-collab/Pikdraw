#include "pikdraw/document/commands/layer_commands.hpp"
#include <algorithm>

namespace pikdraw::document {

// --- AddLayerCommand ---

AddLayerCommand::AddLayerCommand(std::string name, std::optional<size_t> insert_index)
    : name_(std::move(name)), insert_index_(insert_index) {}

CommandResult AddLayerCommand::execute(Document& doc) {
    Page page = doc.page();
    actual_index_ = insert_index_.value_or(page.layers.size());
    if (actual_index_ > page.layers.size()) {
        actual_index_ = page.layers.size();
    }
    Layer new_layer{name_.empty() ? "Layer " + std::to_string(page.layers.size() + 1) : name_, true, false, {}};
    page.layers.insert(page.layers.begin() + static_cast<std::ptrdiff_t>(actual_index_), std::move(new_layer));
    doc.apply_mutation(std::move(page), doc.objects());
    executed_ = true;
    return CommandResult::ok();
}

CommandResult AddLayerCommand::undo(Document& doc) {
    if (!executed_) {
        return CommandResult::fail("Cannot undo unexecuted AddLayerCommand");
    }
    Page page = doc.page();
    if (actual_index_ >= page.layers.size()) {
        return CommandResult::fail("Invalid layer index on undo");
    }
    page.layers.erase(page.layers.begin() + static_cast<std::ptrdiff_t>(actual_index_));
    doc.apply_mutation(std::move(page), doc.objects());
    executed_ = false;
    return CommandResult::ok();
}

// --- RemoveLayerCommand ---

RemoveLayerCommand::RemoveLayerCommand(size_t layer_idx)
    : layer_idx_(layer_idx) {}

CommandResult RemoveLayerCommand::execute(Document& doc) {
    Page page = doc.page();
    if (page.layers.size() <= 1) {
        return CommandResult::fail("A document page must retain at least one layer");
    }
    if (layer_idx_ >= page.layers.size()) {
        return CommandResult::fail("Invalid layer index for removal");
    }

    removed_layer_ = page.layers[layer_idx_];
    removed_objects_.clear();
    PersistentObjectMap map = doc.objects();

    for (ObjectId id : removed_layer_->object_ids) {
        if (auto obj = map.get(id)) {
            removed_objects_.emplace_back(id, *obj);
            map = map.remove(id);
        }
    }

    page.layers.erase(page.layers.begin() + static_cast<std::ptrdiff_t>(layer_idx_));
    doc.apply_mutation(std::move(page), std::move(map));
    executed_ = true;
    return CommandResult::ok();
}

CommandResult RemoveLayerCommand::undo(Document& doc) {
    if (!executed_ || !removed_layer_) {
        return CommandResult::fail("Cannot undo unexecuted RemoveLayerCommand");
    }
    Page page = doc.page();
    PersistentObjectMap map = doc.objects();

    for (const auto& [id, obj] : removed_objects_) {
        map = map.set(id, obj);
    }

    if (layer_idx_ > page.layers.size()) {
        layer_idx_ = page.layers.size();
    }
    page.layers.insert(page.layers.begin() + static_cast<std::ptrdiff_t>(layer_idx_), *removed_layer_);
    doc.apply_mutation(std::move(page), std::move(map));
    executed_ = false;
    return CommandResult::ok();
}

// --- RenameLayerCommand ---

RenameLayerCommand::RenameLayerCommand(size_t layer_idx, std::string new_name)
    : layer_idx_(layer_idx), new_name_(std::move(new_name)) {}

CommandResult RenameLayerCommand::execute(Document& doc) {
    Page page = doc.page();
    if (layer_idx_ >= page.layers.size()) {
        return CommandResult::fail("Invalid layer index for rename");
    }
    old_name_ = page.layers[layer_idx_].name;
    if (old_name_ == new_name_) {
        return CommandResult::no_change();
    }
    page.layers[layer_idx_].name = new_name_;
    doc.apply_mutation(std::move(page), doc.objects());
    executed_ = true;
    return CommandResult::ok();
}

CommandResult RenameLayerCommand::undo(Document& doc) {
    if (!executed_) {
        return CommandResult::fail("Cannot undo unexecuted RenameLayerCommand");
    }
    Page page = doc.page();
    if (layer_idx_ >= page.layers.size()) {
        return CommandResult::fail("Invalid layer index on undo rename");
    }
    page.layers[layer_idx_].name = old_name_;
    doc.apply_mutation(std::move(page), doc.objects());
    executed_ = false;
    return CommandResult::ok();
}

// --- SetLayerVisibilityCommand ---

SetLayerVisibilityCommand::SetLayerVisibilityCommand(size_t layer_idx, bool visible)
    : layer_idx_(layer_idx), visible_(visible) {}

CommandResult SetLayerVisibilityCommand::execute(Document& doc) {
    Page page = doc.page();
    if (layer_idx_ >= page.layers.size()) {
        return CommandResult::fail("Invalid layer index for visibility change");
    }
    old_visible_ = page.layers[layer_idx_].visible;
    if (old_visible_ == visible_) {
        return CommandResult::no_change();
    }
    page.layers[layer_idx_].visible = visible_;
    doc.apply_mutation(std::move(page), doc.objects());
    executed_ = true;
    return CommandResult::ok();
}

CommandResult SetLayerVisibilityCommand::undo(Document& doc) {
    if (!executed_) {
        return CommandResult::fail("Cannot undo unexecuted SetLayerVisibilityCommand");
    }
    Page page = doc.page();
    if (layer_idx_ >= page.layers.size()) {
        return CommandResult::fail("Invalid layer index on undo visibility");
    }
    page.layers[layer_idx_].visible = old_visible_;
    doc.apply_mutation(std::move(page), doc.objects());
    executed_ = false;
    return CommandResult::ok();
}

// --- SetLayerLockedCommand ---

SetLayerLockedCommand::SetLayerLockedCommand(size_t layer_idx, bool locked)
    : layer_idx_(layer_idx), locked_(locked) {}

CommandResult SetLayerLockedCommand::execute(Document& doc) {
    Page page = doc.page();
    if (layer_idx_ >= page.layers.size()) {
        return CommandResult::fail("Invalid layer index for lock change");
    }
    old_locked_ = page.layers[layer_idx_].locked;
    if (old_locked_ == locked_) {
        return CommandResult::no_change();
    }
    page.layers[layer_idx_].locked = locked_;
    doc.apply_mutation(std::move(page), doc.objects());
    executed_ = true;
    return CommandResult::ok();
}

CommandResult SetLayerLockedCommand::undo(Document& doc) {
    if (!executed_) {
        return CommandResult::fail("Cannot undo unexecuted SetLayerLockedCommand");
    }
    Page page = doc.page();
    if (layer_idx_ >= page.layers.size()) {
        return CommandResult::fail("Invalid layer index on undo lock");
    }
    page.layers[layer_idx_].locked = old_locked_;
    doc.apply_mutation(std::move(page), doc.objects());
    executed_ = false;
    return CommandResult::ok();
}

// --- ReorderLayerCommand ---

ReorderLayerCommand::ReorderLayerCommand(size_t from_idx, size_t to_idx)
    : from_idx_(from_idx), to_idx_(to_idx) {}

CommandResult ReorderLayerCommand::execute(Document& doc) {
    Page page = doc.page();
    if (from_idx_ >= page.layers.size() || to_idx_ >= page.layers.size()) {
        return CommandResult::fail("Invalid layer index for reorder");
    }
    if (from_idx_ == to_idx_) {
        return CommandResult::no_change();
    }
    Layer layer = std::move(page.layers[from_idx_]);
    page.layers.erase(page.layers.begin() + static_cast<std::ptrdiff_t>(from_idx_));
    page.layers.insert(page.layers.begin() + static_cast<std::ptrdiff_t>(to_idx_), std::move(layer));
    doc.apply_mutation(std::move(page), doc.objects());
    executed_ = true;
    return CommandResult::ok();
}

CommandResult ReorderLayerCommand::undo(Document& doc) {
    if (!executed_) {
        return CommandResult::fail("Cannot undo unexecuted ReorderLayerCommand");
    }
    Page page = doc.page();
    if (from_idx_ >= page.layers.size() || to_idx_ >= page.layers.size()) {
        return CommandResult::fail("Invalid layer index on undo reorder");
    }
    Layer layer = std::move(page.layers[to_idx_]);
    page.layers.erase(page.layers.begin() + static_cast<std::ptrdiff_t>(to_idx_));
    page.layers.insert(page.layers.begin() + static_cast<std::ptrdiff_t>(from_idx_), std::move(layer));
    doc.apply_mutation(std::move(page), doc.objects());
    executed_ = false;
    return CommandResult::ok();
}

// --- MoveObjectToLayerCommand ---

MoveObjectToLayerCommand::MoveObjectToLayerCommand(ObjectId id, size_t target_layer_idx, std::optional<size_t> insert_at)
    : id_(id), target_layer_idx_(target_layer_idx), insert_at_(insert_at) {}

CommandResult MoveObjectToLayerCommand::execute(Document& doc) {
    Page page = doc.page();
    if (target_layer_idx_ >= page.layers.size()) {
        return CommandResult::fail("Target layer index out of bounds");
    }
    if (page.layers[target_layer_idx_].locked) {
        return CommandResult::fail("Cannot move object into a locked layer");
    }

    bool found = false;
    for (size_t i = 0; i < page.layers.size(); ++i) {
        auto& ids = page.layers[i].object_ids;
        for (size_t j = 0; j < ids.size(); ++j) {
            if (ids[j] == id_) {
                source_layer_idx_ = i;
                source_position_ = j;
                ids.erase(ids.begin() + static_cast<std::ptrdiff_t>(j));
                found = true;
                break;
            }
        }
        if (found) break;
    }

    if (!found) {
        return CommandResult::fail("Object to move not found in any layer");
    }

    auto& target_ids = page.layers[target_layer_idx_].object_ids;
    actual_insert_pos_ = insert_at_.value_or(target_ids.size());
    if (actual_insert_pos_ > target_ids.size()) {
        actual_insert_pos_ = target_ids.size();
    }
    target_ids.insert(target_ids.begin() + static_cast<std::ptrdiff_t>(actual_insert_pos_), id_);

    doc.apply_mutation(std::move(page), doc.objects());
    executed_ = true;
    return CommandResult::ok();
}

CommandResult MoveObjectToLayerCommand::undo(Document& doc) {
    if (!executed_) {
        return CommandResult::fail("Cannot undo unexecuted MoveObjectToLayerCommand");
    }
    Page page = doc.page();
    if (target_layer_idx_ >= page.layers.size() || source_layer_idx_ >= page.layers.size()) {
        return CommandResult::fail("Layer indices out of bounds on undo");
    }

    auto& target_ids = page.layers[target_layer_idx_].object_ids;
    auto it = std::find(target_ids.begin(), target_ids.end(), id_);
    if (it != target_ids.end()) {
        target_ids.erase(it);
    }

    auto& source_ids = page.layers[source_layer_idx_].object_ids;
    size_t restore_pos = std::min(source_position_, source_ids.size());
    source_ids.insert(source_ids.begin() + static_cast<std::ptrdiff_t>(restore_pos), id_);

    doc.apply_mutation(std::move(page), doc.objects());
    executed_ = false;
    return CommandResult::ok();
}

// --- ReorderObjectInLayerCommand ---

ReorderObjectInLayerCommand::ReorderObjectInLayerCommand(ObjectId id, Step step, size_t absolute_index)
    : id_(id), step_(step), absolute_index_(absolute_index) {}

CommandResult ReorderObjectInLayerCommand::execute(Document& doc) {
    Page page = doc.page();
    bool found = false;

    for (size_t i = 0; i < page.layers.size(); ++i) {
        auto& ids = page.layers[i].object_ids;
        for (size_t j = 0; j < ids.size(); ++j) {
            if (ids[j] == id_) {
                layer_idx_ = i;
                old_index_ = j;
                found = true;
                break;
            }
        }
        if (found) break;
    }

    if (!found) {
        return CommandResult::fail("Object to reorder not found in document");
    }

    auto& ids = page.layers[layer_idx_].object_ids;
    if (ids.size() <= 1) {
        return CommandResult::no_change();
    }

    switch (step_) {
        case Step::BringToFront:
            new_index_ = ids.size() - 1;
            break;
        case Step::SendToBack:
            new_index_ = 0;
            break;
        case Step::BringForward:
            new_index_ = std::min(old_index_ + 1, ids.size() - 1);
            break;
        case Step::SendBackward:
            new_index_ = old_index_ == 0 ? 0 : old_index_ - 1;
            break;
        case Step::ToAbsoluteIndex:
            new_index_ = std::min(absolute_index_, ids.size() - 1);
            break;
    }

    if (new_index_ == old_index_) {
        return CommandResult::no_change();
    }

    ids.erase(ids.begin() + static_cast<std::ptrdiff_t>(old_index_));
    ids.insert(ids.begin() + static_cast<std::ptrdiff_t>(new_index_), id_);

    doc.apply_mutation(std::move(page), doc.objects());
    executed_ = true;
    return CommandResult::ok();
}

CommandResult ReorderObjectInLayerCommand::undo(Document& doc) {
    if (!executed_) {
        return CommandResult::fail("Cannot undo unexecuted ReorderObjectInLayerCommand");
    }
    Page page = doc.page();
    if (layer_idx_ >= page.layers.size()) {
        return CommandResult::fail("Invalid layer index on undo reorder");
    }
    auto& ids = page.layers[layer_idx_].object_ids;
    auto it = std::find(ids.begin(), ids.end(), id_);
    if (it != ids.end()) {
        ids.erase(it);
    }
    size_t restore_pos = std::min(old_index_, ids.size());
    ids.insert(ids.begin() + static_cast<std::ptrdiff_t>(restore_pos), id_);

    doc.apply_mutation(std::move(page), doc.objects());
    executed_ = false;
    return CommandResult::ok();
}

// --- SetObjectVisibilityCommand ---

SetObjectVisibilityCommand::SetObjectVisibilityCommand(ObjectId id, bool visible)
    : id_(id), visible_(visible) {}

CommandResult SetObjectVisibilityCommand::execute(Document& doc) {
    auto opt = doc.objects().get(id_);
    if (!opt) {
        return CommandResult::fail("Object not found for visibility change");
    }
    old_visible_ = std::visit([](const auto& o) { return o.visible; }, *opt);
    if (old_visible_ == visible_) {
        return CommandResult::no_change();
    }

    ObjectVariant updated = *opt;
    std::visit([&](auto& o) { o.visible = visible_; }, updated);

    PersistentObjectMap map = doc.objects().set(id_, updated);
    doc.apply_mutation(doc.page(), std::move(map));
    executed_ = true;
    return CommandResult::ok();
}

CommandResult SetObjectVisibilityCommand::undo(Document& doc) {
    if (!executed_) {
        return CommandResult::fail("Cannot undo unexecuted SetObjectVisibilityCommand");
    }
    auto opt = doc.objects().get(id_);
    if (!opt) {
        return CommandResult::fail("Object not found on undo visibility");
    }
    ObjectVariant updated = *opt;
    std::visit([&](auto& o) { o.visible = old_visible_; }, updated);

    PersistentObjectMap map = doc.objects().set(id_, updated);
    doc.apply_mutation(doc.page(), std::move(map));
    executed_ = false;
    return CommandResult::ok();
}

// --- SetObjectLockedCommand ---

SetObjectLockedCommand::SetObjectLockedCommand(ObjectId id, bool locked)
    : id_(id), locked_(locked) {}

CommandResult SetObjectLockedCommand::execute(Document& doc) {
    auto opt = doc.objects().get(id_);
    if (!opt) {
        return CommandResult::fail("Object not found for lock change");
    }
    old_locked_ = std::visit([](const auto& o) { return o.locked; }, *opt);
    if (old_locked_ == locked_) {
        return CommandResult::no_change();
    }

    ObjectVariant updated = *opt;
    std::visit([&](auto& o) { o.locked = locked_; }, updated);

    PersistentObjectMap map = doc.objects().set(id_, updated);
    doc.apply_mutation(doc.page(), std::move(map));
    executed_ = true;
    return CommandResult::ok();
}

CommandResult SetObjectLockedCommand::undo(Document& doc) {
    if (!executed_) {
        return CommandResult::fail("Cannot undo unexecuted SetObjectLockedCommand");
    }
    auto opt = doc.objects().get(id_);
    if (!opt) {
        return CommandResult::fail("Object not found on undo lock");
    }
    ObjectVariant updated = *opt;
    std::visit([&](auto& o) { o.locked = old_locked_; }, updated);

    PersistentObjectMap map = doc.objects().set(id_, updated);
    doc.apply_mutation(doc.page(), std::move(map));
    executed_ = false;
    return CommandResult::ok();
}

// --- RenameObjectCommand ---

RenameObjectCommand::RenameObjectCommand(ObjectId id, std::string new_name)
    : id_(id), new_name_(std::move(new_name)) {}

CommandResult RenameObjectCommand::execute(Document& doc) {
    auto opt = doc.objects().get(id_);
    if (!opt) {
        return CommandResult::fail("Object not found for rename");
    }
    old_name_ = std::visit([](const auto& o) { return o.name; }, *opt);
    if (old_name_ == new_name_) {
        return CommandResult::no_change();
    }

    ObjectVariant updated = *opt;
    std::visit([&](auto& o) { o.name = new_name_; }, updated);

    PersistentObjectMap map = doc.objects().set(id_, updated);
    doc.apply_mutation(doc.page(), std::move(map));
    executed_ = true;
    return CommandResult::ok();
}

CommandResult RenameObjectCommand::undo(Document& doc) {
    if (!executed_) {
        return CommandResult::fail("Cannot undo unexecuted RenameObjectCommand");
    }
    auto opt = doc.objects().get(id_);
    if (!opt) {
        return CommandResult::fail("Object not found on undo rename");
    }
    ObjectVariant updated = *opt;
    std::visit([&](auto& o) { o.name = old_name_; }, updated);

    PersistentObjectMap map = doc.objects().set(id_, updated);
    doc.apply_mutation(doc.page(), std::move(map));
    executed_ = false;
    return CommandResult::ok();
}

} // namespace pikdraw::document
