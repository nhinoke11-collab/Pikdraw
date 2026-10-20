#pragma once

#include "pikdraw/document/model/object_id.hpp"
#include "pikdraw/document/model/object.hpp"
#include "pikdraw/document/model/pdf_source_reference.hpp"
#include "pikdraw/document/snapshots/persistent_map.hpp"

#include <atomic>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace pikdraw::document {

using PageId = uint64_t;

inline constexpr PageId kInvalidPageId = 0;

namespace detail {
inline std::atomic<PageId>& page_id_counter() noexcept {
    static std::atomic<PageId> next_page_id{1};
    return next_page_id;
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

/// One page and its page-local object root in an immutable document snapshot.
struct DocumentPage {
    PageId id{kInvalidPageId};
    std::string name;
    Page layout;
    PersistentObjectMap objects;
    std::optional<PdfSourcePageReference> pdf_source;
};

[[nodiscard]] inline DocumentPage make_document_page(
    std::string name = "Page 1", Page layout = Page::make_default(),
    PersistentObjectMap objects = {},
    std::optional<PdfSourcePageReference> pdf_source = std::nullopt) {
    const auto id = generate_page_id();
    if (id == kInvalidPageId)
        throw std::overflow_error("Document page ID range exhausted");
    return DocumentPage{id, std::move(name), std::move(layout), std::move(objects),
                        std::move(pdf_source)};
}

} // namespace pikdraw::document
