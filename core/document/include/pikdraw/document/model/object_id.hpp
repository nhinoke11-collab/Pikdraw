#pragma once

#include <cstdint>
#include <atomic>
#include <limits>

namespace pikdraw::document {

using ObjectId = uint64_t;

inline constexpr ObjectId kInvalidObjectId = 0;

namespace detail {
inline std::atomic<ObjectId>& object_id_counter() noexcept {
    static std::atomic<ObjectId> next_id{1};
    return next_id;
}
}

/// Reserve imported/native IDs so subsequently generated IDs do not collide.
inline void reserve_object_id(ObjectId id) noexcept {
    auto& counter = detail::object_id_counter();
    auto next = counter.load(std::memory_order_relaxed);
    while (next != 0 && next <= id) {
        const auto desired = id == std::numeric_limits<ObjectId>::max() ? 0 : id + 1;
        if (counter.compare_exchange_weak(next, desired, std::memory_order_relaxed)) return;
    }
}

/// Zero indicates exhaustion; never wrap and reuse an ID.
[[nodiscard]] inline ObjectId generate_object_id() noexcept {
    auto& counter = detail::object_id_counter();
    auto next = counter.load(std::memory_order_relaxed);
    while (next != 0) {
        const auto desired = next == std::numeric_limits<ObjectId>::max() ? 0 : next + 1;
        if (counter.compare_exchange_weak(next, desired, std::memory_order_relaxed)) return next;
    }
    return kInvalidObjectId;
}

} // namespace pikdraw::document
