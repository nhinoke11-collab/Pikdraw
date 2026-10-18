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
} // namespace pikdraw::document
