#include "pikdraw/production/measurement/measurement_import.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace pikdraw::production::measurement {
namespace {
constexpr size_t kMaxInputBytes = 64u * 1024u * 1024u;
constexpr size_t kMaxPatches = 250000;
constexpr size_t kMaxLineBytes = 4096;
constexpr size_t kMaxFields = 256;

MeasurementImportResult failure(MeasurementImportError error, std::string message) {
    return {std::nullopt, error, std::move(message)};
}

std::string uppercase(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return result;
}

std::string trim(std::string_view value) {
    size_t begin = 0;
    size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return std::string(value.substr(begin, end - begin));
}

bool valid_text(std::string_view value, size_t maximum = 256) {
    if (value.empty() || value.size() > maximum) return false;
    for (const unsigned char ch : value)
        if (ch == 0 || ch < 0x20 || ch == 0x7f) return false;
    return true;
}

bool valid_hash(std::string_view value) {
    if (value.size() != 64) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isxdigit(ch) != 0;
    });
}

std::optional<double> number(std::string_view value) {
    if (value.empty() || value.size() > 64) return std::nullopt;
    double result = 0.0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result,
                                        std::chars_format::general);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
        !std::isfinite(result)) return std::nullopt;
    return result;
}

std::vector<std::string> whitespace_tokens(std::string_view line, bool& valid) {
    std::vector<std::string> result;
    valid = true;
    size_t index = 0;
    while (index < line.size()) {
        while (index < line.size() && std::isspace(static_cast<unsigned char>(line[index]))) ++index;
        if (index == line.size()) break;
        std::string token;
        if (line[index] == '"') {
            ++index;
            bool closed = false;
            while (index < line.size()) {
                const char ch = line[index++];
                if (ch == '"') { closed = true; break; }
                if (ch == '\\' && index < line.size() && line[index] == '"') {
                    token.push_back('"'); ++index;
                } else token.push_back(ch);
            }
            if (!closed || (index < line.size() &&
                !std::isspace(static_cast<unsigned char>(line[index])))) {
                valid = false; return {};
            }
        } else {
            const size_t start = index;
            while (index < line.size() && !std::isspace(static_cast<unsigned char>(line[index]))) ++index;
            token.assign(line.substr(start, index - start));
        }
        result.push_back(std::move(token));
        if (result.size() > kMaxFields) { valid = false; return {}; }
    }
    return result;
}

std::optional<std::vector<std::string>> csv_tokens(std::string_view line) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (quoted) {
            if (ch == '"' && i + 1 < line.size() && line[i + 1] == '"') {
                field.push_back('"'); ++i;
            } else if (ch == '"') quoted = false;
            else field.push_back(ch);
        } else if (ch == ',' && !quoted) {
            fields.push_back(trim(field)); field.clear();
        } else if (ch == '"' && field.empty()) quoted = true;
        else if (ch == '"') return std::nullopt;
        else field.push_back(ch);
    }
    if (quoted) return std::nullopt;
    fields.push_back(trim(field));
    if (fields.size() > kMaxFields) return std::nullopt;
    return fields;
}

std::optional<size_t> field_index(const std::vector<std::string>& fields,
                                 std::initializer_list<std::string_view> choices) {
    for (const auto choice : choices) {
        const auto wanted = uppercase(choice);
        const auto found = std::find(fields.begin(), fields.end(), wanted);
        if (found != fields.end()) return static_cast<size_t>(found - fields.begin());
    }
    return std::nullopt;
}

MeasurementImportResult make_run(
    const std::vector<std::string>& fields,
    const std::vector<std::vector<std::string>>& rows,
    const std::unordered_map<std::string, std::string>& metadata,
    std::optional<size_t> declared_count) {
    const auto id_index = field_index(fields, {"SAMPLE_ID"});
    const auto l_index = field_index(fields, {"LAB_L"});
    const auto a_index = field_index(fields, {"LAB_A"});
    const auto b_index = field_index(fields, {"LAB_B"});
    const auto chart_index = field_index(fields, {"CHART_HASH"});
    const auto source_hash_index = field_index(fields, {"SOURCE_SHA256", "SOURCE_HASH"});
    const auto output_hash_index = field_index(fields,
        {"OUTPUT_PROFILE_SHA256", "OUTPUT_PROFILE_HASH"});
    const auto route_index = field_index(fields, {"PRINT_ROUTE", "ROUTE"});
    const auto instrument_index = field_index(fields, {"INSTRUMENT"});
    const auto illuminant_index = field_index(fields, {"ILLUMINANT"});
    const auto observer_index = field_index(fields, {"OBSERVER"});
    const auto mode_index = field_index(fields, {"MEASUREMENT_MODE", "M_MODE", "MEASUREMENT_CONDITION"});
    const auto backing_index = field_index(fields, {"BACKING"});
    const auto measured_at_index = field_index(fields, {"MEASURED_AT", "MEASUREMENT_DATE"});
    const auto x_index = field_index(fields, {"XYZ_X"});
    const auto y_index = field_index(fields, {"XYZ_Y"});
    const auto z_index = field_index(fields, {"XYZ_Z"});
    if (!id_index || !l_index || !a_index || !b_index)
        return failure(MeasurementImportError::MissingFields,
            "Measurement data requires SAMPLE_ID and LAB_L/LAB_A/LAB_B fields.");
    if (rows.empty() || (declared_count && *declared_count != rows.size()))
        return failure(MeasurementImportError::MalformedDocument,
            "Measured row count is empty or differs from NUMBER_OF_SETS.");
    if (rows.size() > kMaxPatches)
        return failure(MeasurementImportError::TooManyPatches,
            "Measurement file exceeds 250,000 patches.");

    auto metadata_value = [&](std::optional<size_t> index, std::string_view key) {
        if (index && !rows.empty() && *index < rows.front().size())
            return rows.front()[*index];
        const auto found = metadata.find(uppercase(key));
        return found == metadata.end() ? std::string{} : found->second;
    };
    MeasurementRun run;
    run.condition.chart_hash = metadata_value(chart_index, "CHART_HASH");
    run.condition.source_sha256 = metadata_value(source_hash_index, "SOURCE_SHA256");
    if (run.condition.source_sha256.empty())
        run.condition.source_sha256 = metadata_value(source_hash_index, "SOURCE_HASH");
    run.condition.output_profile_sha256 = metadata_value(
        output_hash_index, "OUTPUT_PROFILE_SHA256");
    if (run.condition.output_profile_sha256.empty())
        run.condition.output_profile_sha256 = metadata_value(
            output_hash_index, "OUTPUT_PROFILE_HASH");
    run.condition.print_route = metadata_value(route_index, "PRINT_ROUTE");
    if (run.condition.print_route.empty())
        run.condition.print_route = metadata_value(route_index, "ROUTE");
    run.condition.instrument = metadata_value(instrument_index, "INSTRUMENT");
    run.condition.illuminant = metadata_value(illuminant_index, "ILLUMINANT");
    run.condition.observer = metadata_value(observer_index, "OBSERVER");
    run.condition.measurement_mode = metadata_value(mode_index, "MEASUREMENT_MODE");
    if (run.condition.measurement_mode.empty())
        run.condition.measurement_mode = metadata_value(mode_index, "M_MODE");
    if (run.condition.measurement_mode.empty())
        run.condition.measurement_mode = metadata_value(mode_index, "MEASUREMENT_CONDITION");
    run.condition.backing = metadata_value(backing_index, "BACKING");
    run.condition.measured_at = metadata_value(measured_at_index, "MEASURED_AT");
    if (!valid_hash(run.condition.chart_hash) ||
        !valid_hash(run.condition.source_sha256) ||
        !valid_hash(run.condition.output_profile_sha256) ||
        !valid_text(run.condition.print_route, 64) ||
        !valid_text(run.condition.instrument) ||
        !valid_text(run.condition.illuminant, 32) || !valid_text(run.condition.observer, 16) ||
        !valid_text(run.condition.measurement_mode, 32) ||
        !valid_text(run.condition.backing, 64) || !valid_text(run.condition.measured_at, 64))
        return failure(MeasurementImportError::InvalidMetadata,
            "Chart/source/output-profile SHA-256, print route, instrument, illuminant, observer, measurement mode, backing, and timestamp are required.");

    const std::optional<size_t> row_metadata[] = {
        chart_index, source_hash_index, output_hash_index, route_index,
        instrument_index, illuminant_index, observer_index, mode_index,
        backing_index, measured_at_index};
    for (const auto& row : rows) {
        if (row.size() != fields.size())
            return failure(MeasurementImportError::MalformedDocument,
                "A measurement row has a different number of fields from its header.");
        for (const auto index : row_metadata) {
            if (!index) continue;
            if (row[*index] != rows.front()[*index])
                return failure(MeasurementImportError::InvalidMetadata,
                    "Measurement condition fields must be identical across all patch rows.");
        }
    }

    std::unordered_set<std::string> ids;
    run.patches.reserve(rows.size());
    for (const auto& row : rows) {
        const std::string id = row[*id_index];
        const auto l = number(row[*l_index]);
        const auto a = number(row[*a_index]);
        const auto b = number(row[*b_index]);
        if (!valid_text(id) || !l || !a || !b || *l < 0.0 || *l > 100.0 ||
            *a < -200.0 || *a > 200.0 || *b < -200.0 || *b > 200.0)
            return failure(MeasurementImportError::InvalidValue,
                "A patch ID or Lab value is invalid or outside CIELAB storage bounds.");
        if (!ids.insert(id).second)
            return failure(MeasurementImportError::DuplicatePatch,
                "Measurement rows contain a duplicate SAMPLE_ID.");
        MeasuredPatch patch{id, {*l, *a, *b}, std::nullopt};
        if (x_index && y_index && z_index) {
            const auto x = number(row[*x_index]);
            const auto y = number(row[*y_index]);
            const auto z = number(row[*z_index]);
            if (!x || !y || !z || *x < 0.0 || *y < 0.0 || *z < 0.0 ||
                *x > 10000.0 || *y > 10000.0 || *z > 10000.0)
                return failure(MeasurementImportError::InvalidValue,
                    "A supplementary XYZ value is invalid or outside supported bounds.");
            patch.xyz = std::array<double, 3>{*x, *y, *z};
        }
        run.patches.push_back(std::move(patch));
    }
    return {std::move(run), MeasurementImportError::None, {}};
}

MeasurementImportResult parse_csv(std::string_view input) {
    std::vector<std::string> fields;
    std::vector<std::vector<std::string>> rows;
    size_t offset = 0;
    bool first_line = true;
    while (offset < input.size()) {
        const size_t end = input.find('\n', offset);
        const size_t length = (end == std::string_view::npos ? input.size() : end) - offset;
        if (length > kMaxLineBytes)
            return failure(MeasurementImportError::MalformedDocument,
                "CSV line exceeds the 4 KiB parser limit.");
        auto line = input.substr(offset, length);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        offset = end == std::string_view::npos ? input.size() : end + 1;
        if (trim(line).empty()) continue;
        const auto parsed = csv_tokens(line);
        if (!parsed) return failure(MeasurementImportError::MalformedDocument,
                                    "CSV contains malformed quoting or too many columns.");
        if (first_line) {
            fields = *parsed;
            for (auto& field : fields) field = uppercase(trim(field));
            first_line = false;
        } else rows.push_back(*parsed);
        if (rows.size() > kMaxPatches)
            return failure(MeasurementImportError::TooManyPatches,
                "Measurement file exceeds 250,000 patches.");
    }
    return make_run(fields, rows, {}, std::nullopt);
}

MeasurementImportResult parse_cgats(std::string_view input) {
    std::vector<std::string> fields;
    std::vector<std::vector<std::string>> rows;
    std::unordered_map<std::string, std::string> metadata;
    std::optional<size_t> declared_count;
    std::optional<size_t> declared_field_count;
    bool saw_cgats = false;
    bool in_format = false;
    bool in_data = false;
    size_t offset = 0;
    while (offset < input.size()) {
        const size_t end = input.find('\n', offset);
        const size_t length = (end == std::string_view::npos ? input.size() : end) - offset;
        if (length > kMaxLineBytes)
            return failure(MeasurementImportError::MalformedDocument,
                "CGATS line exceeds the 4 KiB parser limit.");
        auto line = input.substr(offset, length);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        offset = end == std::string_view::npos ? input.size() : end + 1;
        line = std::string_view(line.data(), line.size());
        const auto clean = trim(line);
        if (clean.empty() || clean.front() == '#') continue;
        bool valid = false;
        auto tokens = whitespace_tokens(clean, valid);
        if (!valid) return failure(MeasurementImportError::MalformedDocument,
                                   "CGATS line has malformed quoting or too many fields.");
        if (tokens.empty()) continue;
        const auto keyword = uppercase(tokens[0]);
        if (!saw_cgats) {
            if (keyword != "CGATS.17" && keyword != "CGATS.16" && keyword != "CGATS.5")
                return failure(MeasurementImportError::MalformedDocument,
                    "Input is neither a supported CGATS table nor the documented CSV schema.");
            saw_cgats = true;
            continue;
        }
        if (keyword == "BEGIN_DATA_FORMAT") { in_format = true; in_data = false; continue; }
        if (keyword == "END_DATA_FORMAT") { in_format = false; continue; }
        if (keyword == "BEGIN_DATA") { in_data = true; in_format = false; continue; }
        if (keyword == "END_DATA") { in_data = false; continue; }
        if (in_format) {
            for (auto& token : tokens) fields.push_back(uppercase(token));
            if (fields.size() > kMaxFields)
                return failure(MeasurementImportError::MalformedDocument,
                    "CGATS declares more than 256 fields.");
            continue;
        }
        if (in_data) {
            rows.push_back(std::move(tokens));
            if (rows.size() > kMaxPatches)
                return failure(MeasurementImportError::TooManyPatches,
                    "CGATS table exceeds 250,000 patches.");
            continue;
        }
        if ((keyword == "NUMBER_OF_SETS" || keyword == "NUMBER_OF_FIELDS") &&
            tokens.size() == 2) {
            if ((keyword == "NUMBER_OF_SETS" && declared_count) ||
                (keyword == "NUMBER_OF_FIELDS" && declared_field_count))
                return failure(MeasurementImportError::MalformedDocument,
                    "CGATS contains a repeated row or field count declaration.");
            size_t value = 0;
            const auto parsed = std::from_chars(tokens[1].data(),
                tokens[1].data() + tokens[1].size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != tokens[1].data() + tokens[1].size())
                return failure(MeasurementImportError::MalformedDocument,
                    "NUMBER_OF_SETS is not a valid integer.");
            if (keyword == "NUMBER_OF_SETS") declared_count = value;
            else declared_field_count = value;
        } else if (tokens.size() >= 2) {
            if (metadata.contains(keyword))
                return failure(MeasurementImportError::MalformedDocument,
                    "CGATS contains a repeated metadata field.");
            std::string value;
            for (size_t i = 1; i < tokens.size(); ++i) {
                if (!value.empty()) value.push_back(' ');
                value += tokens[i];
            }
            metadata[keyword] = std::move(value);
        }
    }
    if (!saw_cgats || in_format || in_data || fields.empty())
        return failure(MeasurementImportError::MalformedDocument,
            "CGATS table is incomplete or is missing BEGIN/END blocks.");
    if (declared_field_count && *declared_field_count != fields.size())
        return failure(MeasurementImportError::MalformedDocument,
            "CGATS NUMBER_OF_FIELDS differs from BEGIN_DATA_FORMAT.");
    return make_run(fields, rows, metadata, declared_count);
}

} // namespace

MeasurementImportResult parse_measurement_run(std::string_view input) {
    if (input.empty()) return failure(MeasurementImportError::EmptyInput,
                                      "Measurement input is empty.");
    if (input.size() > kMaxInputBytes)
        return failure(MeasurementImportError::InputTooLarge,
                      "Measurement input exceeds the 64 MiB limit.");
    if (input.size() >= 3 && static_cast<uint8_t>(input[0]) == 0xef &&
        static_cast<uint8_t>(input[1]) == 0xbb && static_cast<uint8_t>(input[2]) == 0xbf)
        input.remove_prefix(3);
    const auto first = input.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return failure(MeasurementImportError::EmptyInput, "Measurement input has no rows.");
    const auto line_end = input.find_first_of("\r\n", first);
    const auto first_line = input.substr(first, line_end == std::string_view::npos
        ? input.size() - first : line_end - first);
    const bool csv = first_line.find(',') != std::string_view::npos;
    try { return csv ? parse_csv(input) : parse_cgats(input); }
    catch (const std::bad_alloc&) {
        return failure(MeasurementImportError::InputTooLarge,
                      "Insufficient memory to parse the bounded measurement file.");
    } catch (...) {
        return failure(MeasurementImportError::MalformedDocument,
                      "Unexpected parser failure; no partial measurement run was published.");
    }
}

MeasurementComparison compare_measurement_run(
    const MeasurementRun& measured, const MeasurementCondition& expected,
    const std::vector<ReferencePatch>& reference,
    std::optional<DeltaETolerance> tolerance) {
    MeasurementComparison result;
    if (tolerance && (!std::isfinite(tolerance->mean_max) ||
        !std::isfinite(tolerance->p95_max) || !std::isfinite(tolerance->maximum_max) ||
        tolerance->mean_max < 0.0 || tolerance->p95_max < 0.0 ||
        tolerance->maximum_max < 0.0)) {
        result.diagnostic = "Delta E tolerance contains a negative or non-finite limit.";
        return result;
    }
    const auto& condition = measured.condition;
    if (!valid_hash(expected.chart_hash) || !valid_hash(expected.source_sha256) ||
        !valid_hash(expected.output_profile_sha256) ||
        !valid_text(expected.print_route, 64) ||
        uppercase(condition.chart_hash) != uppercase(expected.chart_hash) ||
        uppercase(condition.source_sha256) != uppercase(expected.source_sha256) ||
        uppercase(condition.output_profile_sha256) !=
            uppercase(expected.output_profile_sha256) ||
        condition.print_route != expected.print_route ||
        condition.instrument != expected.instrument ||
        condition.illuminant != expected.illuminant ||
        condition.observer != expected.observer ||
        condition.measurement_mode != expected.measurement_mode ||
        condition.backing != expected.backing ||
        !valid_text(measured.condition.instrument) ||
        !valid_text(measured.condition.illuminant, 32) ||
        !valid_text(measured.condition.observer, 16) ||
        !valid_text(measured.condition.measurement_mode, 32) ||
        !valid_text(measured.condition.backing, 64) ||
        !valid_text(measured.condition.measured_at, 64) ||
        reference.empty() || reference.size() > kMaxPatches) {
        result.diagnostic = "Chart identity or measurement condition is incomplete or mismatched; results are not assessed.";
        return result;
    }
    std::unordered_map<std::string, LabValue> targets;
    targets.reserve(reference.size());
    for (const auto& patch : reference) {
        if (!valid_text(patch.sample_id) || !std::isfinite(patch.lab.l) ||
            !std::isfinite(patch.lab.a) || !std::isfinite(patch.lab.b) ||
            patch.lab.l < 0.0 || patch.lab.l > 100.0 ||
            !targets.emplace(patch.sample_id, patch.lab).second) {
            result.diagnostic = "Reference patch map is invalid or contains duplicate IDs.";
            return result;
        }
    }
    std::unordered_set<std::string> observed;
    std::vector<double> values;
    for (const auto& patch : measured.patches) {
        if (!observed.insert(patch.sample_id).second) {
            result.diagnostic = "Measurement run contains duplicate patch IDs.";
            return result;
        }
        const auto target = targets.find(patch.sample_id);
        if (target == targets.end()) { ++result.extra; continue; }
        const double delta = delta_e_2000(patch.lab, target->second);
        if (!std::isfinite(delta)) {
            result.diagnostic = "A Lab pair produced an invalid CIEDE2000 value.";
            return result;
        }
        ++result.matched;
        values.push_back(delta);
        result.patch_delta_e.emplace_back(patch.sample_id, delta);
    }
    result.missing = targets.size() - result.matched;
    result.complete = result.missing == 0 && result.extra == 0 &&
                      result.matched == targets.size();
    if (!values.empty()) {
        std::sort(values.begin(), values.end());
        double sum = 0.0;
        for (const auto value : values) sum += value;
        result.mean_delta_e = sum / values.size();
        const size_t p95_index = static_cast<size_t>(std::ceil(values.size() * 0.95)) - 1;
        result.p95_delta_e = values[(std::min)(p95_index, values.size() - 1)];
        result.maximum_delta_e = values.back();
    }
    std::sort(result.patch_delta_e.begin(), result.patch_delta_e.end(),
        [](const auto& left, const auto& right) { return left.first < right.first; });
    if (!result.complete) {
        result.diagnostic = "Measurement coverage is incomplete; no pass/fail claim is made.";
    } else if (!tolerance) {
        result.diagnostic = "Complete measurement coverage; no tolerance was supplied, so no pass/fail claim is made.";
    } else {
        result.assessed = true;
        result.passed = result.mean_delta_e <= tolerance->mean_max &&
                        result.p95_delta_e <= tolerance->p95_max &&
                        result.maximum_delta_e <= tolerance->maximum_max;
        result.diagnostic = result.passed
            ? "Complete measurement run satisfies the supplied mean, p95, and maximum Delta E limits."
            : "Complete measurement run exceeds at least one supplied Delta E limit.";
    }
    return result;
}

} // namespace pikdraw::production::measurement
