#pragma once

#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/snapshots/document_snapshot.hpp"

namespace pikdraw::document {

enum class TACScope : uint8_t {
    Document,
    ActivePage,
    Selection
};

struct TACValidationResult {
    bool success{false};
    std::string error_message;
    color::TACSummary summary;
};

/// Read-only TAC inspection for process CMYK paints in an explicit output condition.
/// Selection IDs are resolved only on the snapshot's active page.
[[nodiscard]] TACValidationResult validate_document_tac(
    const DocumentSnapshot& snapshot,
    const color::TACOutputCondition& output_condition,
    TACScope scope = TACScope::Document,
    const std::vector<ObjectId>& selection = {});

/// Clamps process CMYK paints with the output condition's explicit TAC limit.
/// K, alpha, overprint, spot identities and all non-CMYK paints are preserved.
class FixDocumentTacCommand final : public DocumentCommand {
public:
    explicit FixDocumentTacCommand(color::TACOutputCondition output_condition,
        TACScope scope = TACScope::Document,
        std::vector<ObjectId> selection = {});

    std::string_view name() const noexcept override { return "Fix Total Area Coverage"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    color::TACOutputCondition output_condition_;
    TACScope scope_{TACScope::Document};
    std::vector<ObjectId> selection_;
    SnapshotPtr before_;
};

} // namespace pikdraw::document
