#pragma once

#include "pikdraw/interop/adapters/svg_worker_adapter.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace pikdraw::interop {

/// Decodes one bounded PNG/JPEG/TIFF frame and emits a native image plus ordered
/// pixel chunks. The caller must invoke this only inside the isolated worker.
[[nodiscard]] bool BuildRasterImportRecords(
    std::span<const uint8_t> source,
    std::vector<CanonicalImportRecord>& records,
    std::string& diagnostic);

} // namespace pikdraw::interop
