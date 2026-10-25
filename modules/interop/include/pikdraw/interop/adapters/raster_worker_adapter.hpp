#pragma once

#include "pikdraw/interop/adapters/raster_image_decoder.hpp"
#include "pikdraw/interop/adapters/svg_worker_adapter.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace pikdraw::interop {

using RasterImportRecordSink = std::function<bool(CanonicalImportRecord&&)>;
using RasterMemoryAdmissionCallback =
    std::function<bool(uint64_t additional_bytes, std::string& diagnostic)>;

/// Decodes a selected frame and emits proposal records one at a time. The sink
/// must consume each record before returning to keep peak memory bounded.
[[nodiscard]] bool StreamRasterImportRecords(
    std::span<const uint8_t> source,
    uint32_t frame_index,
    const RasterImportRecordSink& sink,
    std::string& diagnostic,
    const RasterMemoryAdmissionCallback& memory_admission = {},
    bool preserve_16bit = false);

/// Streams from a seekable source without materializing encoded bytes or a
/// full decoded frame. The source must remain immutable for the call duration.
[[nodiscard]] bool StreamRasterImportRecords(
    const RasterByteSource& source,
    uint32_t frame_index,
    const RasterImportRecordSink& sink,
    std::string& diagnostic,
    const RasterMemoryAdmissionCallback& memory_admission = {},
    bool preserve_16bit = false);

/// Decodes one bounded PNG/JPEG/TIFF frame and emits a native image plus ordered
/// pixel chunks. The caller must invoke this only inside the isolated worker.
[[nodiscard]] bool BuildRasterImportRecords(
    std::span<const uint8_t> source,
    std::vector<CanonicalImportRecord>& records,
    std::string& diagnostic,
    uint32_t frame_index = 0);

} // namespace pikdraw::interop
