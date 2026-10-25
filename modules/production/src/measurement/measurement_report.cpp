#include "pikdraw/production/measurement/measurement_report.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pikdraw::production::measurement {
namespace {
constexpr size_t kMaximumReportBytes = 128u * 1024u * 1024u;
constexpr size_t kMaximumPatches = 250000;

bool valid_text(std::string_view value, size_t maximum) {
    if (value.empty() || value.size() > maximum) return false;
    for (const unsigned char ch : value)
        if (ch == 0 || ch < 0x20 || ch == 0x7f) return false;
    return true;
}

bool valid_hash(std::string_view value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(),
        [](unsigned char ch) { return std::isxdigit(ch) != 0; });
}

bool valid_condition(const MeasurementCondition& condition) {
    return valid_hash(condition.chart_hash) &&
        valid_hash(condition.source_sha256) &&
        valid_hash(condition.output_profile_sha256) &&
        valid_text(condition.print_route, 64) &&
        valid_text(condition.instrument, 256) &&
        valid_text(condition.illuminant, 32) &&
        valid_text(condition.observer, 16) &&
        valid_text(condition.measurement_mode, 32) &&
        valid_text(condition.backing, 64) &&
        valid_text(condition.measured_at, 64);
}

bool valid_run_for_report(const MeasurementRun& run) {
    if (run.schema_version != 1 || !valid_condition(run.condition) ||
        run.patches.empty() || run.patches.size() > kMaximumPatches)
        return false;
    std::unordered_set<std::string_view> ids;
    ids.reserve(run.patches.size());
    for (const auto& patch : run.patches) {
        if (!valid_text(patch.sample_id, 256) ||
            !std::isfinite(patch.lab.l) || !std::isfinite(patch.lab.a) ||
            !std::isfinite(patch.lab.b) || patch.lab.l < 0.0 ||
            patch.lab.l > 100.0 || patch.lab.a < -200.0 ||
            patch.lab.a > 200.0 || patch.lab.b < -200.0 ||
            patch.lab.b > 200.0 || !ids.insert(patch.sample_id).second)
            return false;
        if (patch.xyz)
            for (const double value : *patch.xyz)
                if (!std::isfinite(value) || value < 0.0 || value > 10000.0)
                    return false;
    }
    return true;
}

std::string csv_cell(std::string_view value) {
    std::string safe;
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    if (first != value.end() && (*first == '=' || *first == '+' ||
        *first == '-' || *first == '@'))
        safe.push_back('\'');
    for (const char ch : value) {
        safe.push_back(ch);
        if (ch == '"') safe.push_back('"');
    }
    return '"' + safe + '"';
}

std::string number(double value) {
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                      value, std::chars_format::general, 12);
    if (result.ec != std::errc{}) return {};
    return std::string(buffer.data(), result.ptr);
}

bool append_cells(std::string& output, std::span<const std::string> cells) {
    bool first = true;
    for (const auto& cell : cells) {
        if (!first) output.push_back(',');
        first = false;
        output += csv_cell(cell);
        if (output.size() > kMaximumReportBytes) return false;
    }
    output += "\r\n";
    return output.size() <= kMaximumReportBytes;
}

bool append_row(std::string& output, std::initializer_list<std::string> cells) {
    return append_cells(output, std::span<const std::string>(cells.begin(), cells.size()));
}

bool append_patch_row(std::string& output, std::string_view sample_id,
                      std::string_view delta, std::string_view patch_status) {
    std::array<std::string, 35> row{};
    row[0] = "patch";
    row[32] = sample_id;
    row[33] = delta;
    row[34] = patch_status;
    return append_cells(output, std::span<const std::string>(row));
}

std::string status(const MeasurementComparison& comparison) {
    if (!comparison.complete) return "INCOMPLETE";
    if (!comparison.assessed) return "NOT_ASSESSED";
    return comparison.passed ? "PASS" : "FAIL";
}

bool write_atomic(const std::filesystem::path& target, std::string_view bytes,
                  std::string* error) {
    static std::atomic_uint64_t sequence{0};
    auto temporary = target;
    temporary += ".tmp-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
        std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
    try {
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file || !file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
                if (error) *error = "Could not write the complete measurement report.";
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
                return false;
            }
            file.flush();
            if (!file) {
                if (error) *error = "Could not flush the measurement report.";
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
                return false;
            }
        }
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), target.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            if (error) *error = "Could not atomically replace the measurement report.";
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
#else
        std::error_code ec;
        std::filesystem::rename(temporary, target, ec);
        if (ec) {
            if (error) *error = "Could not atomically replace the measurement report.";
            std::filesystem::remove(temporary, ec);
            return false;
        }
#endif
        return true;
    } catch (...) {
        if (error) *error = "Unexpected error while saving the measurement report.";
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
}
}  // namespace

std::optional<std::string> serialize_measurement_comparison_csv(
    const MeasurementRun& measured, const MeasurementRun& reference,
    const MeasurementComparison& comparison,
    std::optional<DeltaETolerance> tolerance, std::string* error) {
    auto fail = [&](std::string message) -> std::optional<std::string> {
        if (error) *error = std::move(message);
        return std::nullopt;
    };
    if (!valid_run_for_report(measured) || !valid_run_for_report(reference) ||
        !valid_text(comparison.diagnostic, 4096) ||
        !std::isfinite(comparison.mean_delta_e) ||
        !std::isfinite(comparison.p95_delta_e) ||
        !std::isfinite(comparison.maximum_delta_e) ||
        comparison.matched > measured.patches.size() ||
        comparison.matched > reference.patches.size() ||
        comparison.missing > reference.patches.size() ||
        comparison.extra > measured.patches.size() ||
        (comparison.assessed && (!comparison.complete || !tolerance)) ||
        (comparison.passed && !comparison.assessed))
        return fail("Measurement comparison report inputs are invalid.");
    if (tolerance && (!std::isfinite(tolerance->mean_max) ||
        !std::isfinite(tolerance->p95_max) || !std::isfinite(tolerance->maximum_max) ||
        tolerance->mean_max < 0.0 || tolerance->p95_max < 0.0 ||
        tolerance->maximum_max < 0.0))
        return fail("Measurement comparison report tolerance is invalid.");
    if (comparison.complete &&
        (comparison.missing != 0 || comparison.extra != 0 ||
         comparison.matched != measured.patches.size() ||
         comparison.matched != reference.patches.size()))
        return fail("Measurement comparison completion state is inconsistent.");
    if (comparison.assessed &&
        comparison.passed !=
            (comparison.mean_delta_e <= tolerance->mean_max &&
             comparison.p95_delta_e <= tolerance->p95_max &&
             comparison.maximum_delta_e <= tolerance->maximum_max))
        return fail("Measurement comparison pass/fail state does not match its limits.");

    std::unordered_set<std::string_view> measured_ids;
    std::unordered_set<std::string_view> reference_ids;
    measured_ids.reserve(measured.patches.size());
    reference_ids.reserve(reference.patches.size());
    for (const auto& patch : reference.patches)
        if (patch.sample_id.empty() || !reference_ids.insert(patch.sample_id).second)
            return fail("Reference run contains duplicate or empty patch identifiers.");
    for (const auto& patch : measured.patches)
        if (patch.sample_id.empty() || !measured_ids.insert(patch.sample_id).second)
            return fail("Measured run contains duplicate or empty patch identifiers.");
    std::unordered_map<std::string_view, double> deltas;
    deltas.reserve(comparison.patch_delta_e.size());
    for (const auto& [id, delta] : comparison.patch_delta_e) {
        if (id.empty() || !std::isfinite(delta) || delta < 0.0 ||
            !measured_ids.contains(id) || !reference_ids.contains(id) ||
            !deltas.emplace(id, delta).second)
            return fail("Measurement comparison report contains invalid patch results.");
    }
    if (deltas.size() != comparison.matched)
        return fail("Measurement comparison report patch count is inconsistent.");
    std::string output;
    output.reserve(1024 + measured.patches.size() * 48);
    if (!append_row(output, {"record", "result", "chart_hash", "source_sha256",
        "output_profile_sha256", "print_route", "instrument", "illuminant",
        "observer", "measurement_mode", "backing", "measured_at", "matched",
        "missing", "extra", "mean_delta_e00", "p95_delta_e00",
        "maximum_delta_e00", "mean_limit_delta_e00", "p95_limit_delta_e00",
        "maximum_limit_delta_e00", "reference_chart_hash",
        "reference_source_sha256", "reference_output_profile_sha256",
        "reference_print_route", "reference_instrument", "reference_illuminant",
        "reference_observer", "reference_measurement_mode", "reference_backing",
        "reference_measured_at", "diagnostic", "sample_id", "patch_delta_e00",
        "patch_status"}))
        return fail("Measurement comparison report exceeds its output limit.");
    const auto& condition = measured.condition;
    if (!append_row(output, {"summary", status(comparison), condition.chart_hash,
        condition.source_sha256, condition.output_profile_sha256,
        condition.print_route, condition.instrument, condition.illuminant,
        condition.observer, condition.measurement_mode, condition.backing,
        condition.measured_at, std::to_string(comparison.matched),
        std::to_string(comparison.missing), std::to_string(comparison.extra),
        comparison.matched ? number(comparison.mean_delta_e) : "",
        comparison.matched ? number(comparison.p95_delta_e) : "",
        comparison.matched ? number(comparison.maximum_delta_e) : "",
        tolerance ? number(tolerance->mean_max) : "",
        tolerance ? number(tolerance->p95_max) : "",
        tolerance ? number(tolerance->maximum_max) : "",
        reference.condition.chart_hash, reference.condition.source_sha256,
        reference.condition.output_profile_sha256, reference.condition.print_route,
        reference.condition.instrument, reference.condition.illuminant,
        reference.condition.observer, reference.condition.measurement_mode,
        reference.condition.backing, reference.condition.measured_at,
        comparison.diagnostic, "", "", ""}))
        return fail("Measurement comparison report exceeds its output limit.");

    for (const auto& patch : measured.patches) {
        const auto found = deltas.find(patch.sample_id);
        const bool in_reference = reference_ids.contains(patch.sample_id);
        if (found != deltas.end()) {
            if (!append_patch_row(output, patch.sample_id, number(found->second),
                                  "MATCHED"))
                return fail("Measurement comparison report exceeds its output limit.");
        } else if (!in_reference) {
            if (!append_patch_row(output, patch.sample_id, "", "EXTRA"))
                return fail("Measurement comparison report exceeds its output limit.");
        } else if (comparison.patch_delta_e.empty()) {
            if (!append_patch_row(output, patch.sample_id, "",
                                  "UNASSESSED_CONDITION"))
                return fail("Measurement comparison report exceeds its output limit.");
        } else {
            return fail("Measurement comparison report is missing a matched patch score.");
        }
    }
    for (const auto& patch : reference.patches) {
        if (!measured_ids.contains(patch.sample_id) &&
            !append_patch_row(output, patch.sample_id, "", "MISSING"))
            return fail("Measurement comparison report exceeds its output limit.");
    }
    if (error) error->clear();
    return output;
}

bool save_measurement_comparison_csv(
    const std::filesystem::path& path, const MeasurementRun& measured,
    const MeasurementRun& reference, const MeasurementComparison& comparison,
    std::optional<DeltaETolerance> tolerance, std::string* error) {
    auto report = serialize_measurement_comparison_csv(measured, reference,
                                                       comparison, tolerance, error);
    return report && write_atomic(path, *report, error);
}

}  // namespace pikdraw::production::measurement
