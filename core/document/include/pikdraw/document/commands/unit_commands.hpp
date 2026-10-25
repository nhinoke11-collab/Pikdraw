#pragma once

#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include "pikdraw/geometry/units/length_unit.hpp"

namespace pikdraw::document {

/// Changes the display/input unit attached to one page. Geometry remains mm.
class SetPageDisplayLengthUnitCommand final : public DocumentCommand {
public:
    SetPageDisplayLengthUnitCommand(PageId page_id, geometry::units::LengthUnit unit)
        : page_id_(page_id), unit_(unit) {}
    [[nodiscard]] std::string_view name() const noexcept override {
        return "Set Page Display Length Unit";
    }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;
private:
    PageId page_id_{kInvalidPageId};
    geometry::units::LengthUnit unit_{geometry::units::LengthUnit::Millimeter};
    SnapshotPtr before_;
};

/// Changes the unit for every page in one undoable transaction.
class SetDocumentDisplayLengthUnitCommand final : public DocumentCommand {
public:
    explicit SetDocumentDisplayLengthUnitCommand(geometry::units::LengthUnit unit)
        : unit_(unit) {}
    [[nodiscard]] std::string_view name() const noexcept override {
        return "Set Document Display Length Unit";
    }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;
private:
    geometry::units::LengthUnit unit_{geometry::units::LengthUnit::Millimeter};
    SnapshotPtr before_;
};

} // namespace pikdraw::document
