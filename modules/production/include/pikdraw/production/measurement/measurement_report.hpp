#pragma once

#include "pikdraw/production/measurement/measurement_import.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace pikdraw::production::measurement {

/// Creates a bounded CSV report for a comparison already computed by
/// compare_measurement_run. The report keeps unassessed results explicit and
/// protects imported patch identifiers from spreadsheet formula execution.
[[nodiscard]] std::optional<std::string> serialize_measurement_comparison_csv(
    const MeasurementRun& measured, const MeasurementRun& reference,
    const MeasurementComparison& comparison,
    std::optional<DeltaETolerance> tolerance = std::nullopt,
    std::string* error = nullptr);

/// Atomically saves the comparison report beside the selected destination.
[[nodiscard]] bool save_measurement_comparison_csv(
    const std::filesystem::path& path, const MeasurementRun& measured,
    const MeasurementRun& reference, const MeasurementComparison& comparison,
    std::optional<DeltaETolerance> tolerance = std::nullopt,
    std::string* error = nullptr);

}  // namespace pikdraw::production::measurement
