#pragma once

#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/document/model/clone_utils.hpp"
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

/// Rename a page without changing its stable identity or object roots.
class RenamePageCommand final : public DocumentCommand {
public:
    RenamePageCommand(PageId page_id, std::string name)
        : page_id_(page_id), name_(std::move(name)) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "Rename Page"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    PageId page_id_{kInvalidPageId};
    std::string name_;
    bool applied_{false};
    std::vector<DocumentPage> previous_pages_;
    PageId previous_active_page_id_{kInvalidPageId};
};

/// Change a page's physical size without moving or scaling its artwork.
class ResizePageCommand final : public DocumentCommand {
public:
    ResizePageCommand(PageId page_id, double width_mm, double height_mm)
        : page_id_(page_id), width_mm_(width_mm), height_mm_(height_mm) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "Resize Page"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    PageId page_id_{kInvalidPageId};
    double width_mm_{0.0};
    double height_mm_{0.0};
    bool applied_{false};
    std::vector<DocumentPage> previous_pages_;
    PageId previous_active_page_id_{kInvalidPageId};
};

/// Move a page to an explicit zero-based position in the document order.
class ReorderPageCommand final : public DocumentCommand {
public:
    ReorderPageCommand(PageId page_id, size_t destination)
        : page_id_(page_id), destination_(destination) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "Reorder Page"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    PageId page_id_{kInvalidPageId};
    size_t destination_{0};
    bool applied_{false};
    std::vector<DocumentPage> previous_pages_;
    PageId previous_active_page_id_{kInvalidPageId};
};

/// Deep-clone a page and its object hierarchy with fresh page/object/path IDs.
class DuplicatePageCommand final : public DocumentCommand {
public:
    explicit DuplicatePageCommand(PageId source_page_id)
        : source_page_id_(source_page_id) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "Duplicate Page"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    PageId source_page_id_{kInvalidPageId};
    DocumentPage duplicate_;
    bool prepared_{false};
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
        if (page_.artboards.empty())
            page_.artboards.push_back(make_page_bounds_artboard(page_.id, page_.layout));
    }
    auto added = page_;
    const auto& settings = doc.page();
    added.layout.color_mode = settings.color_mode;
    added.layout.color_profile_name = settings.color_profile_name;
    added.layout.raster_dpi = settings.raster_dpi;
    added.layout.color_mode_explicit = settings.color_mode_explicit;
    added.layout.raster_dpi_explicit = settings.raster_dpi_explicit;
    next.push_back(std::move(added));
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

inline CommandResult RenamePageCommand::execute(Document& doc) {
    if (applied_) return CommandResult::fail("Page rename is already applied");
    if (name_.empty()) return CommandResult::fail("Page name cannot be empty");
    auto next = doc.pages();
    const auto found = std::find_if(next.begin(), next.end(), [&](const auto& page) {
        return page.id == page_id_;
    });
    if (found == next.end()) return CommandResult::fail("Page does not exist");
    if (found->name == name_) return CommandResult::no_change();
    previous_pages_ = next;
    previous_active_page_id_ = doc.active_page_id();
    found->name = name_;
    doc.apply_page_collection(std::move(next), previous_active_page_id_);
    applied_ = true;
    return CommandResult::ok();
}

inline CommandResult RenamePageCommand::undo(Document& doc) {
    if (!applied_) return CommandResult::no_change();
    doc.apply_page_collection(previous_pages_, previous_active_page_id_);
    applied_ = false;
    return CommandResult::ok();
}

inline CommandResult ResizePageCommand::execute(Document& doc) {
    if (applied_) return CommandResult::fail("Page resize is already applied");
    auto next = doc.pages();
    const auto found = std::find_if(next.begin(), next.end(), [&](const auto& page) {
        return page.id == page_id_;
    });
    if (found == next.end()) return CommandResult::fail("Page does not exist");
    if (found->layout.width_mm == width_mm_ && found->layout.height_mm == height_mm_)
        return CommandResult::no_change();
    previous_pages_ = next;
    previous_active_page_id_ = doc.active_page_id();
    found->layout.width_mm = width_mm_;
    found->layout.height_mm = height_mm_;
    for (auto& artboard : found->artboards) {
        if (!artboard.linked_to_page_bounds) continue;
        artboard.width_mm = width_mm_;
        artboard.height_mm = height_mm_;
    }
    doc.apply_page_collection(std::move(next), previous_active_page_id_);
    applied_ = true;
    return CommandResult::ok();
}

inline CommandResult ResizePageCommand::undo(Document& doc) {
    if (!applied_) return CommandResult::no_change();
    doc.apply_page_collection(previous_pages_, previous_active_page_id_);
    applied_ = false;
    return CommandResult::ok();
}

inline CommandResult ReorderPageCommand::execute(Document& doc) {
    if (applied_) return CommandResult::fail("Page reorder is already applied");
    auto next = doc.pages();
    const auto found = std::find_if(next.begin(), next.end(), [&](const auto& page) {
        return page.id == page_id_;
    });
    if (found == next.end()) return CommandResult::fail("Page does not exist");
    if (destination_ >= next.size()) return CommandResult::fail("Page destination is outside the document");
    const size_t source = static_cast<size_t>(std::distance(next.begin(), found));
    if (source == destination_) return CommandResult::no_change();
    previous_pages_ = next;
    previous_active_page_id_ = doc.active_page_id();
    auto page = std::move(next[source]);
    next.erase(next.begin() + static_cast<std::ptrdiff_t>(source));
    next.insert(next.begin() + static_cast<std::ptrdiff_t>(destination_), std::move(page));
    doc.apply_page_collection(std::move(next), previous_active_page_id_);
    applied_ = true;
    return CommandResult::ok();
}

inline CommandResult ReorderPageCommand::undo(Document& doc) {
    if (!applied_) return CommandResult::no_change();
    doc.apply_page_collection(previous_pages_, previous_active_page_id_);
    applied_ = false;
    return CommandResult::ok();
}

inline CommandResult DuplicatePageCommand::execute(Document& doc) {
    if (applied_) return CommandResult::fail("Page duplicate is already applied");
    const auto source_it = std::find_if(doc.pages().begin(), doc.pages().end(), [&](const auto& page) {
        return page.id == source_page_id_;
    });
    if (source_it == doc.pages().end()) return CommandResult::fail("Page does not exist");
    if (!prepared_) {
        duplicate_ = *source_it;
        duplicate_.id = generate_page_id();
        if (duplicate_.id == kInvalidPageId) return CommandResult::fail("Page ID allocator exhausted");
        duplicate_.name += " copy";
        for (auto& artboard : duplicate_.artboards) {
            artboard.id = generate_artboard_id();
            artboard.owner_page_id = duplicate_.id;
            if (artboard.id == kInvalidArtboardId)
                return CommandResult::fail("Artboard ID allocator exhausted");
        }
        duplicate_.objects = {};
        std::vector<ObjectId> roots;
        for (const auto& layer : source_it->layout.layers)
            roots.insert(roots.end(), layer.object_ids.begin(), layer.object_ids.end());
        const auto source_snapshot = doc.current_snapshot();
        if (!source_snapshot) return CommandResult::fail("Document snapshot is unavailable");
        DocumentSnapshot page_snapshot{source_snapshot->epoch(), source_snapshot->revision(),
            std::vector<DocumentPage>{*source_it}, source_page_id_, source_snapshot->document_id()};
        auto cloned = clone_hierarchy(roots, page_snapshot);
        for (auto& object : cloned.cloned_objects) {
            const ObjectId id = std::visit([](const auto& value) { return value.id; }, object);
            duplicate_.objects = duplicate_.objects.set(id, std::move(object));
        }
        for (auto& layer : duplicate_.layout.layers) {
            for (auto& root : layer.object_ids) {
                const auto mapped = cloned.id_map.find(root);
                if (mapped == cloned.id_map.end())
                    return CommandResult::fail("Page hierarchy could not be cloned completely");
                root = mapped->second;
            }
        }
        prepared_ = true;
    }
    auto next = doc.pages();
    const auto current_source = std::find_if(next.begin(), next.end(), [&](const auto& page) {
        return page.id == source_page_id_;
    });
    if (current_source == next.end()) return CommandResult::fail("Source page no longer exists");
    previous_pages_ = next;
    previous_active_page_id_ = doc.active_page_id();
    const auto index = static_cast<size_t>(std::distance(next.begin(), current_source));
    next.insert(next.begin() + static_cast<std::ptrdiff_t>(index + 1), duplicate_);
    doc.apply_page_collection(std::move(next), duplicate_.id);
    applied_ = true;
    return CommandResult::ok();
}

inline CommandResult DuplicatePageCommand::undo(Document& doc) {
    if (!applied_) return CommandResult::no_change();
    doc.apply_page_collection(previous_pages_, previous_active_page_id_);
    applied_ = false;
    return CommandResult::ok();
}

} // namespace pikdraw::document
