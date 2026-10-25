#include "pikdraw/document/commands/unit_commands.hpp"
#include "pikdraw/document/document.hpp"

#include <algorithm>

namespace pikdraw::document {
namespace {
bool valid_unit(geometry::units::LengthUnit unit) noexcept {
    return geometry::units::is_physical_length_unit(unit);
}
}

CommandResult SetPageDisplayLengthUnitCommand::execute(Document& doc) {
    if (!valid_unit(unit_))
        return CommandResult::fail("Page display unit must be a physical length unit");
    const auto snapshot = doc.current_snapshot();
    const auto* target = snapshot->find_page(page_id_);
    if (!target) return CommandResult::fail("Page not found");
    if (target->layout.display_length_unit == unit_ &&
        target->layout.display_length_unit_explicit)
        return CommandResult::no_change();

    auto pages = doc.pages();
    const auto found = std::find_if(pages.begin(), pages.end(), [&](const auto& page) {
        return page.id == page_id_;
    });
    if (found == pages.end()) return CommandResult::fail("Page not found in document");
    found->layout.display_length_unit = unit_;
    found->layout.display_length_unit_explicit = true;
    before_ = snapshot;
    doc.apply_page_collection(std::move(pages), doc.active_page_id());
    return CommandResult::ok();
}

CommandResult SetPageDisplayLengthUnitCommand::undo(Document& doc) {
    if (!before_) return CommandResult::fail("No page display unit change to undo");
    doc.apply_page_collection(before_->pages(), before_->active_page_id());
    return CommandResult::ok();
}

CommandResult SetDocumentDisplayLengthUnitCommand::execute(Document& doc) {
    if (!valid_unit(unit_))
        return CommandResult::fail("Document display unit must be a physical length unit");
    const auto snapshot = doc.current_snapshot();
    const bool already_set = std::all_of(snapshot->pages().begin(), snapshot->pages().end(),
        [&](const DocumentPage& page) {
            return page.layout.display_length_unit == unit_ &&
                page.layout.display_length_unit_explicit;
        });
    if (already_set) return CommandResult::no_change();

    auto pages = doc.pages();
    for (auto& page : pages) {
        page.layout.display_length_unit = unit_;
        page.layout.display_length_unit_explicit = true;
    }
    before_ = snapshot;
    doc.apply_page_collection(std::move(pages), doc.active_page_id());
    return CommandResult::ok();
}

CommandResult SetDocumentDisplayLengthUnitCommand::undo(Document& doc) {
    if (!before_) return CommandResult::fail("No document display unit change to undo");
    doc.apply_page_collection(before_->pages(), before_->active_page_id());
    return CommandResult::ok();
}

} // namespace pikdraw::document
