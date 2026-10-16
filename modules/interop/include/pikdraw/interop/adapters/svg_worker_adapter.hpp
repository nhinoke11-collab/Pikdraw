#pragma once

#include "pikdraw/interop/adapters/svg_importer.hpp"
#include "pikdraw/interop/ipc_dto.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace pikdraw::interop {

enum class ImportRecordType : uint16_t {
    Page = 1,
    Path = 2,
    Text = 3,
    FidelityReport = 4,
    FidelityIssue = 5,
    Layer = 6,
    Group = 7
};

enum class ImportColorModel : uint8_t { Rgb = 1, Cmyk = 2, Spot = 3, Gray = 4 };
enum class ImportPathVerb : uint8_t { Move = 1, Line = 2, Quad = 3, Cubic = 4, Arc = 5, Close = 6 };
enum class ImportFidelitySeverity : uint8_t { Info = 1, Warning = 2, Unsupported = 3 };
enum class ImportParseOutcome : uint8_t { Complete = 1, CompleteWithDiagnostics = 2 };
enum class ImportCoverage : uint8_t { Partial = 1, Unknown = 2 };
enum class ImportAppearance : uint8_t { Unknown = 1, Partial = 2 };
enum class ImportEditability : uint8_t { NativeEditable = 1, ExplicitConversion = 2 };
enum class ImportProduction : uint8_t { NotAssessed = 1, Blocked = 2 };

struct ImportColorRecord {
    ImportColorModel model{ImportColorModel::Rgb};
    std::array<double, 5> components{};
    std::string spot_name;
    std::array<double, 4> alternate_rgb{};
    uint8_t overprint_flags{0};
};

struct ImportPathElementRecord {
    ImportPathVerb verb{ImportPathVerb::Move};
    uint8_t flags{0};
    std::array<double, 9> values{}; // endpoint, controls, then arc radii and rotation; all model mm.
};

struct ImportPageRecord { double width_mm{0}; double height_mm{0}; };

struct ImportLayerRecord {
    uint32_t local_id{0};
    std::string source_id;
    std::string name;
    bool visible{true};
    bool locked{false};
    std::vector<uint32_t> object_local_ids;
};

struct ImportGroupRecord {
    uint32_t local_id{0};
    std::string source_id;
    std::string name;
    std::vector<uint32_t> child_local_ids;
};

struct ImportPathRecord {
    uint32_t local_id{0};
    std::string source_id;
    std::string name;
    uint8_t winding_rule{0};
    bool has_fill{true};
    bool has_stroke{false};
    double stroke_width_mm{0};
    ImportColorRecord fill;
    ImportColorRecord stroke;
    std::vector<ImportPathElementRecord> elements;
};

struct ImportTextRecord {
    uint32_t local_id{0};
    std::string source_id;
    std::string name;
    std::string utf8_text;
    std::string font_family;
    uint16_t font_weight{400};
    bool italic{false};
    double font_size_pt{12};
    double x_mm{0};
    double y_mm{0};
    ImportColorRecord fill;
};

struct ImportFidelityReportRecord {
    std::string coverage_profile;
    ImportParseOutcome parse_outcome{ImportParseOutcome::Complete};
    ImportCoverage coverage{ImportCoverage::Partial};
    ImportAppearance appearance{ImportAppearance::Unknown};
    ImportEditability editability{ImportEditability::NativeEditable};
    ImportProduction production{ImportProduction::NotAssessed};
    uint32_t issue_count{0};
};

struct ImportFidelityIssueRecord {
    ImportFidelitySeverity severity{ImportFidelitySeverity::Warning};
    std::string source_locator;
    std::string feature_code;
    std::string diagnostic_code;
};

using ImportRecordValue = std::variant<
    ImportPageRecord,
    ImportLayerRecord,
    ImportGroupRecord,
    ImportPathRecord,
    ImportTextRecord,
    ImportFidelityReportRecord,
    ImportFidelityIssueRecord>;

struct CanonicalImportRecord {
    ImportRecordType type{ImportRecordType::Page};
    std::vector<uint8_t> payload;
};

[[nodiscard]] std::optional<CanonicalImportRecord> SerializeImportRecord(const ImportRecordValue& value);
[[nodiscard]] std::optional<ImportRecordValue> DeserializeImportRecord(
    ImportRecordType type, std::span<const uint8_t> payload);

/// Runs the bounded SVG subset importer and maps only its supported source data into C6 records.
[[nodiscard]] bool BuildSvgImportRecords(
    std::span<const uint8_t> source,
    std::vector<CanonicalImportRecord>& records,
    std::string& diagnostic);

} // namespace pikdraw::interop
