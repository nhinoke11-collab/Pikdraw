#pragma once

#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/commands/path_commands.hpp"
#include <memory>
#include <string>
#include <vector>

namespace pikdraw::document {

/// Replaces an existing target object with zero, one, or multiple piece objects.
/// Preserves hierarchy (parent group/clip or layer) and exact stack slot (z-order).
/// Atomic execute and undo restoring the snapshot.
class ReplaceObjectWithPiecesCommand final : public DocumentCommand {
public:
    ReplaceObjectWithPiecesCommand(
        ObjectId target_id,
        std::vector<ObjectVariant> pieces,
        std::string name = "Replace Object With Pieces",
        std::shared_ptr<PathTopologyMapping> mapping_result = {}
    ) noexcept;

    [[nodiscard]] std::string_view name() const noexcept override { return name_; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

    [[nodiscard]] const std::vector<ObjectId>& new_object_ids() const noexcept {
        return new_object_ids_;
    }

private:
    ObjectId target_id_{kInvalidObjectId};
    std::vector<ObjectVariant> pieces_;
    std::string name_;
    std::shared_ptr<PathTopologyMapping> mapping_result_;
    std::vector<ObjectId> new_object_ids_;
    std::shared_ptr<const DocumentSnapshot> before_snapshot_;
};

} // namespace pikdraw::document
