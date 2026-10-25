#pragma once

#include "pikdraw/document/model/object_id.hpp"
#include "pikdraw/document/model/object.hpp"
#include "pikdraw/document/model/import_fidelity_report.hpp"
#include "pikdraw/document/model/pdf_source_reference.hpp"
#include "pikdraw/document/snapshots/persistent_map.hpp"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace pikdraw::document {

using PageId = uint64_t;
using ArtboardId = uint64_t;

inline constexpr PageId kInvalidPageId = 0;
inline constexpr ArtboardId kInvalidArtboardId = 0;

namespace detail {
inline std::atomic<PageId>& page_id_counter() noexcept {
    static std::atomic<PageId> next_page_id{1};
    return next_page_id;
}
inline std::atomic<ArtboardId>& artboard_id_counter() noexcept {
    static std::atomic<ArtboardId> next_artboard_id{1};
    return next_artboard_id;
}
}

[[nodiscard]] inline PageId generate_page_id() noexcept {
    auto& counter = detail::page_id_counter();
    auto next = counter.load(std::memory_order_relaxed);
    while (next != kInvalidPageId) {
        const auto desired = next == std::numeric_limits<PageId>::max()
            ? kInvalidPageId : next + 1;
        if (counter.compare_exchange_weak(next, desired, std::memory_order_relaxed))
            return next;
    }
    return kInvalidPageId;
}

inline void reserve_page_id(PageId id) noexcept {
    auto& counter = detail::page_id_counter();
    auto next = counter.load(std::memory_order_relaxed);
    while (id != kInvalidPageId && next != kInvalidPageId && next <= id) {
        const auto desired = id == std::numeric_limits<PageId>::max()
            ? kInvalidPageId : id + 1;
        if (counter.compare_exchange_weak(next, desired, std::memory_order_relaxed))
            return;
    }
}

[[nodiscard]] inline ArtboardId generate_artboard_id() noexcept {
    auto& counter = detail::artboard_id_counter();
    auto next = counter.load(std::memory_order_relaxed);
    while (next != kInvalidArtboardId) {
        const auto desired = next == std::numeric_limits<ArtboardId>::max()
            ? kInvalidArtboardId : next + 1;
        if (counter.compare_exchange_weak(next, desired, std::memory_order_relaxed))
            return next;
    }
    return kInvalidArtboardId;
}

inline void reserve_artboard_id(ArtboardId id) noexcept {
    auto& counter = detail::artboard_id_counter();
    auto next = counter.load(std::memory_order_relaxed);
    while (id != kInvalidArtboardId && next != kInvalidArtboardId && next <= id) {
        const auto desired = id == std::numeric_limits<ArtboardId>::max()
            ? kInvalidArtboardId : id + 1;
        if (counter.compare_exchange_weak(next, desired, std::memory_order_relaxed))
            return;
    }
}

/// Export region inside one page scene; all regions share that page's object map.
struct Artboard {
    ArtboardId id{kInvalidArtboardId};
    PageId owner_page_id{kInvalidPageId};
    std::string name;
    double x_mm{0.0};
    double y_mm{0.0};
    double width_mm{0.0};
    double height_mm{0.0};
    bool linked_to_page_bounds{false};

    [[nodiscard]] bool is_valid_for(PageId owner) const noexcept {
        return id != kInvalidArtboardId && owner_page_id == owner && !name.empty() &&
            name.size() <= 1024 && std::isfinite(x_mm) && std::isfinite(y_mm) &&
            std::isfinite(width_mm) && std::isfinite(height_mm) &&
            width_mm > 0.0 && height_mm > 0.0;
    }
};

[[nodiscard]] inline Artboard make_page_bounds_artboard(PageId owner, const Page& page,
                                                         std::string name = "Artboard 1") {
    const auto id = generate_artboard_id();
    if (id == kInvalidArtboardId)
        throw std::overflow_error("Document artboard ID range exhausted");
    return Artboard{id, owner, std::move(name), 0.0, 0.0,
                    page.width_mm, page.height_mm, true};
}

/// One page and its page-local object root in an immutable document snapshot.
struct DocumentPage {
    PageId id{kInvalidPageId};
    std::string name;
    Page layout;
    PersistentObjectMap objects;
    std::optional<PdfSourcePageReference> pdf_source;
    std::vector<ImportFidelityReport> import_fidelity_reports;
    std::vector<Artboard> artboards;
};

[[nodiscard]] inline DocumentPage make_document_page(
    std::string name = "Page 1", Page layout = Page::make_default(),
    PersistentObjectMap objects = {},
    std::optional<PdfSourcePageReference> pdf_source = std::nullopt) {
    const auto id = generate_page_id();
    if (id == kInvalidPageId)
        throw std::overflow_error("Document page ID range exhausted");
    DocumentPage result{id, std::move(name), std::move(layout), std::move(objects),
                        std::move(pdf_source), {}, {}};
    result.artboards.push_back(make_page_bounds_artboard(result.id, result.layout));
    return result;
}

} // namespace pikdraw::document
