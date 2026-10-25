#pragma once

#include "pikdraw/production/measurement/measurement_import.hpp"

#include <filesystem>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace pikdraw::production::measurement {

struct MeasurementRunStoreResult {
    std::optional<MeasurementRun> run;
    std::string diagnostic;

    [[nodiscard]] explicit operator bool() const noexcept {
        return run.has_value() && diagnostic.empty();
    }
};

/// Versioned, checksummed run artifact. The reader validates all lengths,
/// metadata and patch values before publishing a run.
[[nodiscard]] std::vector<uint8_t> serialize_measurement_run(
    const MeasurementRun& run, std::string* error = nullptr);
[[nodiscard]] MeasurementRunStoreResult deserialize_measurement_run(
    std::span<const uint8_t> bytes);

/// Writes via a sibling temporary file then atomically replaces the target.
[[nodiscard]] bool save_measurement_run(
    const std::filesystem::path& path, const MeasurementRun& run,
    std::string* error = nullptr);
[[nodiscard]] MeasurementRunStoreResult load_measurement_run(
    const std::filesystem::path& path);

}  // namespace pikdraw::production::measurement
