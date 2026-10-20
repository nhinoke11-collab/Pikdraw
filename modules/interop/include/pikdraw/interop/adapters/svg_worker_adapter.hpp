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
    Group = 7,
    RasterImage = 8,
    RasterImageChunk = 9,
    RasterSourceChunk = 10
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

struct ImportPageRecord {
    ImportPageRecord() = default;
    ImportPageRecord(double width, double height) : width_mm(width), height_mm(height) {}

    double width_mm{0};
    double height_mm{0};
    uint32_t local_id{1};
    uint32_t source_page_index{0};
    std::string name;
    uint16_t rotation_degrees{0};
    bool blank{false};
    bool has_manifest_metadata{false};
};

struct ImportLayerRecord {
    uint32_t local_id{0};
    std::string source_id;
    std::string name;
    bool visible{true};
    bool locked{false};
    std::vector<uint32_t> object_local_ids;
    uint32_t page_local_id{1};
    bool has_page_association{false};
};

struct ImportGroupRecord {
    uint32_t local_id{0};
    std::string source_id;
    std::string name;
    std::vector<uint32_t> child_local_ids;
    // Zero means no clip. When present, this must identify a Path child.
    // The wire codec accepts the legacy payload without this trailing field.
    uint32_t clip_mask_local_id{0};
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
    std::string font_postscript_name;
    uint16_t font_weight{400};
    bool italic{false};
    uint16_t rotation_degrees{0};
    double font_size_pt{12};
    double x_mm{0};
    double y_mm{0};
    ImportColorRecord fill;
};

struct ImportRasterImageRecord {
    uint32_t local_id{0};
    std::string source_id;
    std::string name;
    std::string media_type;
    uint32_t width_px{0};
    uint32_t height_px{0};
    uint32_t decoded_pixel_bytes{0};
    uint32_t source_bits_per_pixel{0};
    uint32_t frame_index{0};
    uint32_t frame_count{1};
    uint16_t exif_orientation{1};
    bool orientation_applied{false};
    bool dpi_from_source{false};
    double dpi_x{96.0};
    double dpi_y{96.0};
    std::vector<uint8_t> icc_profile;
    // Optional placement in page coordinates; all-zero keeps source-DPI sizing.
    double x_mm{0.0};
    double y_mm{0.0};
    double width_mm{0.0};
    double height_mm{0.0};
    bool has_placement_transform{false};
    std::array<double, 6> placement_transform{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    // Zero means this record owns its pixel chunks; otherwise it references an
    // earlier raster record that carries the same decoded image data.
    uint32_t pixel_data_local_id{0};
    // Original encoded source bytes are separate from the editable RGBA preview.
    // Zero means the source bytes are not present in this proposal.
    uint32_t source_data_local_id{0};
    uint32_t source_bytes_size{0};
};

struct ImportRasterImageChunkRecord {
    uint32_t local_id{0};
    uint32_t byte_offset{0};
    uint32_t total_bytes{0};
    std::vector<uint8_t> bytes;
};

struct ImportRasterSourceChunkRecord {
    uint32_t local_id{0};
    uint32_t byte_offset{0};
    uint32_t total_bytes{0};
    std::vector<uint8_t> bytes;
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
    ImportRasterImageRecord,
    ImportRasterImageChunkRecord,
    ImportRasterSourceChunkRecord,
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
