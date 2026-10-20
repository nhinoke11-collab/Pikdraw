#pragma once
#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include "pikdraw/color/preview/icc_preview.hpp"

namespace pikdraw::document {
[[nodiscard]] bool object_colors_match_mode(const ObjectVariant& object, color::DocumentColorMode mode);
// Shared by document writers; returns true only if process channels changed.
bool normalize_object_colors(ObjectVariant& object, color::DocumentColorMode mode,
                             const color::IccPreview* icc = nullptr);
PersistentObjectMap normalize_process_colors(const PersistentObjectMap& objects,
    color::DocumentColorMode mode, const color::IccPreview* icc = nullptr);

class ConvertDocumentColorModeCommand final : public DocumentCommand {
public:
    explicit ConvertDocumentColorModeCommand(color::DocumentColorMode mode,
        std::string profile = {}, std::shared_ptr<const color::IccPreview> icc = {});
    std::string_view name() const noexcept override { return "Convert Document Color Mode"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;
private:
    color::DocumentColorMode mode_;
    std::string profile_;
    std::shared_ptr<const color::IccPreview> icc_;
    SnapshotPtr before_;
};

class SetDocumentRasterDpiCommand final : public DocumentCommand {
public:
    explicit SetDocumentRasterDpiCommand(int32_t dpi) : dpi_(dpi) {}
    std::string_view name() const noexcept override { return "Set Document Raster Effects DPI"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;
private:
    int32_t dpi_;
    SnapshotPtr before_;
};

class SetPageColorModeCommand final : public DocumentCommand {
public:
    explicit SetPageColorModeCommand(PageId page_id, color::DocumentColorMode mode,
        std::string profile = {}, std::shared_ptr<const color::IccPreview> icc = {});
    std::string_view name() const noexcept override { return "Set Page Color Mode"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;
private:
    PageId page_id_;
    color::DocumentColorMode mode_;
    std::string profile_;
    std::shared_ptr<const color::IccPreview> icc_;
    SnapshotPtr before_;
};

class SetPageRasterDpiCommand final : public DocumentCommand {
public:
    explicit SetPageRasterDpiCommand(PageId page_id, int32_t dpi) : page_id_(page_id), dpi_(dpi) {}
    std::string_view name() const noexcept override { return "Set Page Raster Effects DPI"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;
private:
    PageId page_id_;
    int32_t dpi_;
    SnapshotPtr before_;
};

class ConvertObjectsColorModeCommand final : public DocumentCommand {
public:
    ConvertObjectsColorModeCommand(std::vector<ObjectId> object_ids, color::DocumentColorMode mode,
        std::string profile = {}, std::shared_ptr<const color::IccPreview> icc = {});
    std::string_view name() const noexcept override { return "Convert Selected Objects Color Mode"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;
private:
    std::vector<ObjectId> object_ids_;
    color::DocumentColorMode mode_;
    std::string profile_;
    std::shared_ptr<const color::IccPreview> icc_;
    SnapshotPtr before_;
};
} // namespace pikdraw::document
