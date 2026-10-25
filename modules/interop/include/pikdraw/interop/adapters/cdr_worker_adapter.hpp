#pragma once

#include "pikdraw/interop/adapters/svg_worker_adapter.hpp"

#include <functional>

namespace pikdraw::interop {

using CdrMemoryAdmissionCallback =
    std::function<bool(uint64_t additional_bytes, std::string& diagnostic)>;

/// Converts supported CDR input streams through the pinned libcdr SVG generator, then
/// maps the generated vector hierarchy into the same validated native import records as SVG.
[[nodiscard]] bool BuildCdrImportRecords(
    std::span<const uint8_t> source,
    std::vector<CanonicalImportRecord>& records,
    std::string& diagnostic,
    const CdrMemoryAdmissionCallback& memory_admission = {});

/// Worker overload that releases its owned source snapshot as soon as the
/// synchronous libcdr bridge has finished consuming it.
[[nodiscard]] bool BuildCdrImportRecordsAndReleaseSource(
    std::vector<uint8_t>& source,
    std::vector<CanonicalImportRecord>& records,
    std::string& diagnostic,
    const CdrMemoryAdmissionCallback& memory_admission = {});

}  // namespace pikdraw::interop
