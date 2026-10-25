#pragma once

#include "pikdraw/production/measurement/ciede2000.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pikdraw::production::measurement {

struct MeasurementCondition {
    std::string chart_hash;
    std::string source_sha256;
    std::string output_profile_sha256;
    std::string print_route;
    std::string instrument;
    std::string illuminant;
    std::string observer;
    std::string measurement_mode;
    std::string backing;
    std::string measured_at;
};

struct MeasuredPatch {
    std::string sample_id;
    LabValue lab;
    std::optional<std::array<double, 3>> xyz;
};

struct MeasurementRun {
    uint32_t schema_version{1};
    MeasurementCondition condition;
    std::vector<MeasuredPatch> patches;
};

enum class MeasurementImportError : uint8_t {
    None,
    EmptyInput,
    InputTooLarge,
    MalformedDocument,
    MissingFields,
    InvalidMetadata,
    InvalidValue,
    DuplicatePatch,
    TooManyPatches
};

struct MeasurementImportResult {
    std::optional<MeasurementRun> run;
    MeasurementImportError error{MeasurementImportError::None};
    std::string diagnostic;
    [[nodiscard]] explicit operator bool() const noexcept {
        return run.has_value() && error == MeasurementImportError::None;
    }
};

/// Strict parser for the documented PikDraw CGATS.17 subset and its CSV
/// equivalent. Unknown fields are ignored; execution and file references are
/// never interpreted. Limits: 64 MiB input and 250,000 unique patches.
[[nodiscard]] MeasurementImportResult parse_measurement_run(
    std::string_view input);

struct ReferencePatch {
    std::string sample_id;
    LabValue lab;
};

struct MeasurementComparison {
    bool complete{false};
    bool assessed{false};
    bool passed{false};
    size_t matched{0};
    size_t missing{0};
    size_t extra{0};
    double mean_delta_e{0.0};
    double p95_delta_e{0.0};
    double maximum_delta_e{0.0};
    std::vector<std::pair<std::string, double>> patch_delta_e;
    std::string diagnostic;
};

struct DeltaETolerance {
    double mean_max{0.0};
    double p95_max{0.0};
    double maximum_max{0.0};
};

/// Matches by stable sample ID and requires the chart/source/output hashes,
/// print route, and complete instrument/illuminant/observer/mode/backing to
/// match the expected condition before scoring.
[[nodiscard]] MeasurementComparison compare_measurement_run(
    const MeasurementRun& measured, const MeasurementCondition& expected,
    const std::vector<ReferencePatch>& reference,
    std::optional<DeltaETolerance> tolerance = std::nullopt);

} // namespace pikdraw::production::measurement
