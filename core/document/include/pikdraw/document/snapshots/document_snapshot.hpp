#pragma once

#include "pikdraw/document/model/document_page.hpp"
#include "pikdraw/document/model/structure.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

namespace pikdraw::document {

/// Immutable document roots consumed by concurrent readers and format adapters.
class DocumentSnapshot {
public:
    DocumentSnapshot(uint64_t epoch, uint64_t revision, Page page,
                     PersistentObjectMap objects, std::string document_id = {},
                     DocumentContentMode content_mode = DocumentContentMode::Editable)
        : epoch_(epoch), revision_(revision), document_id_(std::move(document_id)),
          content_mode_(content_mode) {
        auto first_page = make_document_page("Page 1", std::move(page), std::move(objects));
        active_page_id_ = first_page.id;
        pages_.push_back(std::move(first_page));
        validate();
    }

    DocumentSnapshot(uint64_t epoch, uint64_t revision,
                     std::vector<DocumentPage> pages, PageId active_page_id,
                     std::string document_id = {},
                     DocumentContentMode content_mode = DocumentContentMode::Editable)
        : epoch_(epoch), revision_(revision), pages_(std::move(pages)),
          active_page_id_(active_page_id), document_id_(std::move(document_id)),
          content_mode_(content_mode) {
        validate();
    }

    [[nodiscard]] const std::string& document_id() const noexcept { return document_id_; }
    [[nodiscard]] DocumentContentMode content_mode() const noexcept { return content_mode_; }
    [[nodiscard]] bool is_content_locked() const noexcept {
        return content_mode_ == DocumentContentMode::PdfPrintLocked;
    }
    [[nodiscard]] uint64_t epoch() const noexcept { return epoch_; }
    [[nodiscard]] uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] const Page& page() const noexcept { return active_page().layout; }
    [[nodiscard]] color::DocumentColorMode color_mode() const noexcept { return page().color_mode; }
    [[nodiscard]] const std::string& color_profile_name() const noexcept { return page().color_profile_name; }
    [[nodiscard]] int32_t raster_dpi() const noexcept { return page().raster_dpi; }
    [[nodiscard]] color::DocumentColorMode page_color_mode(PageId id) const {
        const auto* p = find_page(id);
        return p ? p->layout.color_mode : color_mode();
    }
    [[nodiscard]] int32_t page_raster_dpi(PageId id) const {
        const auto* p = find_page(id);
        return p ? p->layout.raster_dpi : raster_dpi();
    }
    [[nodiscard]] const PersistentObjectMap& objects() const noexcept {
        return active_page().objects;
    }
    [[nodiscard]] const std::vector<DocumentPage>& pages() const noexcept { return pages_; }
    [[nodiscard]] size_t page_count() const noexcept { return pages_.size(); }
    [[nodiscard]] PageId active_page_id() const noexcept { return active_page_id_; }
    [[nodiscard]] const DocumentPage& active_page() const noexcept {
        return *find_page(active_page_id_);
    }
    [[nodiscard]] const DocumentPage* find_page(PageId id) const noexcept {
        for (const auto& page : pages_)
            if (page.id == id) return &page;
        return nullptr;
    }

    // Legacy object APIs remain scoped to the active page.
    [[nodiscard]] size_t object_count() const noexcept { return objects().size(); }
    [[nodiscard]] bool contains_object(ObjectId id) const { return objects().contains(id); }
    [[nodiscard]] std::optional<ObjectVariant> get_object(ObjectId id) const {
        return objects().get(id);
    }

    [[nodiscard]] geometry::Rect2D bounds() const {
        return geometry::Rect2D{0.0, 0.0, page().width_mm, page().height_mm};
    }

private:
    void validate() {
        if (pages_.empty())
            throw std::invalid_argument("A document must contain at least one page");
        if (content_mode_ != DocumentContentMode::Editable &&
            content_mode_ != DocumentContentMode::PdfPrintLocked)
            throw std::invalid_argument("Invalid document content mode");
        std::unordered_set<PageId> page_ids;
        std::unordered_set<uint64_t> stable_ids;
        uint64_t maximum_stable_id = 0;
        size_t effect_count = 0;
        size_t gradient_stop_count = 0;
        bool active_found = false;
        for (const auto& page : pages_) {
            if (page.pdf_source) {
                const auto& reference = *page.pdf_source;
                const auto& asset = reference.asset;
                const auto valid_hex = [](char value) {
                    return (value >= '0' && value <= '9') ||
                           (value >= 'a' && value <= 'f') ||
                           (value >= 'A' && value <= 'F');
                };
                if (!asset || !asset->bytes || asset->bytes->empty() ||
                    asset->page_count == 0 || reference.source_page_index >= asset->page_count ||
                    reference.rotation_quarters_clockwise > 3 ||
                    asset->sha256_hex.size() != 64 ||
                    !std::all_of(asset->sha256_hex.begin(), asset->sha256_hex.end(), valid_hex))
                    throw std::invalid_argument("Invalid PDF source page reference");
                if (content_mode_ != DocumentContentMode::PdfPrintLocked)
                    throw std::invalid_argument("Full-page PDF references require locked content mode");
                if (!page.objects.empty())
                    throw std::invalid_argument("Locked PDF source pages cannot contain editable objects");
            } else if (content_mode_ == DocumentContentMode::PdfPrintLocked) {
                throw std::invalid_argument("Every locked PDF page requires a source reference");
            }
            if (!color::valid_document_color_mode(page.layout.color_mode))
                throw std::invalid_argument("Invalid page color mode");
            if (page.layout.raster_dpi != 72 && page.layout.raster_dpi != 150 && page.layout.raster_dpi != 300)
                throw std::invalid_argument("Invalid page raster DPI");
            if (page.id == kInvalidPageId || !page_ids.insert(page.id).second)
                throw std::invalid_argument("Document page IDs must be unique and nonzero");
            if (page.id == active_page_id_) active_found = true;
            validate_document_structure(page.layout, page.objects);
            for (const auto& [id, object] : page.objects.all()) {
                add_stable_id(stable_ids, maximum_stable_id, id);
                const Appearance* appearance = nullptr;
                if (const auto* path = std::get_if<PathObject>(&object)) {
                    appearance = &path->appearance;
                    if (path->fill_gradient) {
                        add_stable_id(stable_ids, maximum_stable_id, path->fill_gradient->id);
                        if (path->fill_gradient->stops.size() >
                            kMaxDocumentGradientStops - gradient_stop_count)
                            throw std::invalid_argument("Document exceeds the maximum gradient stop count");
                        gradient_stop_count += path->fill_gradient->stops.size();
                        for (const auto& stop : path->fill_gradient->stops)
                            add_stable_id(stable_ids, maximum_stable_id, stop.id);
                    }
                    if (path->authoring_source)
                        for (const auto& subpath : path->authoring_source->subpaths) {
                            add_stable_id(stable_ids, maximum_stable_id, subpath.id.value());
                            for (const auto& node : subpath.nodes)
                                add_stable_id(stable_ids, maximum_stable_id, node.id.value());
                            for (const auto& segment : subpath.segments)
                                add_stable_id(stable_ids, maximum_stable_id, segment.id.value());
                        }
                } else if (const auto* text = std::get_if<TextObject>(&object)) {
                    appearance = &text->appearance;
                }
                if (appearance) {
                    if (appearance->effects.size() > kMaxEffectsPerDocument - effect_count)
                        throw std::invalid_argument("Document exceeds the maximum effect node count");
                    effect_count += appearance->effects.size();
                    for (const auto& effect : appearance->effects)
                        add_stable_id(stable_ids, maximum_stable_id, effect.id);
                }
                if (effect_count > kMaxEffectsPerDocument)
                    throw std::invalid_argument("Document exceeds the maximum effect node count");
                if (gradient_stop_count > kMaxDocumentGradientStops)
                    throw std::invalid_argument("Document exceeds the maximum gradient stop count");
            }
        }
        if (!active_found)
            throw std::invalid_argument("Active page ID is not present in the document");
        for (const auto& page : pages_) reserve_page_id(page.id);
        reserve_object_id(maximum_stable_id);
    }

    static void add_stable_id(std::unordered_set<uint64_t>& ids,
                              uint64_t& maximum_stable_id, uint64_t id) {
        if (id == 0 || !ids.insert(id).second)
            throw std::invalid_argument("Stable IDs must be unique across the document");
        maximum_stable_id = std::max(maximum_stable_id, id);
    }

    uint64_t epoch_{1};
    uint64_t revision_{0};
    std::vector<DocumentPage> pages_;
    PageId active_page_id_{kInvalidPageId};
    std::string document_id_;
    DocumentContentMode content_mode_{DocumentContentMode::Editable};
};

using SnapshotPtr = std::shared_ptr<const DocumentSnapshot>;

} // namespace pikdraw::document
