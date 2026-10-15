#pragma once

#include <cstdint>
#include <atomic>

namespace pikdraw::document {

using ObjectId = uint64_t;

inline constexpr ObjectId kInvalidObjectId = 0;

/// Thread-safe monotonic generator for unique stable object identifiers
[[nodiscard]] inline ObjectId generate_object_id() noexcept {
    static std::atomic<ObjectId> next_id{1};
    return next_id.fetch_add(1, std::memory_order_relaxed);
}

} // namespace pikdraw::document
