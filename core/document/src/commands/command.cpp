#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/document.hpp"

namespace pikdraw::document {

// --- AddObjectCommand ---

AddObjectCommand::AddObjectCommand(ObjectVariant object, size_t layer_idx)
    : object_(std::move(object)), layer_idx_(layer_idx) {}

ObjectId AddObjectCommand::object_id() const noexcept {
    return std::visit([](const auto& obj) { return obj.id; }, object_);
}

CommandResult AddObjectCommand::execute(Document& doc) {
    Page page = doc.page();
    if (layer_idx_ >= page.layers.size()) {
        return CommandResult::fail("Invalid layer index for AddObjectCommand");
    }
    if (page.layers[layer_idx_].locked)
        return CommandResult::fail("Cannot add to a locked layer");

    ObjectId id = object_id();
    if (id == kInvalidObjectId) {
        return CommandResult::fail("Cannot add object with invalid ID");
    }

    if (doc.objects().contains(id)) {
        return CommandResult::fail("Object ID already exists");
    }
    if (!std::visit([](const auto& obj) { return obj.transform.is_finite(); }, object_))
        return CommandResult::fail("Non-finite object transform");

    PersistentObjectMap map = doc.objects().set(id, object_);
    page.layers[layer_idx_].object_ids.push_back(id);
    doc.apply_mutation(std::move(page), std::move(map));
    reserve_object_id(id);
    executed_ = true;
    return CommandResult::ok();
}

CommandResult AddObjectCommand::undo(Document& doc) {
    if (!executed_) {
        return CommandResult::fail("Cannot undo unexecuted AddObjectCommand");
    }
    Page page = doc.page();
    if (layer_idx_ >= page.layers.size()) {
        return CommandResult::fail("Invalid layer index on undo");
    }

    ObjectId id = object_id();
    auto& ids = page.layers[layer_idx_].object_ids;
    for (auto it = ids.begin(); it != ids.end(); ++it) {
        if (*it == id) {
            ids.erase(it);
            break;
        }
    }
    PersistentObjectMap map = doc.objects().remove(id);
    doc.apply_mutation(std::move(page), std::move(map));
    executed_ = false;
    return CommandResult::ok();
}

// --- RemoveObjectCommand ---

RemoveObjectCommand::RemoveObjectCommand(ObjectId id)
    : id_(id) {}

CommandResult RemoveObjectCommand::execute(Document& doc) {
    auto opt = doc.objects().get(id_);
    if (!opt) {
        return CommandResult::fail("Object to remove not found in document");
    }
    removed_object_ = opt;

    Page page = doc.page();
    bool found_in_layer = false;
    for (size_t i = 0; i < page.layers.size(); ++i) {
        auto& ids = page.layers[i].object_ids;
        for (auto it = ids.begin(); it != ids.end(); ++it) {
            if (*it == id_) {
                object_idx_ = static_cast<size_t>(it - ids.begin());
                ids.erase(it);
                layer_idx_ = i;
                found_in_layer = true;
                break;
            }
        }
        if (found_in_layer) break;
    }

    PersistentObjectMap map = doc.objects().remove(id_);
    doc.apply_mutation(std::move(page), std::move(map));
    return CommandResult::ok();
}

CommandResult RemoveObjectCommand::undo(Document& doc) {
    if (!removed_object_) {
        return CommandResult::fail("No saved object to restore on undo");
    }
    Page page = doc.page();
    if (layer_idx_ >= page.layers.size()) {
        return CommandResult::fail("Invalid layer index restoring removed object");
    }

    PersistentObjectMap map = doc.objects().set(id_, *removed_object_);
    auto& ids = page.layers[layer_idx_].object_ids;
    if (object_idx_ > ids.size()) return CommandResult::fail("Invalid saved z-order index");
    ids.insert(ids.begin() + static_cast<std::ptrdiff_t>(object_idx_), id_);
    doc.apply_mutation(std::move(page), std::move(map));
    return CommandResult::ok();
}

// --- TransformObjectCommand ---

TransformObjectCommand::TransformObjectCommand(ObjectId id, geometry::Transform2D delta)
    : id_(id), delta_(delta) {}

CommandResult TransformObjectCommand::execute(Document& doc) {
    if (!delta_.is_finite()) return CommandResult::fail("Non-finite transform delta");
    auto opt = doc.objects().get(id_);
    if (!opt) {
        return CommandResult::fail("Object to transform not found in document");
    }

    ObjectVariant updated = *opt;
    std::visit([&](auto& obj) {
        previous_transform_ = obj.transform;
        obj.transform = delta_ * obj.transform;
    }, updated);

    if (!std::visit([](const auto& obj) { return obj.transform.is_finite(); }, updated))
        return CommandResult::fail("Transform overflow");

    PersistentObjectMap map = doc.objects().set(id_, std::move(updated));
    doc.apply_mutation(doc.page(), std::move(map));
    return CommandResult::ok();
}

CommandResult TransformObjectCommand::undo(Document& doc) {
    auto opt = doc.objects().get(id_);
    if (!opt) {
        return CommandResult::fail("Object to un-transform not found in document");
    }

    ObjectVariant updated = *opt;
    std::visit([&](auto& obj) {
        obj.transform = previous_transform_;
    }, updated);

    PersistentObjectMap map = doc.objects().set(id_, std::move(updated));
    doc.apply_mutation(doc.page(), std::move(map));
    return CommandResult::ok();
}

} // namespace pikdraw::document
