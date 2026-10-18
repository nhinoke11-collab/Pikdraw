#pragma once

#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/document/model/document_page.hpp"

#include <algorithm>
#include <vector>

namespace pikdraw::document {

/// Add a stable page root, optionally making it the active page.
class AddPageCommand final : public DocumentCommand {
public:
    explicit AddPageCommand(DocumentPage page, bool activate = true)
        : page_(std::move(page)), activate_(activate) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "Add Page"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    DocumentPage page_;
    bool activate_{true};
    bool applied_{false};
    std::vector<DocumentPage> previous_pages_;
    PageId previous_active_page_id_{kInvalidPageId};
};

/// Remove a page while preserving at least one page in the document.
class RemovePageCommand final : public DocumentCommand {
public:
    explicit RemovePageCommand(PageId page_id) : page_id_(page_id) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "Remove Page"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    PageId page_id_{kInvalidPageId};
    bool applied_{false};
    std::vector<DocumentPage> previous_pages_;
    PageId previous_active_page_id_{kInvalidPageId};
};

inline CommandResult AddPageCommand::execute(Document& doc) {
    auto next = doc.pages();
    if (std::any_of(next.begin(), next.end(), [&](const auto& page) {
            return page.id == page_.id;
        }))
        return CommandResult::fail("Page ID already exists");
    if (!applied_) {
        previous_pages_ = next;
        previous_active_page_id_ = doc.active_page_id();
    }
    next.push_back(page_);
    const auto active_id = activate_ ? page_.id : previous_active_page_id_;
    doc.apply_page_collection(std::move(next), active_id);
    applied_ = true;
    return CommandResult::ok();
}

inline CommandResult AddPageCommand::undo(Document& doc) {
    if (!applied_) return CommandResult::no_change();
    doc.apply_page_collection(previous_pages_, previous_active_page_id_);
    applied_ = false;
    return CommandResult::ok();
}

inline CommandResult RemovePageCommand::execute(Document& doc) {
    const auto& current = doc.pages();
    if (current.size() <= 1)
        return CommandResult::fail("A document must retain at least one page");
    const auto found = std::find_if(current.begin(), current.end(), [&](const auto& page) {
        return page.id == page_id_;
    });
    if (found == current.end()) return CommandResult::fail("Page does not exist");
    if (!applied_) {
        previous_pages_ = current;
        previous_active_page_id_ = doc.active_page_id();
    }
    const auto index = static_cast<size_t>(std::distance(current.begin(), found));
    std::vector<DocumentPage> next = current;
    next.erase(next.begin() + static_cast<std::ptrdiff_t>(index));
    auto active_id = previous_active_page_id_;
    if (active_id == page_id_)
        active_id = next[std::min(index, next.size() - 1)].id;
    doc.apply_page_collection(std::move(next), active_id);
    applied_ = true;
    return CommandResult::ok();
}

inline CommandResult RemovePageCommand::undo(Document& doc) {
    if (!applied_) return CommandResult::no_change();
    doc.apply_page_collection(previous_pages_, previous_active_page_id_);
    applied_ = false;
    return CommandResult::ok();
}

} // namespace pikdraw::document
