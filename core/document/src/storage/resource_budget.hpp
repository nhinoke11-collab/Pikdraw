#pragma once

#include <cstdint>

namespace pikdraw::document::storage_detail {

/// Returns half of currently available physical memory. Native readers use
/// this adaptive per-open quota before allocating decoded document payloads.
[[nodiscard]] uint64_t native_read_memory_budget_bytes() noexcept;

} // namespace pikdraw::document::storage_detail
