#pragma once
#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/model/document_page.hpp"
#include "pikdraw/document/snapshots/persistent_map.hpp"

namespace pikdraw::document {

enum class ClipOperation { Place, Extract, Release, Fit, SetLock, SetAppearance, CopyContents };

// One ownership mutation and one Undo unit, including optional fitting.
class ClipCommand final : public DocumentCommand {
public:
    ClipCommand(ClipOperation operation, ObjectId frame, std::vector<ObjectId> contents = {},
                ClipContentFitMode fit = ClipContentFitMode::CurrentPosition, bool flag = true);
    [[nodiscard]] ObjectId result_id() const noexcept { return result_id_; }
    std::string_view name() const noexcept override { return "PowerClip"; }
    CommandResult execute(Document&) override;
    CommandResult undo(Document&) override;
private:
    ClipOperation operation_;
    ObjectId frame_, result_id_;
    std::vector<ObjectId> contents_;
    ClipContentFitMode fit_;
    bool flag_;
    std::optional<Page> before_page_;
    std::optional<PersistentObjectMap> before_objects_;
    std::optional<Page> after_page_;
    std::optional<PersistentObjectMap> after_objects_;
};

class AddScopedObjectCommand final : public DocumentCommand {
public:
    AddScopedObjectCommand(ObjectVariant object, ObjectId owner, size_t layer = 0)
        : object_(std::move(object)), owner_(owner), layer_(layer) {}
    std::string_view name() const noexcept override { return "Create Object in Scope"; }
    CommandResult execute(Document&) override;
    CommandResult undo(Document&) override;
private:
    ObjectVariant object_;
    ObjectId owner_;
    size_t layer_;
    std::optional<Page> before_page_;
    std::optional<PersistentObjectMap> before_objects_;
};
} // namespace pikdraw::document
