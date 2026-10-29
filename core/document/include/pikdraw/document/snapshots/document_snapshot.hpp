#pragma once

#include "pikdraw/document/model/document_page.hpp"
#include "pikdraw/document/model/structure.hpp"
#include "pikdraw/document/hierarchy/scene_hierarchy.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

namespace pikdraw::document {

struct SnapshotChangesSince {
    bool contiguous{false};
    bool requires_full_query{true};
    uint64_t from_revision{0};
    uint64_t to_revision{0};
    std::vector<ObjectId> changed_ids;
};

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
        validate(nullptr);
    }

    DocumentSnapshot(uint64_t epoch, uint64_t revision,
                     std::vector<DocumentPage> pages, PageId active_page_id,
                     std::string document_id = {},
                     DocumentContentMode content_mode = DocumentContentMode::Editable,
                     const DocumentSnapshot* previous = nullptr)
        : epoch_(epoch), revision_(revision), pages_(std::move(pages)),
          active_page_id_(active_page_id), document_id_(std::move(document_id)),
          content_mode_(content_mode) {
        validate(previous);
    }

    /// Publishes a new revision of an already validated immutable state.
    DocumentSnapshot(uint64_t epoch, uint64_t revision,
                     const DocumentSnapshot& validated_state)
        : epoch_(epoch), revision_(revision), pages_(validated_state.pages_),
          active_page_id_(validated_state.active_page_id_),
          document_id_(validated_state.document_id_),
          content_mode_(validated_state.content_mode_),
          hierarchy_(validated_state.hierarchy_),
          revision_changes_(validated_state.revision_changes_) {}

    /// Revision-changing restores invalidate object deltas and force a fresh query.
    DocumentSnapshot(uint64_t epoch, uint64_t revision,
                     const DocumentSnapshot& validated_state,
                     const DocumentSnapshot* previous)
        : epoch_(epoch), revision_(revision), pages_(validated_state.pages_),
          active_page_id_(validated_state.active_page_id_),
          document_id_(validated_state.document_id_),
          content_mode_(validated_state.content_mode_),
          hierarchy_(validated_state.hierarchy_) {
        record_revision_change(previous, false, {});
    }

    [[nodiscard]] const std::string& document_id() const noexcept { return document_id_; }
    [[nodiscard]] DocumentContentMode content_mode() const noexcept { return content_mode_; }
    [[nodiscard]] bool is_content_locked() const noexcept {
        return content_mode_ == DocumentContentMode::PdfPrintLocked;
    }
    [[nodiscard]] uint64_t epoch() const noexcept { return epoch_; }
    [[nodiscard]] uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] const Page& page() const noexcept { return active_page().layout; }
    [[nodiscard]] const SceneHierarchy& hierarchy() const noexcept { return *hierarchy_; }
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

    [[nodiscard]] SnapshotChangesSince changes_since(
        uint64_t from_revision) const {
        SnapshotChangesSince result;
        result.from_revision = from_revision;
        result.to_revision = revision_;
        if (from_revision == revision_) {
            result.contiguous = true;
            result.requires_full_query = false;
            return result;
        }
        if (from_revision > revision_ || revision_changes_.empty()) return result;

        uint64_t cursor = from_revision;
        std::unordered_set<ObjectId> seen;
        for (const auto& change : revision_changes_) {
            if (change->to_revision <= cursor) continue;
            if (change->from_revision != cursor || change->requires_full_query) {
                result.changed_ids.clear();
                return result;
            }
            for (const auto id : change->changed_ids)
                if (seen.insert(id).second) {
                    if (result.changed_ids.size() >= kMaxDeltaObjectIds) {
                        result.changed_ids.clear();
                        return result;
                    }
                    result.changed_ids.push_back(id);
                }
            cursor = change->to_revision;
            if (cursor == revision_) {
                result.contiguous = true;
                result.requires_full_query = false;
                return result;
            }
        }
        result.changed_ids.clear();
        return result;
    }

private:
    struct RevisionChangeRecord {
        uint64_t from_revision{0};
        uint64_t to_revision{0};
        bool requires_full_query{true};
        std::vector<ObjectId> changed_ids;
    };

    void record_revision_change(const DocumentSnapshot* previous,
                                bool incremental,
                                const std::vector<ObjectId>& changed_ids) {
        revision_changes_.clear();
        if (!previous || previous->epoch_ != epoch_ ||
            previous->document_id_ != document_id_)
            return;
        revision_changes_ = previous->revision_changes_;
        if (revision_ == previous->revision_) return;
        auto change = std::make_shared<RevisionChangeRecord>();
        change->from_revision = previous->revision_;
        change->to_revision = revision_;
        change->requires_full_query =
            !incremental || previous->revision_ == std::numeric_limits<uint64_t>::max() ||
            revision_ != previous->revision_ + 1 ||
            changed_ids.size() > kMaxDeltaObjectIds;
        if (!change->requires_full_query)
            change->changed_ids = changed_ids;
        revision_changes_.push_back(std::move(change));
        if (revision_changes_.size() > kMaxRetainedChangeRevisions)
            revision_changes_.erase(revision_changes_.begin());
    }

    static constexpr size_t kMaxRetainedChangeRevisions = 128;
    static constexpr size_t kMaxDeltaObjectIds = 4096;

    static bool same_layer_layout(const Page& left, const Page& right) {
        if (left.width_mm != right.width_mm ||
            left.height_mm != right.height_mm ||
            left.color_mode != right.color_mode ||
            left.color_profile_name != right.color_profile_name ||
            left.color_profile_resource != right.color_profile_resource ||
            left.raster_dpi != right.raster_dpi ||
            left.color_mode_explicit != right.color_mode_explicit ||
            left.raster_dpi_explicit != right.raster_dpi_explicit ||
            left.display_length_unit != right.display_length_unit ||
            left.display_length_unit_explicit != right.display_length_unit_explicit ||
            left.layers.size() != right.layers.size())
            return false;
        for (size_t index = 0; index < left.layers.size(); ++index) {
            const auto& a = left.layers[index];
            const auto& b = right.layers[index];
            if (a.name != b.name || a.visible != b.visible ||
                a.locked != b.locked || a.printable != b.printable ||
                a.color_rgba != b.color_rgba || a.object_ids != b.object_ids)
                return false;
        }
        return true;
    }

    static bool same_artboards(const std::vector<Artboard>& left,
                               const std::vector<Artboard>& right) {
        if (left.size() != right.size()) return false;
        for (size_t index = 0; index < left.size(); ++index) {
            const auto& a = left[index];
            const auto& b = right[index];
            if (a.id != b.id || a.owner_page_id != b.owner_page_id ||
                a.name != b.name || a.x_mm != b.x_mm || a.y_mm != b.y_mm ||
                a.width_mm != b.width_mm || a.height_mm != b.height_mm ||
                a.linked_to_page_bounds != b.linked_to_page_bounds)
                return false;
        }
        return true;
    }

    static bool same_effect_ids(const Appearance& left,
                                const Appearance& right) {
        if (left.effects.size() != right.effects.size()) return false;
        for (size_t index = 0; index < left.effects.size(); ++index)
            if (left.effects[index].id != right.effects[index].id) return false;
        return true;
    }

    static bool same_editable_path_ids(const EditablePath& left,
                                       const EditablePath& right) {
        if (left.subpaths.size() != right.subpaths.size()) return false;
        for (size_t index = 0; index < left.subpaths.size(); ++index) {
            const auto& a = left.subpaths[index];
            const auto& b = right.subpaths[index];
            if (a.id != b.id || a.nodes.size() != b.nodes.size() ||
                a.segments.size() != b.segments.size())
                return false;
            for (size_t node = 0; node < a.nodes.size(); ++node)
                if (a.nodes[node].id != b.nodes[node].id) return false;
            for (size_t segment = 0; segment < a.segments.size(); ++segment)
                if (a.segments[segment].id != b.segments[segment].id)
                    return false;
        }
        return true;
    }

    static bool same_object_identity_layout(const ObjectVariant& left,
                                           const ObjectVariant& right) {
        if (left.index() != right.index()) return false;
        if (const auto* a = std::get_if<PathObject>(&left)) {
            const auto& b = std::get<PathObject>(right);
            if (a->id != b.id || !same_effect_ids(a->appearance, b.appearance) ||
                static_cast<bool>(a->authoring_source) !=
                    static_cast<bool>(b.authoring_source) ||
                static_cast<bool>(a->fill_gradient) !=
                    static_cast<bool>(b.fill_gradient))
                return false;
            if (a->authoring_source &&
                a->authoring_source != b.authoring_source &&
                !same_editable_path_ids(*a->authoring_source,
                                        *b.authoring_source))
                return false;
            if (a->fill_gradient) {
                if (a->fill_gradient->id != b.fill_gradient->id ||
                    a->fill_gradient->stops.size() !=
                        b.fill_gradient->stops.size())
                    return false;
                if (a->fill_gradient != b.fill_gradient)
                    for (size_t index = 0;
                         index < a->fill_gradient->stops.size(); ++index)
                        if (a->fill_gradient->stops[index].id !=
                            b.fill_gradient->stops[index].id)
                            return false;
            }
            return true;
        }
        if (const auto* a = std::get_if<TextObject>(&left)) {
            const auto& b = std::get<TextObject>(right);
            return a->id == b.id && same_effect_ids(a->appearance, b.appearance) &&
                a->next_frame_id == b.next_frame_id &&
                a->prev_frame_id == b.prev_frame_id &&
                a->path_binding_id == b.path_binding_id;
        }
        if (const auto* a = std::get_if<ImageObject>(&left))
            return a->id == std::get<ImageObject>(right).id;
        // Group child membership determines global reachability and clipping
        // invariants, so group edits retain the full structural validation path.
        return false;
    }

    static std::vector<ObjectId> incremental_validation_ids(
        const DocumentSnapshot& current, const DocumentSnapshot* previous) {
        if (!previous || current.epoch_ != previous->epoch_ ||
            current.document_id_ != previous->document_id_ ||
            current.content_mode_ != previous->content_mode_ ||
            current.active_page_id_ != previous->active_page_id_ ||
            current.pages_.size() != previous->pages_.size())
            return {};
        const auto* active_page = current.find_page(current.active_page_id_);
        if (!active_page) return {};
        const auto changed_ids = active_page->objects.changed_ids();
        if (changed_ids.empty()) return {};
        for (size_t index = 0; index < current.pages_.size(); ++index) {
            const auto& page = current.pages_[index];
            const auto& old = previous->pages_[index];
            if (page.id != old.id || page.name != old.name ||
                !same_layer_layout(page.layout, old.layout) ||
                !same_artboards(page.artboards, old.artboards) ||
                page.pdf_source || old.pdf_source ||
                page.import_fidelity_reports != old.import_fidelity_reports)
                return {};
            if (page.id != current.active_page_id_) {
                if (!page.objects.shares_root_with(old.objects)) return {};
                continue;
            }
            if (page.objects.size() != old.objects.size()) return {};
            for (const auto id : changed_ids) {
                const auto* before = old.objects.lookup_ref(id);
                const auto* after = page.objects.lookup_ref(id);
                if (!before || !after ||
                    !same_object_identity_layout(*before, *after))
                    return {};
            }
        }
        return changed_ids;
    }

    void validate(const DocumentSnapshot* previous) {
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
        std::unordered_set<ArtboardId> artboard_ids;
        const auto changed_ids = incremental_validation_ids(*this, previous);
        const bool incremental = !changed_ids.empty();
        bool active_found = false;
        for (const auto& page : pages_) {
            for (const auto& report : page.import_fidelity_reports)
                if (!report.is_valid())
                    throw std::invalid_argument("Document has invalid import fidelity report metadata");
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
                const uint64_t preview_pixels =
                    static_cast<uint64_t>(reference.preview_width_px) *
                    reference.preview_height_px;
                if ((reference.preview_rgba == nullptr) !=
                        (reference.preview_width_px == 0 && reference.preview_height_px == 0) ||
                    (reference.preview_rgba &&
                     (preview_pixels == 0 || preview_pixels > UINT32_MAX / 4u ||
                      reference.preview_width_px > INT32_MAX ||
                      reference.preview_height_px > INT32_MAX ||
                      reference.preview_rgba->size() != preview_pixels)))
                    throw std::invalid_argument("Invalid PDF source page preview cache");
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
            if (page.artboards.size() > 10000)
                throw std::invalid_argument("A page exceeds the maximum artboard count");
            for (const auto& artboard : page.artboards) {
                if (!artboard.is_valid_for(page.id) || !artboard_ids.insert(artboard.id).second)
                    throw std::invalid_argument("Invalid or duplicate document artboard");
                if (artboard.linked_to_page_bounds &&
                    (artboard.x_mm != 0.0 || artboard.y_mm != 0.0 ||
                     artboard.width_mm != page.layout.width_mm ||
                     artboard.height_mm != page.layout.height_mm))
                    throw std::invalid_argument("Page-linked artboard bounds are inconsistent");
                reserve_artboard_id(artboard.id);
            }
            if (incremental) {
                if (page.id == active_page_id_) {
                    for (const auto id : changed_ids) {
                        const auto* object = page.objects.lookup_ref(id);
                        if (!object)
                            throw std::invalid_argument(
                                "Incremental snapshot edit removed an object");
                        validate_object_source(id, *object);
                    }
                }
            } else {
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
        }
        if (!active_found)
            throw std::invalid_argument("Active page ID is not present in the document");
        const auto* previous_page = previous &&
                previous->active_page_id() == active_page_id_
            ? previous->find_page(active_page_id_) : nullptr;
        if (previous_page) {
            const auto changed_ids = objects().changed_ids();
            hierarchy_ = std::make_shared<const SceneHierarchy>(
                page(), objects(), previous->hierarchy(), previous_page->layout,
                previous_page->objects, changed_ids);
        } else {
            hierarchy_ = std::make_shared<const SceneHierarchy>(page(), objects());
        }
        record_revision_change(previous, incremental, changed_ids);
        for (auto& page : pages_)
            page.objects = page.objects.without_change_tracking();
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
    std::shared_ptr<const SceneHierarchy> hierarchy_;
    std::vector<std::shared_ptr<const RevisionChangeRecord>> revision_changes_;
};

using SnapshotPtr = std::shared_ptr<const DocumentSnapshot>;

} // namespace pikdraw::document
