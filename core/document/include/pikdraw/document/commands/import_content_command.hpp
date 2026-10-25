#pragma once

#include "pikdraw/document/commands/command.hpp"
#include "pikdraw/document/model/document_page.hpp"
#include "pikdraw/document/model/import_fidelity_report.hpp"
#include "pikdraw/document/snapshots/persistent_map.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pikdraw::document {

/// Normalized page payload used by multi-page import and Open workflows.
struct ImportedPageContent {
    std::string name;
    Page layout{Page::make_default()};
    std::vector<ObjectVariant> objects;
    uint32_t source_page_index{0};
    uint8_t source_page_rotation_quarters_clockwise{0};
    ImportFidelityReport import_fidelity_report;
};

/// Applies a page set in one document transaction and one Undo step.
class ImportPagesCommand final : public DocumentCommand {
public:
    explicit ImportPagesCommand(std::vector<ImportedPageContent> pages,
        PageId insert_after_page_id = kInvalidPageId, bool replace_document_pages = false)
        : pages_(std::move(pages)), insert_after_page_id_(insert_after_page_id),
          replace_document_pages_(replace_document_pages) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "Import Pages"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    std::vector<ImportedPageContent> pages_;
    std::vector<DocumentPage> prepared_pages_;
    std::vector<DocumentPage> previous_pages_;
    PageId insert_after_page_id_{kInvalidPageId};
    PageId previous_active_page_id_{kInvalidPageId};
    bool replace_document_pages_{false};
    bool prepared_{false};
    bool executed_{false};
};

/// Adds one already-normalized import proposal and its source layers atomically.
class ImportContentCommand final : public DocumentCommand {
public:
    ImportContentCommand(std::vector<Layer> layers, std::vector<ObjectVariant> objects,
        std::optional<std::pair<double, double>> source_page_size_mm = std::nullopt,
        PageId target_page_id = kInvalidPageId,
        ImportFidelityReport import_fidelity_report = {});

    [[nodiscard]] std::string_view name() const noexcept override { return "Import Content"; }
    CommandResult execute(Document& doc) override;
    CommandResult undo(Document& doc) override;

private:
    std::vector<Layer> imported_layers_;
    std::vector<ObjectVariant> imported_objects_;
    std::optional<std::pair<double, double>> source_page_size_mm_;
    std::vector<DocumentPage> before_pages_;
    PageId before_active_page_id_{kInvalidPageId};
    PageId target_page_id_{kInvalidPageId};
    ImportFidelityReport import_fidelity_report_;
    bool has_before_{false};
    bool executed_{false};
};

} // namespace pikdraw::document
