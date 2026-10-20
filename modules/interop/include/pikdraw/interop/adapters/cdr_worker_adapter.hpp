#pragma once

#include "pikdraw/interop/adapters/svg_worker_adapter.hpp"

namespace pikdraw::interop {

/// Converts supported CDR input streams through the pinned libcdr SVG generator, then
/// maps the generated vector hierarchy into the same validated native import records as SVG.
[[nodiscard]] bool BuildCdrImportRecords(
    std::span<const uint8_t> source,
    std::vector<CanonicalImportRecord>& records,
    std::string& diagnostic);

}  // namespace pikdraw::interop
