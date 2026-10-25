#pragma once
#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/color/profiles/profile_resource.hpp"
#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include "pikdraw/color/preview/icc_preview.hpp"
#include "pikdraw/color/preview/profile_color_transform.hpp"

namespace pikdraw::document {
[[nodiscard]] bool object_colors_match_mode(const ObjectVariant& object, color::DocumentColorMode mode);
// Shared by document writers; returns true only if process channels changed.
bool normalize_object_colors(ObjectVariant& object, color::DocumentColorMode mode,
                             const color::IccPreview* icc = nullptr,
                             color::ColorConversionSummary* summary = nullptr);
PersistentObjectMap normalize_process_colors(const PersistentObjectMap& objects,
    color::DocumentColorMode mode, const color::IccPreview* icc = nullptr,
    color::ColorConversionSummary* summary = nullptr);

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

/// Assigns an exact ICC resource to one page without changing its stored paint channels.
class AssignPageColorProfileCommand final : public DocumentCommand {
public:
    AssignPageColorProfileCommand(PageId page_id,
        std::shared_ptr<const color::ProfileResource> resource);
    std::string_view name() const noexcept override { return "Assign page color profile"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;
private:
    PageId page_id_{};
    std::shared_ptr<const color::ProfileResource> resource_;
    SnapshotPtr before_;
};

/// Assigns an ICC interpretation to compatible process/gray paints in a selection.
/// Channel values stay unchanged; paints of other models are left untouched.
class AssignObjectsColorProfileCommand final : public DocumentCommand {
public:
    AssignObjectsColorProfileCommand(std::vector<ObjectId> object_ids,
        std::shared_ptr<const color::ProfileResource> resource);
    std::string_view name() const noexcept override { return "Assign selected paint ICC profile"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;
private:
    std::vector<ObjectId> object_ids_;
    std::shared_ptr<const color::ProfileResource> resource_;
    SnapshotPtr before_;
};

/// Converts process paints from the page's bound source ICC profile to a
/// destination ICC profile, then binds the destination in the same command.
class ConvertPageColorProfileCommand final : public DocumentCommand {
public:
    ConvertPageColorProfileCommand(PageId page_id,
        std::shared_ptr<const color::ProfileColorTransform> transform);
    std::string_view name() const noexcept override { return "Convert page ICC profile"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;
private:
    PageId page_id_{};
    std::shared_ptr<const color::ProfileColorTransform> transform_;
    SnapshotPtr before_;
};

/// Converts selected RGB/CMYK paints using the active page's source ICC profile.
/// Since the page itself stays unchanged, converted paints become explicitly bound
/// to the destination profile in the same Undoable command.
class ConvertObjectsColorProfileCommand final : public DocumentCommand {
public:
    ConvertObjectsColorProfileCommand(std::vector<ObjectId> object_ids,
        PageId page_id, std::shared_ptr<const color::ProfileColorTransform> transform);
    ConvertObjectsColorProfileCommand(std::vector<ObjectId> object_ids,
        PageId page_id, std::shared_ptr<const color::ProfileResource> destination,
        std::vector<std::shared_ptr<const color::ProfileColorTransform>> transforms);
    std::string_view name() const noexcept override { return "Convert selected paint ICC profile"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;
private:
    std::vector<ObjectId> object_ids_;
    PageId page_id_{};
    std::shared_ptr<const color::ProfileResource> destination_;
    std::vector<std::shared_ptr<const color::ProfileColorTransform>> transforms_;
    SnapshotPtr before_;
};

/// Converts every page with a resolved source ICC profile to one destination.
/// All source transforms are preflighted before the document is mutated.
class ConvertDocumentColorProfileCommand final : public DocumentCommand {
public:
    ConvertDocumentColorProfileCommand(
        std::shared_ptr<const color::ProfileResource> destination,
        std::vector<std::shared_ptr<const color::ProfileColorTransform>> transforms);
    std::string_view name() const noexcept override { return "Convert document ICC profile"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;
private:
    std::shared_ptr<const color::ProfileResource> destination_;
    std::vector<std::shared_ptr<const color::ProfileColorTransform>> transforms_;
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
