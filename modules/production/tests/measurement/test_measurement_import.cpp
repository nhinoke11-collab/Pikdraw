#include "pikdraw/production/measurement/measurement_import.hpp"
#include "pikdraw/production/measurement/measurement_report.hpp"
#include "pikdraw/production/measurement/measurement_run_store.hpp"

#include <cassert>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace pikdraw::production::measurement;

namespace {
const std::string kChartHash = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
const std::string kSourceHash = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const std::string kOutputHash = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

std::string cgats(std::string_view rows) {
    return "CGATS.17\n"
        "ORIGINATOR \"PikDraw measurement import\"\n"
        "CHART_HASH " + kChartHash + "\n"
        "SOURCE_SHA256 " + kSourceHash + "\n"
        "OUTPUT_PROFILE_SHA256 " + kOutputHash + "\n"
        "PRINT_ROUTE DeviceRGB\n"
        "INSTRUMENT \"fixture-spectro\"\n"
        "ILLUMINANT D50\n"
        "OBSERVER 2\n"
        "M_MODE M1\n"
        "BACKING white\n"
        "MEASURED_AT 2026-10-24T12:00:00Z\n"
        "NUMBER_OF_FIELDS 4\n"
        "BEGIN_DATA_FORMAT\n"
        "SAMPLE_ID LAB_L LAB_A LAB_B\n"
        "END_DATA_FORMAT\n"
        "NUMBER_OF_SETS 2\n"
        "BEGIN_DATA\n" + std::string(rows) +
        "END_DATA\n";
}
}

int main() {
    assert(std::abs(delta_e_2000({50.0, 2.6772, -79.7751},
                                 {50.0, 0.0, -82.7485}) - 2.0425) < 0.0001);
    assert(std::abs(delta_e_2000({50.0, 0.0, 0.0},
                                 {50.0, 0.0, 0.0})) < 1e-12);

    const auto parsed = parse_measurement_run(cgats("P-01 50.0 0.0 0.0\nP-02 60.0 1.0 -2.0\n"));
    assert(parsed && parsed.run->patches.size() == 2);
    assert(parsed.run->condition.chart_hash == kChartHash);
    assert(parsed.run->condition.measurement_mode == "M1");

    const auto reference = std::vector<ReferencePatch>{
        {"P-01", {50.0, 0.0, 0.0}}, {"P-02", {60.0, 0.0, -2.0}}
    };
    const auto comparison = compare_measurement_run(
        *parsed.run, parsed.run->condition, reference);
    assert(comparison.complete && comparison.matched == 2 &&
           comparison.missing == 0 && comparison.extra == 0);
    assert(comparison.maximum_delta_e > 0.0 && comparison.p95_delta_e >= comparison.mean_delta_e);
    assert(!comparison.assessed && !comparison.passed);
    assert(comparison.diagnostic.find("no pass/fail claim") != std::string::npos);
    const auto tolerated = compare_measurement_run(*parsed.run,
        parsed.run->condition, reference, DeltaETolerance{3.0, 3.0, 3.0});
    assert(tolerated.complete && tolerated.assessed && tolerated.passed);
    const auto rejected = compare_measurement_run(*parsed.run,
        parsed.run->condition, reference, DeltaETolerance{0.01, 0.01, 0.01});
    assert(rejected.complete && rejected.assessed && !rejected.passed);
    const auto invalid_tolerance = compare_measurement_run(*parsed.run,
        parsed.run->condition, reference, DeltaETolerance{-1.0, 2.0, 2.0});
    assert(!invalid_tolerance.assessed && !invalid_tolerance.complete);
    auto formula_run = *parsed.run;
    formula_run.patches.front().sample_id = "=2+3";
    auto formula_reference = formula_run;
    const auto formula_comparison = compare_measurement_run(formula_run,
        formula_reference.condition,
        std::vector<ReferencePatch>{{"=2+3", formula_run.patches.front().lab},
                                    {"P-02", formula_run.patches.back().lab}},
        DeltaETolerance{3.0, 3.0, 3.0});
    std::string report_error;
    const auto csv_report = serialize_measurement_comparison_csv(formula_run,
        formula_reference, formula_comparison, DeltaETolerance{3.0, 3.0, 3.0},
        &report_error);
    assert(csv_report && report_error.empty());
    assert(csv_report->find("\"PASS\"") != std::string::npos);
    assert(csv_report->find("\"'=2+3\"") != std::string::npos);
    assert(csv_report->find("mean_limit_delta_e00") != std::string::npos);
    const auto first_line_end = csv_report->find("\r\n");
    const auto second_line_end = csv_report->find("\r\n", first_line_end + 2);
    const auto third_line_end = csv_report->find("\r\n", second_line_end + 2);
    assert(first_line_end != std::string::npos && second_line_end != std::string::npos &&
           third_line_end != std::string::npos);
    assert(std::count(csv_report->begin(), csv_report->begin() + first_line_end, ',') ==
           std::count(csv_report->begin() + first_line_end + 2,
                      csv_report->begin() + second_line_end, ','));
    assert(std::count(csv_report->begin() + first_line_end + 2,
                      csv_report->begin() + second_line_end, ',') ==
           std::count(csv_report->begin() + second_line_end + 2,
                      csv_report->begin() + third_line_end, ','));
    const auto report_path = std::filesystem::temp_directory_path() /
        ("pikdraw-measurement-report-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".csv");
    struct RemoveReport { std::filesystem::path path; ~RemoveReport() {
        std::error_code ec; std::filesystem::remove(path, ec);
    }} cleanup_report{report_path};
    assert(save_measurement_comparison_csv(report_path, formula_run,
        formula_reference, formula_comparison, DeltaETolerance{3.0, 3.0, 3.0},
        &report_error));
    std::ifstream saved_report(report_path, std::ios::binary);
    const std::string saved_csv((std::istreambuf_iterator<char>(saved_report)),
                                std::istreambuf_iterator<char>());
    assert(saved_csv == *csv_report);

    const auto incomplete = compare_measurement_run(*parsed.run,
        parsed.run->condition,
        std::vector<ReferencePatch>{{"P-01", {50.0, 0.0, 0.0}},
                                    {"P-02", {60.0, 0.0, -2.0}},
                                    {"P-03", {70.0, 0.0, 0.0}}});
    assert(!incomplete.complete && incomplete.missing == 1 && incomplete.matched == 2);
    auto different_route = parsed.run->condition;
    different_route.print_route = "RIP-CMYK";
    const auto mismatched_route = compare_measurement_run(
        *parsed.run, different_route, reference);
    assert(!mismatched_route.complete && mismatched_route.matched == 0);
    const auto mismatch_report = serialize_measurement_comparison_csv(
        *parsed.run, *parsed.run, mismatched_route, std::nullopt, &report_error);
    assert(mismatch_report && mismatch_report->find("UNASSESSED_CONDITION") !=
           std::string::npos);

    const auto duplicate = parse_measurement_run(cgats("P-01 50 0 0\nP-01 51 0 0\n"));
    assert(!duplicate && duplicate.error == MeasurementImportError::DuplicatePatch);
    const auto malformed = parse_measurement_run(cgats("P-01 NaN 0 0\nP-02 60 0 0\n"));
    assert(!malformed && malformed.error == MeasurementImportError::InvalidValue);
    auto wrong_field_count = cgats("P-01 50 0 0\nP-02 60 0 0\n");
    const auto field_count = wrong_field_count.find("NUMBER_OF_FIELDS 4");
    assert(field_count != std::string::npos);
    wrong_field_count.replace(field_count, 18, "NUMBER_OF_FIELDS 3");
    const auto malformed_schema = parse_measurement_run(wrong_field_count);
    assert(!malformed_schema &&
           malformed_schema.error == MeasurementImportError::MalformedDocument);

    const auto csv = parse_measurement_run(
        "SAMPLE_ID,LAB_L,LAB_A,LAB_B,CHART_HASH,SOURCE_SHA256,OUTPUT_PROFILE_SHA256,PRINT_ROUTE,INSTRUMENT,ILLUMINANT,OBSERVER,MEASUREMENT_MODE,BACKING,MEASURED_AT\n"
        "P-01,50,0,0," + kChartHash + "," + kSourceHash + "," + kOutputHash + ",DeviceRGB,fixture-spectro,D50,2,M1,white,2026-10-24T12:00:00Z\n");
    assert(csv && csv.run->patches.size() == 1);
    auto store_run = *csv.run;
    store_run.patches.front().xyz = std::array<double, 3>{19.01, 20.02, 21.03};
    std::string store_error;
    const auto encoded = serialize_measurement_run(store_run, &store_error);
    assert(!encoded.empty() && store_error.empty());
    const auto decoded = deserialize_measurement_run(encoded);
    assert(decoded && decoded.run->condition.chart_hash == kChartHash);
    assert(decoded.run->patches.size() == 1 && decoded.run->patches.front().xyz);
    assert((*decoded.run->patches.front().xyz)[1] == 20.02);
    auto corrupt = encoded;
    corrupt[corrupt.size() / 2] ^= 0x20;
    assert(!deserialize_measurement_run(corrupt));
    const auto temp_path = std::filesystem::temp_directory_path() /
        ("pikdraw-measurement-run-store-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".pdmr");
    struct RemoveRunFile { std::filesystem::path path; ~RemoveRunFile() {
        std::error_code ec; std::filesystem::remove(path, ec);
    }} cleanup{temp_path};
    assert(save_measurement_run(temp_path, store_run, &store_error));
    const auto loaded = load_measurement_run(temp_path);
    assert(loaded && loaded.run->patches.front().sample_id == "P-01");
    const auto inconsistent_condition = parse_measurement_run(
        "SAMPLE_ID,LAB_L,LAB_A,LAB_B,CHART_HASH,SOURCE_SHA256,OUTPUT_PROFILE_SHA256,PRINT_ROUTE,INSTRUMENT,ILLUMINANT,OBSERVER,MEASUREMENT_MODE,BACKING,MEASURED_AT\n"
        "P-01,50,0,0," + kChartHash + "," + kSourceHash + "," + kOutputHash + ",DeviceRGB,fixture-spectro,D50,2,M1,white,2026-10-24T12:00:00Z\n"
        "P-02,50,0,0," + kChartHash + "," + kSourceHash + "," + kOutputHash + ",DeviceRGB,another-instrument,D50,2,M1,white,2026-10-24T12:00:00Z\n");
    assert(!inconsistent_condition &&
           inconsistent_condition.error == MeasurementImportError::InvalidMetadata);

    std::cout << "P7 bounded CGATS/CSV and CIEDE2000 checks passed\n";
}
