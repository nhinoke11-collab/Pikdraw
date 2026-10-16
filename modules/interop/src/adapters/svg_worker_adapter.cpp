#include "pikdraw/interop/adapters/svg_worker_adapter.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <type_traits>

namespace pikdraw::interop {
namespace {

bool IsValidRecordUtf8(std::string_view input) {
    size_t index = 0;
    while (index < input.size()) {
        const uint8_t lead = static_cast<uint8_t>(input[index++]);
        if (lead < 0x80) continue;
        uint32_t codepoint = 0;
        size_t count = 0;
        if ((lead & 0xe0) == 0xc0) { codepoint = lead & 0x1f; count = 1; }
        else if ((lead & 0xf0) == 0xe0) { codepoint = lead & 0x0f; count = 2; }
        else if ((lead & 0xf8) == 0xf0) { codepoint = lead & 0x07; count = 3; }
        else return false;
        if (count > input.size() - index) return false;
        for (size_t i = 0; i < count; ++i) {
            const uint8_t next = static_cast<uint8_t>(input[index++]);
            if ((next & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (next & 0x3f);
        }
        if ((count == 1 && codepoint < 0x80) || (count == 2 && codepoint < 0x800) ||
            (count == 3 && codepoint < 0x10000) || codepoint > 0x10ffff ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
    }
    return true;
}

class RecordWriter final {
public:
    bool U8(uint8_t value) { return Integer(value, 1); }
    bool U16(uint16_t value) { return Integer(value, 2); }
    bool U32(uint32_t value) { return Integer(value, 4); }
    bool U64(uint64_t value) { return Integer(value, 8); }
    bool Double(double value) { return U64(std::bit_cast<uint64_t>(value)); }
    bool String16(const std::string& value, size_t maximum, bool allow_empty = false) {
        if ((!allow_empty && value.empty()) || value.size() > maximum || value.size() > UINT16_MAX ||
            value.find('\0') != std::string::npos || !IsValidRecordUtf8(value) ||
            !U16(static_cast<uint16_t>(value.size()))) return false;
        return Bytes(std::span(reinterpret_cast<const uint8_t*>(value.data()), value.size()));
    }
    bool Bytes(std::span<const uint8_t> bytes) {
        if (bytes.size() > IPC_V2_MAX_RECORD_BYTES - data_.size()) return false;
        data_.insert(data_.end(), bytes.begin(), bytes.end());
        return true;
    }
    std::vector<uint8_t> Take() && { return std::move(data_); }

private:
    bool Integer(uint64_t value, size_t size) {
        if (size > IPC_V2_MAX_RECORD_BYTES - data_.size()) return false;
        for (size_t i = 0; i < size; ++i) data_.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xff));
        return true;
    }
    std::vector<uint8_t> data_;
};

class RecordReader final {
public:
    explicit RecordReader(std::span<const uint8_t> bytes) : bytes_(bytes) {}
    bool U8(uint8_t& value) { return Integer(value, 1); }
    bool U16(uint16_t& value) { return Integer(value, 2); }
    bool U32(uint32_t& value) { return Integer(value, 4); }
    bool U64(uint64_t& value) { return Integer(value, 8); }
    bool Double(double& value) {
        uint64_t bits = 0;
        if (!U64(bits)) return false;
        value = std::bit_cast<double>(bits);
        return std::isfinite(value);
    }
    bool String16(std::string& value, size_t maximum, bool allow_empty = false) {
        uint16_t size = 0;
        if (!U16(size) || (!allow_empty && size == 0) || size > maximum || size > bytes_.size()) return false;
        value.assign(reinterpret_cast<const char*>(bytes_.data()), size);
        bytes_ = bytes_.subspan(size);
        return value.find('\0') == std::string::npos && IsValidRecordUtf8(value);
    }
    [[nodiscard]] bool Done() const noexcept { return bytes_.empty(); }

private:
    template <typename T>
    bool Integer(T& value, size_t size) {
        if (size > bytes_.size()) return false;
        uint64_t decoded = 0;
        for (size_t i = 0; i < size; ++i) decoded |= static_cast<uint64_t>(bytes_[i]) << (i * 8);
        value = static_cast<T>(decoded);
        bytes_ = bytes_.subspan(size);
        return true;
    }
    std::span<const uint8_t> bytes_;
};

bool ValidColor(const ImportColorRecord& color) {
    if (color.overprint_flags > 3) return false;
    for (double component : color.components) {
        if (!std::isfinite(component) || component < 0 || component > 1) return false;
    }
    for (double component : color.alternate_rgb) {
        if (!std::isfinite(component) || component < 0 || component > 1) return false;
    }
    if (color.model == ImportColorModel::Spot) {
        return !color.spot_name.empty() && color.spot_name.size() <= 128 && IsValidRecordUtf8(color.spot_name) &&
               color.components[2] == 0 && color.components[3] == 0 && color.components[4] == 0 &&
               color.components[1] == color.alternate_rgb[3];
    }
    if (!color.spot_name.empty()) return false;
    if (std::any_of(color.alternate_rgb.begin(), color.alternate_rgb.end(), [](double component) { return component != 0; })) return false;
    if (color.model == ImportColorModel::Rgb) return color.components[4] == 0;
    if (color.model == ImportColorModel::Cmyk) return true;
    if (color.model == ImportColorModel::Gray) {
        return color.components[2] == 0 && color.components[3] == 0 && color.components[4] == 0;
    }
    return false;
}

bool WriteColor(RecordWriter& writer, const ImportColorRecord& color) {
    if (!ValidColor(color) || !writer.U8(static_cast<uint8_t>(color.model)) ||
        !writer.U8(color.overprint_flags) || !writer.U16(0)) return false;
    for (double value : color.components) if (!writer.Double(value)) return false;
    if (!writer.String16(color.spot_name, 128, true)) return false;
    for (double value : color.alternate_rgb) if (!writer.Double(value)) return false;
    return true;
}

bool ReadColor(RecordReader& reader, ImportColorRecord& color) {
    uint8_t model = 0, flags = 0;
    uint16_t reserved = 0;
    if (!reader.U8(model) || !reader.U8(flags) || !reader.U16(reserved) || reserved != 0 ||
        model < static_cast<uint8_t>(ImportColorModel::Rgb) || model > static_cast<uint8_t>(ImportColorModel::Gray)) return false;
    color.model = static_cast<ImportColorModel>(model);
    color.overprint_flags = flags;
    for (double& value : color.components) if (!reader.Double(value)) return false;
    if (!reader.String16(color.spot_name, 128, true)) return false;
    for (double& value : color.alternate_rgb) if (!reader.Double(value)) return false;
    return ValidColor(color);
}

bool ValidElement(const ImportPathElementRecord& element) {
    if (element.flags > 3) return false;
    for (double value : element.values) if (!std::isfinite(value) || std::abs(value) > 1.0e7) return false;
    if (element.verb == ImportPathVerb::Move || element.verb == ImportPathVerb::Line) {
        if (element.flags != 0) return false;
        for (size_t i = 2; i < element.values.size(); ++i) if (element.values[i] != 0) return false;
    } else if (element.verb == ImportPathVerb::Quad) {
        if (element.flags != 0) return false;
        for (size_t i = 4; i < element.values.size(); ++i) if (element.values[i] != 0) return false;
    } else if (element.verb == ImportPathVerb::Cubic) {
        if (element.flags != 0) return false;
        for (size_t i = 6; i < element.values.size(); ++i) if (element.values[i] != 0) return false;
    } else if (element.verb == ImportPathVerb::Arc) {
        if (element.values[2] != 0 || element.values[3] != 0 || element.values[4] != 0 || element.values[5] != 0 ||
            element.values[6] < 0 || element.values[7] < 0) return false;
    } else if (element.verb == ImportPathVerb::Close) {
        if (element.flags != 0) return false;
        for (double value : element.values) if (value != 0) return false;
    } else return false;
    return true;
}

bool WritePathElement(RecordWriter& writer, const ImportPathElementRecord& element) {
    if (!ValidElement(element) || !writer.U8(static_cast<uint8_t>(element.verb)) ||
        !writer.U8(element.flags) || !writer.U16(0)) return false;
    for (double value : element.values) if (!writer.Double(value)) return false;
    return true;
}

bool ReadPathElement(RecordReader& reader, ImportPathElementRecord& element) {
    uint8_t verb = 0;
    uint16_t reserved = 0;
    if (!reader.U8(verb) || !reader.U8(element.flags) || !reader.U16(reserved) || reserved != 0 ||
        verb < static_cast<uint8_t>(ImportPathVerb::Move) || verb > static_cast<uint8_t>(ImportPathVerb::Close)) return false;
    element.verb = static_cast<ImportPathVerb>(verb);
    for (double& value : element.values) if (!reader.Double(value)) return false;
    return ValidElement(element);
}

ImportColorRecord FromColor(const color::ColorValue& value) {
    ImportColorRecord result;
    result.overprint_flags = static_cast<uint8_t>((value.overprint().fill ? 1 : 0) | (value.overprint().stroke ? 2 : 0));
    std::visit([&](const auto& color_value) {
        using T = std::decay_t<decltype(color_value)>;
        if constexpr (std::is_same_v<T, color::RGBColor>) {
            result.model = ImportColorModel::Rgb;
            result.components = {color_value.r, color_value.g, color_value.b, color_value.a, 0};
        } else if constexpr (std::is_same_v<T, color::CMYKColor>) {
            result.model = ImportColorModel::Cmyk;
            result.components = {color_value.c, color_value.m, color_value.y, color_value.k, color_value.a};
        } else if constexpr (std::is_same_v<T, color::SpotColor>) {
            result.model = ImportColorModel::Spot;
            result.components = {color_value.tint, color_value.alternate_rgb.a, 0, 0, 0};
            result.spot_name = color_value.spot_name;
            result.alternate_rgb = {color_value.alternate_rgb.r, color_value.alternate_rgb.g,
                                    color_value.alternate_rgb.b, color_value.alternate_rgb.a};
            if (color_value.overprint) result.overprint_flags |= 1;
        } else {
            result.model = ImportColorModel::Gray;
            result.components = {color_value.gray, color_value.a, 0, 0, 0};
        }
    }, value.payload());
    return result;
}

bool ValidRecord(const ImportPageRecord& record) {
    return std::isfinite(record.width_mm) && std::isfinite(record.height_mm) && record.width_mm > 0 &&
           record.height_mm > 0 && record.width_mm <= SvgImporter::kMaxPageMillimeters &&
           record.height_mm <= SvgImporter::kMaxPageMillimeters;
}

bool ValidPathSequence(std::span<const ImportPathElementRecord> elements) {
    if (elements.empty() || elements.front().verb != ImportPathVerb::Move) return false;
    bool has_current_point = false;
    for (const auto& element : elements) {
        if (element.verb == ImportPathVerb::Move) has_current_point = true;
        else if (!has_current_point) return false;
    }
    return true;
}

} // namespace

std::optional<CanonicalImportRecord> SerializeImportRecord(const ImportRecordValue& value) {
    try {
    RecordWriter writer;
    ImportRecordType type{};
    const bool success = std::visit([&](const auto& record) {
        using T = std::decay_t<decltype(record)>;
        if constexpr (std::is_same_v<T, ImportPageRecord>) {
            type = ImportRecordType::Page;
            return ValidRecord(record) && writer.Double(record.width_mm) && writer.Double(record.height_mm);
        } else if constexpr (std::is_same_v<T, ImportPathRecord>) {
            type = ImportRecordType::Path;
            if (record.local_id == 0 || record.winding_rule > 1 || record.elements.empty() ||
                record.elements.size() > SvgImporter::kMaxPathElements || !std::isfinite(record.stroke_width_mm) ||
                record.stroke_width_mm < 0 || record.stroke_width_mm > SvgImporter::kMaxPageMillimeters ||
                !ValidColor(record.fill) || !ValidColor(record.stroke) ||
                !writer.U32(record.local_id) || !writer.String16(record.source_id, 128) ||
                !writer.String16(record.name, 128) || !writer.U8(record.winding_rule) ||
                !writer.U8(record.has_fill ? 1 : 0) || !writer.U8(record.has_stroke ? 1 : 0) || !writer.U8(0) ||
                !writer.Double(record.stroke_width_mm) || !WriteColor(writer, record.fill) ||
                !WriteColor(writer, record.stroke) || !writer.U32(static_cast<uint32_t>(record.elements.size()))) return false;
            if (!ValidPathSequence(record.elements)) return false;
            for (const auto& element : record.elements) if (!WritePathElement(writer, element)) return false;
            return true;
        } else if constexpr (std::is_same_v<T, ImportTextRecord>) {
            type = ImportRecordType::Text;
            return record.local_id != 0 && record.font_weight >= 100 && record.font_weight <= 900 &&
                   record.font_weight % 100 == 0 && std::isfinite(record.font_size_pt) && record.font_size_pt > 0 &&
                   record.font_size_pt <= 100000 && std::isfinite(record.x_mm) && std::abs(record.x_mm) <= 1.0e7 &&
                   std::isfinite(record.y_mm) && std::abs(record.y_mm) <= 1.0e7 && ValidColor(record.fill) &&
                   writer.U32(record.local_id) && writer.String16(record.source_id, 128) &&
                   writer.String16(record.name, 128) && writer.String16(record.utf8_text, 256 * 1024, true) &&
                   writer.String16(record.font_family, 256) && writer.U16(record.font_weight) &&
                   writer.U8(record.italic ? 1 : 0) && writer.U8(0) && writer.Double(record.font_size_pt) &&
                   writer.Double(record.x_mm) && writer.Double(record.y_mm) && WriteColor(writer, record.fill);
        } else if constexpr (std::is_same_v<T, ImportFidelityReportRecord>) {
            type = ImportRecordType::FidelityReport;
            return !record.coverage_profile.empty() && record.issue_count <= SvgImporter::kMaxObjects &&
                   record.parse_outcome >= ImportParseOutcome::Complete && record.parse_outcome <= ImportParseOutcome::CompleteWithDiagnostics &&
                   record.coverage >= ImportCoverage::Partial && record.coverage <= ImportCoverage::Unknown &&
                   record.appearance >= ImportAppearance::Unknown && record.appearance <= ImportAppearance::Partial &&
                   record.editability >= ImportEditability::NativeEditable && record.editability <= ImportEditability::ExplicitConversion &&
                   record.production >= ImportProduction::NotAssessed && record.production <= ImportProduction::Blocked &&
                   writer.String16(record.coverage_profile, 128) &&
                   writer.U8(static_cast<uint8_t>(record.parse_outcome)) &&
                   writer.U8(static_cast<uint8_t>(record.coverage)) &&
                   writer.U8(static_cast<uint8_t>(record.appearance)) &&
                   writer.U8(static_cast<uint8_t>(record.editability)) &&
                   writer.U8(static_cast<uint8_t>(record.production)) && writer.U8(0) && writer.U16(0) &&
                   writer.U32(record.issue_count);
        } else {
            type = ImportRecordType::FidelityIssue;
            return record.severity >= ImportFidelitySeverity::Info && record.severity <= ImportFidelitySeverity::Unsupported &&
                   writer.U8(static_cast<uint8_t>(record.severity)) && writer.U8(0) && writer.U16(0) &&
                   writer.String16(record.source_locator, 128, true) && writer.String16(record.feature_code, 64) &&
                   writer.String16(record.diagnostic_code, 128);
        }
    }, value);
    if (!success) return std::nullopt;
    return CanonicalImportRecord{type, std::move(writer).Take()};
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<ImportRecordValue> DeserializeImportRecord(
    ImportRecordType type, std::span<const uint8_t> payload) {
    try {
    if (payload.empty() || payload.size() > IPC_V2_MAX_RECORD_BYTES) return std::nullopt;
    RecordReader reader(payload);
    if (type == ImportRecordType::Page) {
        ImportPageRecord record;
        if (!reader.Double(record.width_mm) || !reader.Double(record.height_mm) || !reader.Done() || !ValidRecord(record)) return std::nullopt;
        return record;
    }
    if (type == ImportRecordType::Path) {
        ImportPathRecord record;
        uint8_t has_fill = 0, has_stroke = 0, reserved = 0;
        uint32_t count = 0;
        if (!reader.U32(record.local_id) || record.local_id == 0 || !reader.String16(record.source_id, 128) ||
            !reader.String16(record.name, 128) || !reader.U8(record.winding_rule) || record.winding_rule > 1 ||
            !reader.U8(has_fill) || has_fill > 1 || !reader.U8(has_stroke) || has_stroke > 1 ||
            !reader.U8(reserved) || reserved != 0 || !reader.Double(record.stroke_width_mm) ||
            record.stroke_width_mm < 0 || record.stroke_width_mm > SvgImporter::kMaxPageMillimeters ||
            !ReadColor(reader, record.fill) || !ReadColor(reader, record.stroke) || !reader.U32(count) ||
            count == 0 || count > SvgImporter::kMaxPathElements) return std::nullopt;
        record.has_fill = has_fill != 0;
        record.has_stroke = has_stroke != 0;
        record.elements.resize(count);
        for (auto& element : record.elements) if (!ReadPathElement(reader, element)) return std::nullopt;
        if (!reader.Done() || !ValidPathSequence(record.elements)) return std::nullopt;
        return record;
    }
    if (type == ImportRecordType::Text) {
        ImportTextRecord record;
        uint8_t italic = 0, reserved = 0;
        if (!reader.U32(record.local_id) || record.local_id == 0 || !reader.String16(record.source_id, 128) ||
            !reader.String16(record.name, 128) || !reader.String16(record.utf8_text, 256 * 1024, true) ||
            !reader.String16(record.font_family, 256) || !reader.U16(record.font_weight) ||
            record.font_weight < 100 || record.font_weight > 900 || record.font_weight % 100 != 0 ||
            !reader.U8(italic) || italic > 1 || !reader.U8(reserved) || reserved != 0 ||
            !reader.Double(record.font_size_pt) || record.font_size_pt <= 0 || record.font_size_pt > 100000 ||
            !reader.Double(record.x_mm) || std::abs(record.x_mm) > 1.0e7 ||
            !reader.Double(record.y_mm) || std::abs(record.y_mm) > 1.0e7 || !ReadColor(reader, record.fill) ||
            !reader.Done()) return std::nullopt;
        record.italic = italic != 0;
        return record;
    }
    if (type == ImportRecordType::FidelityReport) {
        ImportFidelityReportRecord record;
        uint8_t outcome = 0, coverage = 0, appearance = 0, editability = 0, production = 0, reserved8 = 0;
        uint16_t reserved16 = 0;
        if (!reader.String16(record.coverage_profile, 128) || !reader.U8(outcome) || !reader.U8(coverage) ||
            !reader.U8(appearance) || !reader.U8(editability) || !reader.U8(production) ||
            !reader.U8(reserved8) || reserved8 != 0 || !reader.U16(reserved16) || reserved16 != 0 ||
            !reader.U32(record.issue_count) || record.issue_count > SvgImporter::kMaxObjects || !reader.Done() ||
            outcome < 1 || outcome > 2 || coverage < 1 || coverage > 2 || appearance < 1 || appearance > 2 ||
            editability < 1 || editability > 2 || production < 1 || production > 2) return std::nullopt;
        record.parse_outcome = static_cast<ImportParseOutcome>(outcome);
        record.coverage = static_cast<ImportCoverage>(coverage);
        record.appearance = static_cast<ImportAppearance>(appearance);
        record.editability = static_cast<ImportEditability>(editability);
        record.production = static_cast<ImportProduction>(production);
        return record;
    }
    if (type == ImportRecordType::FidelityIssue) {
        ImportFidelityIssueRecord record;
        uint8_t severity = 0, reserved8 = 0;
        uint16_t reserved16 = 0;
        if (!reader.U8(severity) || severity < 1 || severity > 3 || !reader.U8(reserved8) || reserved8 != 0 ||
            !reader.U16(reserved16) || reserved16 != 0 || !reader.String16(record.source_locator, 128, true) ||
            !reader.String16(record.feature_code, 64) || !reader.String16(record.diagnostic_code, 128) || !reader.Done()) return std::nullopt;
        record.severity = static_cast<ImportFidelitySeverity>(severity);
        return record;
    }
    return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

bool BuildSvgImportRecords(std::span<const uint8_t> source,
                           std::vector<CanonicalImportRecord>& records,
                           std::string& diagnostic) {
    records.clear();
    diagnostic.clear();
    try {
    if (source.empty() || source.size() > SvgImporter::kMaxSourceBytes) {
        diagnostic = "SVG source exceeds the bounded input profile";
        return false;
    }
    const std::string input(reinterpret_cast<const char*>(source.data()), source.size());
    const auto imported = SvgImporter::import_from_string(input);
    if (!imported.success) {
        diagnostic = imported.error_message.empty() ? "SVG parser rejected the source" : imported.error_message;
        return false;
    }

    const auto fail = [&](const char* reason) {
        records.clear();
        diagnostic = reason;
        return false;
    };

    const auto add_record = [&](const ImportRecordValue& value) {
        auto encoded = SerializeImportRecord(value);
        if (!encoded) return false;
        records.push_back(std::move(*encoded));
        return records.size() <= SvgImporter::kMaxObjects * 2 + 2;
    };

    if (!add_record(ImportPageRecord{imported.page.width_mm, imported.page.height_mm})) {
        return fail("SVG page record exceeds C6 record limits");
    }
    uint32_t local_id = 1;
    for (size_t index = 0; index < imported.objects.size(); ++index) {
        const std::string source_id = index < imported.source_ids.size() ? imported.source_ids[index] : "unknown-source";
        const auto& object = imported.objects[index];
        if (const auto* path = std::get_if<document::PathObject>(&object)) {
            ImportPathRecord record;
            record.local_id = local_id++;
            record.source_id = source_id;
            record.name = path->name;
            record.winding_rule = static_cast<uint8_t>(path->path.winding_rule());
            record.has_fill = path->has_fill;
            record.has_stroke = path->has_stroke;
            record.stroke_width_mm = path->stroke_width_mm;
            record.fill = FromColor(path->fill);
            record.stroke = FromColor(path->stroke);
            record.elements.reserve(path->path.size());
            for (const auto& element : path->path.elements()) {
                ImportPathElementRecord converted;
                if (element.verb == geometry::PathVerb::MoveTo) converted.verb = ImportPathVerb::Move;
                else if (element.verb == geometry::PathVerb::LineTo) converted.verb = ImportPathVerb::Line;
                else if (element.verb == geometry::PathVerb::QuadTo) converted.verb = ImportPathVerb::Quad;
                else if (element.verb == geometry::PathVerb::CubicTo) converted.verb = ImportPathVerb::Cubic;
                else if (element.verb == geometry::PathVerb::ArcTo) converted.verb = ImportPathVerb::Arc;
                else converted.verb = ImportPathVerb::Close;
                converted.values = {element.pt.x, element.pt.y, element.ctrl1.x, element.ctrl1.y,
                                    element.ctrl2.x, element.ctrl2.y, element.rx, element.ry, element.rotation_deg};
                converted.flags = static_cast<uint8_t>((element.large_arc ? 1 : 0) | (element.sweep ? 2 : 0));
                record.elements.push_back(converted);
            }
            if (!add_record(record)) return fail("SVG path record exceeds C6 record limits");
        } else if (const auto* text = std::get_if<document::TextObject>(&object)) {
            ImportTextRecord record;
            record.local_id = local_id++;
            record.source_id = source_id;
            record.name = text->name;
            record.utf8_text = text->text;
            record.font_family = text->font.family_name;
            record.font_weight = text->font.weight;
            record.italic = text->font.italic;
            record.font_size_pt = text->font.size_pt;
            record.x_mm = text->position.x;
            record.y_mm = text->position.y;
            record.fill = FromColor(text->fill);
            if (!add_record(record)) return fail("SVG text record exceeds C6 record limits");
        } else {
            return fail("SVG group records are not supported by the current adapter profile");
        }
    }

    for (const auto& issue : imported.report.issues()) {
        ImportFidelityIssueRecord record;
        record.severity = issue.severity == FidelitySeverity::Info ? ImportFidelitySeverity::Info :
            issue.severity == FidelitySeverity::Unsupported ? ImportFidelitySeverity::Unsupported : ImportFidelitySeverity::Warning;
        record.source_locator = issue.element_name;
        record.feature_code = issue.feature;
        record.diagnostic_code = "appearance_or_fidelity_unverified";
        if (!add_record(record)) return fail("SVG fidelity report exceeds C6 record limits");
    }
    ImportFidelityReportRecord report;
    report.coverage_profile = "svg-flat-v1";
    report.parse_outcome = imported.report.is_clean() ? ImportParseOutcome::Complete : ImportParseOutcome::CompleteWithDiagnostics;
    report.coverage = ImportCoverage::Partial;
    report.appearance = imported.report.is_clean() ? ImportAppearance::Unknown : ImportAppearance::Partial;
    report.editability = ImportEditability::NativeEditable;
    report.production = ImportProduction::NotAssessed;
    report.issue_count = static_cast<uint32_t>(imported.report.issue_count());
    if (!add_record(report)) return fail("SVG fidelity summary exceeds C6 record limits");

    uint32_t expected_local_id = 1;
    uint32_t issue_count = 0;
    bool issues_started = false;
    bool report_seen = false;
    for (size_t index = 0; index < records.size(); ++index) {
        const auto& record = records[index];
        const auto decoded = DeserializeImportRecord(record.type, record.payload);
        if (!decoded) return fail("SVG proposal record failed producer semantic validation");
        if (record.type == ImportRecordType::Page) {
            if (index != 0) return fail("SVG proposal page record is out of order");
        } else if (record.type == ImportRecordType::Path) {
            const auto* path = std::get_if<ImportPathRecord>(&*decoded);
            if (!path || issues_started || path->local_id != expected_local_id++) {
                return fail("SVG proposal path record order or ID is invalid");
            }
        } else if (record.type == ImportRecordType::Text) {
            const auto* text = std::get_if<ImportTextRecord>(&*decoded);
            if (!text || issues_started || text->local_id != expected_local_id++) {
                return fail("SVG proposal text record order or ID is invalid");
            }
        } else if (record.type == ImportRecordType::FidelityIssue) {
            issues_started = true;
            ++issue_count;
        } else if (record.type == ImportRecordType::FidelityReport) {
            const auto* decoded_report = std::get_if<ImportFidelityReportRecord>(&*decoded);
            if (!decoded_report || index + 1 != records.size() || report_seen || decoded_report->issue_count != issue_count) {
                return fail("SVG proposal fidelity report is absent, duplicated, or inconsistent");
            }
            report_seen = true;
        } else {
            return fail("SVG proposal contains an unknown record type");
        }
    }
    if (!report_seen) return fail("SVG proposal has no terminal fidelity report");
    return true;
    } catch (...) {
        records.clear();
        diagnostic = "SVG proposal allocation or codec failure";
        return false;
    }
}

} // namespace pikdraw::interop
