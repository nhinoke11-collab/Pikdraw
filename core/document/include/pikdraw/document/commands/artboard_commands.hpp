#pragma once

#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/document/model/document_page.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace pikdraw::document {

class AddArtboardCommand final : public DocumentCommand {
public:
    AddArtboardCommand(PageId page_id, std::string name, geometry::Rect2D bounds)
        : page_id_(page_id), artboard_{generate_artboard_id(), page_id, std::move(name),
              bounds.x, bounds.y, bounds.width, bounds.height, false} {}
    [[nodiscard]] std::string_view name() const noexcept override { return "Add Artboard"; }
    CommandResult execute(Document& doc) override {
        if (applied_) return CommandResult::fail("Artboard creation is already applied");
        if (artboard_.id == kInvalidArtboardId) return CommandResult::fail("Artboard ID allocator exhausted");
        auto next = doc.pages();
        const auto page = find_page(next, page_id_);
        if (page == next.end()) return CommandResult::fail("Artboard owner page does not exist");
        if (!artboard_.is_valid_for(page_id_)) return CommandResult::fail("Artboard geometry or name is invalid");
        if (std::any_of(page->artboards.begin(), page->artboards.end(), [&](const auto& item) {
                return item.id == artboard_.id;
            })) return CommandResult::fail("Artboard ID already exists");
        before_ = next;
        active_before_ = doc.active_page_id();
        page->artboards.push_back(artboard_);
        doc.apply_page_collection(std::move(next), active_before_);
        applied_ = true;
        return CommandResult::ok();
    }
    CommandResult undo(Document& doc) override {
        if (!applied_) return CommandResult::no_change();
        doc.apply_page_collection(before_, active_before_);
        applied_ = false;
        return CommandResult::ok();
    }
    [[nodiscard]] ArtboardId artboard_id() const noexcept { return artboard_.id; }
private:
    static std::vector<DocumentPage>::iterator find_page(
        std::vector<DocumentPage>& pages, PageId id) {
        return std::find_if(pages.begin(), pages.end(), [&](const auto& page) { return page.id == id; });
    }
    PageId page_id_{};
    Artboard artboard_;
    bool applied_{false};
    std::vector<DocumentPage> before_;
    PageId active_before_{kInvalidPageId};
};

class DeleteArtboardCommand final : public DocumentCommand {
public:
    DeleteArtboardCommand(PageId page_id, ArtboardId artboard_id)
        : page_id_(page_id), artboard_id_(artboard_id) {}
    [[nodiscard]] std::string_view name() const noexcept override { return "Delete Artboard"; }
    CommandResult execute(Document& doc) override {
        if (applied_) return CommandResult::fail("Artboard deletion is already applied");
        auto next = doc.pages();
        const auto page = std::find_if(next.begin(), next.end(), [&](const auto& item) { return item.id == page_id_; });
        if (page == next.end()) return CommandResult::fail("Artboard owner page does not exist");
        if (page->artboards.size() <= 1) return CommandResult::fail("A page must retain at least one artboard");
        const auto board = std::find_if(page->artboards.begin(), page->artboards.end(),
            [&](const auto& item) { return item.id == artboard_id_; });
        if (board == page->artboards.end()) return CommandResult::fail("Artboard does not exist");
        before_ = next;
        active_before_ = doc.active_page_id();
        page->artboards.erase(board);
        doc.apply_page_collection(std::move(next), active_before_);
        applied_ = true;
        return CommandResult::ok();
    }
    CommandResult undo(Document& doc) override {
        if (!applied_) return CommandResult::no_change();
        doc.apply_page_collection(before_, active_before_);
        applied_ = false;
        return CommandResult::ok();
    }
private:
    PageId page_id_{};
    ArtboardId artboard_id_{};
    bool applied_{false};
    std::vector<DocumentPage> before_;
    PageId active_before_{kInvalidPageId};
};

class DuplicateArtboardCommand final : public DocumentCommand {
public:
    DuplicateArtboardCommand(PageId page_id, ArtboardId source_id)
        : page_id_(page_id), source_id_(source_id) {}
    [[nodiscard]] std::string_view name() const noexcept override { return "Duplicate Artboard"; }
    CommandResult execute(Document& doc) override {
        if (applied_) return CommandResult::fail("Artboard duplication is already applied");
        auto next = doc.pages();
        auto page = std::find_if(next.begin(), next.end(), [&](const auto& item) { return item.id == page_id_; });
        if (page == next.end()) return CommandResult::fail("Artboard owner page does not exist");
        const auto source = std::find_if(page->artboards.begin(), page->artboards.end(),
            [&](const auto& item) { return item.id == source_id_; });
        if (source == page->artboards.end()) return CommandResult::fail("Artboard does not exist");
        if (duplicate_.id == kInvalidArtboardId) {
            duplicate_ = *source;
            duplicate_.id = generate_artboard_id();
            duplicate_.owner_page_id = page_id_;
            duplicate_.name += " copy";
            duplicate_.x_mm += duplicate_.width_mm + 20.0;
            duplicate_.linked_to_page_bounds = false;
        }
        if (!duplicate_.is_valid_for(page_id_)) return CommandResult::fail("Duplicate artboard bounds are invalid");
        before_ = next;
        active_before_ = doc.active_page_id();
        page->artboards.push_back(duplicate_);
        doc.apply_page_collection(std::move(next), active_before_);
        applied_ = true;
        return CommandResult::ok();
    }
    CommandResult undo(Document& doc) override {
        if (!applied_) return CommandResult::no_change();
        doc.apply_page_collection(before_, active_before_);
        applied_ = false;
        return CommandResult::ok();
    }
    [[nodiscard]] ArtboardId duplicate_id() const noexcept { return duplicate_.id; }
private:
    PageId page_id_{};
    ArtboardId source_id_{};
    Artboard duplicate_;
    bool applied_{false};
    std::vector<DocumentPage> before_;
    PageId active_before_{kInvalidPageId};
};

class ReorderArtboardCommand final : public DocumentCommand {
public:
    ReorderArtboardCommand(PageId page_id, ArtboardId artboard_id, size_t destination_index)
        : page_id_(page_id), artboard_id_(artboard_id), destination_index_(destination_index) {}
    [[nodiscard]] std::string_view name() const noexcept override { return "Reorder Artboard"; }
    CommandResult execute(Document& doc) override {
        if (applied_) return CommandResult::fail("Artboard reorder is already applied");
        auto next = doc.pages();
        const auto page = std::find_if(next.begin(), next.end(), [&](const auto& item) {
            return item.id == page_id_;
        });
        if (page == next.end()) return CommandResult::fail("Artboard owner page does not exist");
        const auto found = std::find_if(page->artboards.begin(), page->artboards.end(),
            [&](const auto& item) { return item.id == artboard_id_; });
        if (found == page->artboards.end()) return CommandResult::fail("Artboard does not exist");
        const size_t from = static_cast<size_t>(std::distance(page->artboards.begin(), found));
        const size_t to = std::min(destination_index_, page->artboards.size() - 1);
        if (from == to) return CommandResult::no_change();
        before_ = next;
        active_before_ = doc.active_page_id();
        auto item = std::move(page->artboards[from]);
        page->artboards.erase(page->artboards.begin() + static_cast<std::ptrdiff_t>(from));
        page->artboards.insert(page->artboards.begin() + static_cast<std::ptrdiff_t>(to),
                               std::move(item));
        doc.apply_page_collection(std::move(next), active_before_);
        applied_ = true;
        return CommandResult::ok();
    }
    CommandResult undo(Document& doc) override {
        if (!applied_) return CommandResult::no_change();
        doc.apply_page_collection(before_, active_before_);
        applied_ = false;
        return CommandResult::ok();
    }
private:
    PageId page_id_{};
    ArtboardId artboard_id_{};
    size_t destination_index_{};
    bool applied_{false};
    std::vector<DocumentPage> before_;
    PageId active_before_{kInvalidPageId};
};

class RenameArtboardCommand final : public DocumentCommand {
public:
    RenameArtboardCommand(PageId page_id, ArtboardId artboard_id, std::string name)
        : page_id_(page_id), artboard_id_(artboard_id), name_(std::move(name)) {}
    [[nodiscard]] std::string_view name() const noexcept override { return "Rename Artboard"; }
    CommandResult execute(Document& doc) override {
        if (applied_) return CommandResult::fail("Artboard rename is already applied");
        if (name_.empty() || name_.size() > 1024) return CommandResult::fail("Artboard name is invalid");
        auto next = doc.pages();
        const auto page = std::find_if(next.begin(), next.end(), [&](const auto& item) { return item.id == page_id_; });
        if (page == next.end()) return CommandResult::fail("Artboard owner page does not exist");
        const auto board = std::find_if(page->artboards.begin(), page->artboards.end(),
            [&](const auto& item) { return item.id == artboard_id_; });
        if (board == page->artboards.end()) return CommandResult::fail("Artboard does not exist");
        if (board->name == name_) return CommandResult::no_change();
        before_ = next;
        active_before_ = doc.active_page_id();
        board->name = name_;
        doc.apply_page_collection(std::move(next), active_before_);
        applied_ = true;
        return CommandResult::ok();
    }
    CommandResult undo(Document& doc) override {
        if (!applied_) return CommandResult::no_change();
        doc.apply_page_collection(before_, active_before_);
        applied_ = false;
        return CommandResult::ok();
    }
private:
    PageId page_id_{};
    ArtboardId artboard_id_{};
    std::string name_;
    bool applied_{false};
    std::vector<DocumentPage> before_;
    PageId active_before_{kInvalidPageId};
};

class SetArtboardBoundsCommand final : public DocumentCommand {
public:
    SetArtboardBoundsCommand(PageId page_id, ArtboardId artboard_id, geometry::Rect2D bounds)
        : page_id_(page_id), artboard_id_(artboard_id), bounds_(bounds) {}
    [[nodiscard]] std::string_view name() const noexcept override { return "Set Artboard Bounds"; }
    CommandResult execute(Document& doc) override {
        if (applied_) return CommandResult::fail("Artboard bounds are already applied");
        if (!bounds_.is_finite() || bounds_.is_empty()) return CommandResult::fail("Artboard bounds are invalid");
        auto next = doc.pages();
        const auto page = std::find_if(next.begin(), next.end(), [&](const auto& item) { return item.id == page_id_; });
        if (page == next.end()) return CommandResult::fail("Artboard owner page does not exist");
        const auto board = std::find_if(page->artboards.begin(), page->artboards.end(),
            [&](const auto& item) { return item.id == artboard_id_; });
        if (board == page->artboards.end()) return CommandResult::fail("Artboard does not exist");
        if (board->x_mm == bounds_.x && board->y_mm == bounds_.y &&
            board->width_mm == bounds_.width && board->height_mm == bounds_.height)
            return CommandResult::no_change();
        before_ = next;
        active_before_ = doc.active_page_id();
        board->x_mm = bounds_.x;
        board->y_mm = bounds_.y;
        board->width_mm = bounds_.width;
        board->height_mm = bounds_.height;
        board->linked_to_page_bounds = false;
        doc.apply_page_collection(std::move(next), active_before_);
        applied_ = true;
        return CommandResult::ok();
    }
    CommandResult undo(Document& doc) override {
        if (!applied_) return CommandResult::no_change();
        doc.apply_page_collection(before_, active_before_);
        applied_ = false;
        return CommandResult::ok();
    }
private:
    PageId page_id_{};
    ArtboardId artboard_id_{};
    geometry::Rect2D bounds_;
    bool applied_{false};
    std::vector<DocumentPage> before_;
    PageId active_before_{kInvalidPageId};
};

} // namespace pikdraw::document
