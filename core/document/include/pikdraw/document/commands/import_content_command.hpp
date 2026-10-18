#pragma once

#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/snapshots/persistent_map.hpp"

namespace pikdraw::document {

/// Adds one already-normalized import proposal and its source layers atomically.
class ImportContentCommand final : public DocumentCommand {
public:
    ImportContentCommand(std::vector<Layer> layers, std::vector<ObjectVariant> objects);

    [[nodiscard]] std::string_view name() const noexcept override { return "Import Content"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    std::vector<Layer> imported_layers_;
    std::vector<ObjectVariant> imported_objects_;
    Page before_page_;
    PersistentObjectMap before_objects_;
    bool has_before_{false};
    bool executed_{false};
};

} // namespace pikdraw::document
