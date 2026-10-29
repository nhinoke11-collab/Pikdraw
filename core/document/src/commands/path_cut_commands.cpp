#include "pikdraw/document/commands/path_cut_commands.hpp"
#include "pikdraw/document/document.hpp"
#include <algorithm>
#include <unordered_set>

namespace pikdraw::document {

ReplaceObjectWithPiecesCommand::ReplaceObjectWithPiecesCommand(
    ObjectId target_id,
    std::vector<ObjectVariant> pieces,
    std::string name,
    std::shared_ptr<PathTopologyMapping> mapping_result
) noexcept
    : target_id_(target_id),
      pieces_(std::move(pieces)),
      name_(std::move(name)),
      mapping_result_(std::move(mapping_result)) {}

CommandResult ReplaceObjectWithPiecesCommand::execute(Document& doc) {
    const auto snapshot = doc.current_snapshot();
    if (!snapshot) {
        return CommandResult::fail("Cannot replace object without an active snapshot");
    }

    const auto* entry = snapshot->hierarchy().find(target_id_);
    if (!entry) {
        return CommandResult::fail("Target object not found in document hierarchy");
    }

    if (entry->mask || !snapshot->hierarchy().editable(target_id_)) {
        return CommandResult::fail("Cannot replace locked, hidden, or clip mask object");
    }

    auto page = doc.page();
    auto map = doc.objects();
    GroupObject parent_group;
    std::vector<ObjectId>* owner_ids = nullptr;
    if (entry->parent) {
        const auto parent_obj = map.get(entry->parent);
        if (!parent_obj || !std::holds_alternative<GroupObject>(*parent_obj)) {
            return CommandResult::fail("Parent group not found");
        }
        parent_group = std::get<GroupObject>(*parent_obj);
        owner_ids = &parent_group.children_ids;
    } else {
        if (entry->layer >= page.layers.size()) {
            return CommandResult::fail("Invalid layer index");
        }
        owner_ids = &page.layers[entry->layer].object_ids;
    }
    const auto target_slot = std::find(owner_ids->begin(), owner_ids->end(), target_id_);
    if (target_slot == owner_ids->end()) {
        return CommandResult::fail("Target is missing from its recorded owner stack");
    }

    // Allocate IDs for pieces if needed and ensure valid transforms
    new_object_ids_.clear();
    new_object_ids_.reserve(pieces_.size());
    std::unordered_set<ObjectId> seen_ids;
    bool preserves_target_id = false;
    for (auto& piece : pieces_) {
        ObjectId piece_id = std::visit([](const auto& o) { return o.id; }, piece);
        if (piece_id == kInvalidObjectId) {
            piece_id = generate_object_id();
            std::visit([piece_id](auto& o) { o.id = piece_id; }, piece);
        }
        if (!seen_ids.insert(piece_id).second) {
            return CommandResult::fail("Replacement pieces contain duplicate object IDs");
        }
        if (piece_id == target_id_) {
            preserves_target_id = true;
        } else if (snapshot->objects().get(piece_id)) {
            return CommandResult::fail("Replacement piece ID already belongs to another object");
        }
        if (!std::visit([](const auto& o) { return o.transform.is_finite(); }, piece)) {
            return CommandResult::fail("Piece has non-finite transform");
        }
        new_object_ids_.push_back(piece_id);
    }
    if (!pieces_.empty() && !preserves_target_id) {
        return CommandResult::fail("A non-empty replacement must preserve the target object ID");
    }

    before_snapshot_ = snapshot;

    // Remove target from objects map
    map = map.remove(target_id_);

    // Replace target_id in parent group or layer object_ids
    if (entry->parent) {
        const auto insert_idx = std::distance(parent_group.children_ids.begin(), target_slot);
        parent_group.children_ids.erase(parent_group.children_ids.begin() + insert_idx);
        if (!new_object_ids_.empty()) {
            parent_group.children_ids.insert(parent_group.children_ids.begin() + insert_idx,
                                             new_object_ids_.begin(), new_object_ids_.end());
        }
        if (parent_group.clip) {
            parent_group.clip->is_empty_frame = (parent_group.children_ids.size() <= 1);
        }
        map = map.set(parent_group.id, parent_group);
    } else {
        auto& ids = page.layers[entry->layer].object_ids;
        const auto insert_idx = std::distance(ids.begin(), target_slot);
        ids.erase(ids.begin() + insert_idx);
        if (!new_object_ids_.empty())
            ids.insert(ids.begin() + insert_idx, new_object_ids_.begin(), new_object_ids_.end());
    }

    // Add all pieces into map
    for (size_t i = 0; i < pieces_.size(); ++i) {
        map = map.set(new_object_ids_[i], pieces_[i]);
        reserve_object_id(new_object_ids_[i]);
    }

    doc.apply_mutation(std::move(page), std::move(map));
    return CommandResult::ok();
}

CommandResult ReplaceObjectWithPiecesCommand::undo(Document& doc) {
    if (!before_snapshot_) {
        return CommandResult::fail("No saved snapshot to undo ReplaceObjectWithPiecesCommand");
    }
    doc.apply_mutation(before_snapshot_->page(), before_snapshot_->objects());
    return CommandResult::ok();
}

} // namespace pikdraw::document
