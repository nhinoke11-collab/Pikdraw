#pragma once

#include "pikdraw/interop/adapters/svg_worker_adapter.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace pikdraw::interop {

inline constexpr size_t kEpsImportMaxObjects = 90000;

/// Converts a deliberately bounded, non-executing PostScript path subset to native C6 records.
[[nodiscard]] bool BuildEpsImportRecords(
    std::span<const uint8_t> source,
    std::vector<CanonicalImportRecord>& records,
    std::string& diagnostic);

} // namespace pikdraw::interop
