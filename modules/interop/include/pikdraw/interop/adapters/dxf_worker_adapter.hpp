#pragma once

#include "pikdraw/interop/adapters/svg_worker_adapter.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace pikdraw::interop {

/// Converts a complete bounded DXF parse into canonical C6 records or returns
/// no records.
[[nodiscard]] bool
BuildDxfImportRecords(std::span<const uint8_t> source,
                      std::vector<CanonicalImportRecord> &records,
                      std::string &diagnostic);

} // namespace pikdraw::interop
