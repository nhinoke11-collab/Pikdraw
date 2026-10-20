#include "pikdraw/interop/adapters/svg_worker_adapter.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <type_traits>
#include <unordered_set>

namespace pikdraw::interop {
namespace {
constexpr uint32_t kMaxRasterPixelBytes = UINT32_MAX;
constexpr uint32_t kMaxRasterChunkBytes = 256u * 1024u;
constexpr uint32_t kMaxRasterIccBytes = 256u * 1024u;

bool ValidRasterPlacementTransform(bool enabled, const std::array<double, 6>& matrix) {
    if (!enabled) return true;
    if (!std::all_of(matrix.begin(), matrix.end(), [](double value) {
            return std::isfinite(value);
        })) return false;
    return std::abs(matrix[0]) <= 1.0e6 && std::abs(matrix[1]) <= 1.0e6 &&
           std::abs(matrix[2]) <= 1.0e6 && std::abs(matrix[3]) <= 1.0e6 &&
           std::abs(matrix[4]) <= 1.0e7 && std::abs(matrix[5]) <= 1.0e7 &&
           std::abs(matrix[0] * matrix[3] - matrix[1] * matrix[2]) > 1.0e-12;
}

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
    bool RawBytes(std::vector<uint8_t>& value, size_t count, size_t maximum) {
        if (count > maximum || count > bytes_.size()) return false;
        value.assign(bytes_.begin(), bytes_.begin() + static_cast<std::ptrdiff_t>(count));
        bytes_ = bytes_.subspan(count);
        return true;
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


bool ValidRecord(const ImportPageRecord& record) {
    return record.local_id != 0 && std::isfinite(record.width_mm) && std::isfinite(record.height_mm) && record.width_mm > 0 &&
           record.height_mm > 0 && record.width_mm <= SvgImporter::kMaxPageMillimeters &&
           record.height_mm <= SvgImporter::kMaxPageMillimeters &&
           (!record.has_manifest_metadata ||
            (!record.name.empty() && record.name.size() <= 256 &&
             record.rotation_degrees < 360 && record.rotation_degrees % 90 == 0));
}

bool ValidLocalReferences(std::span<const uint32_t> references, uint32_t owner_id) {
    if (references.size() > IPC_MAX_OBJECTS) return false;
    std::unordered_set<uint32_t> unique;
    unique.reserve(references.size());
    for (const uint32_t reference : references) {
        if (reference == 0 || reference == owner_id || !unique.insert(reference).second) return false;
    }
    return true;
}

bool WriteLocalReferences(RecordWriter& writer, std::span<const uint32_t> references, uint32_t owner_id) {
    if (!ValidLocalReferences(references, owner_id) ||
        !writer.U32(static_cast<uint32_t>(references.size()))) return false;
    for (const uint32_t reference : references) if (!writer.U32(reference)) return false;
    return true;
}

bool ReadLocalReferences(RecordReader& reader, std::vector<uint32_t>& references, uint32_t owner_id) {
    uint32_t count = 0;
    if (!reader.U32(count) || count > IPC_MAX_OBJECTS) return false;
    references.resize(count);
    for (uint32_t& reference : references) if (!reader.U32(reference)) return false;
    return ValidLocalReferences(references, owner_id);
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
            const bool has_manifest = record.has_manifest_metadata || record.local_id != 1 ||
                record.source_page_index != 0 || !record.name.empty() ||
                record.rotation_degrees != 0 || record.blank;
            return ValidRecord(record) && writer.Double(record.width_mm) && writer.Double(record.height_mm) &&
                   (!has_manifest ||
                    (writer.U32(record.local_id) && writer.U32(record.source_page_index) &&
                     writer.U8(record.blank ? 1 : 0) && writer.U8(0) &&
                     writer.String16(record.name, 256) &&
                     (record.rotation_degrees == 0 || writer.U16(record.rotation_degrees))));
        } else if constexpr (std::is_same_v<T, ImportLayerRecord>) {
            type = ImportRecordType::Layer;
            return record.local_id != 0 && record.page_local_id != 0 && writer.U32(record.local_id) &&
                   writer.String16(record.source_id, 128) && writer.String16(record.name, 128) &&
                   writer.U8(record.visible ? 1 : 0) && writer.U8(record.locked ? 1 : 0) && writer.U16(0) &&
                   WriteLocalReferences(writer, record.object_local_ids, 0) &&
                   (!(record.has_page_association || record.page_local_id != 1) ||
                    writer.U32(record.page_local_id));
        } else if constexpr (std::is_same_v<T, ImportGroupRecord>) {
            type = ImportRecordType::Group;
            const bool clip_is_child = record.clip_mask_local_id == 0 ||
                std::find(record.child_local_ids.begin(), record.child_local_ids.end(),
                          record.clip_mask_local_id) != record.child_local_ids.end();
            return record.local_id != 0 && writer.U32(record.local_id) &&
                   writer.String16(record.source_id, 128) && writer.String16(record.name, 128) &&
                   WriteLocalReferences(writer, record.child_local_ids, record.local_id) &&
                   clip_is_child && (record.clip_mask_local_id == 0 ||
                                     writer.U32(record.clip_mask_local_id));
        } else if constexpr (std::is_same_v<T, ImportRasterImageRecord>) {
            type = ImportRecordType::RasterImage;
            const uint64_t expected_pixel_bytes =
                static_cast<uint64_t>(record.width_px) * record.height_px * 4;
            const bool supported_media_type = record.media_type == "image/png" ||
                record.media_type == "image/jpeg" || record.media_type == "image/tiff" ||
                record.media_type == "image/bmp" ||
                record.media_type == "application/x-pikdraw-rgba8";
            if (record.local_id == 0 || !supported_media_type ||
                record.width_px == 0 || record.height_px == 0 ||
                expected_pixel_bytes == 0 || expected_pixel_bytes > kMaxRasterPixelBytes ||
                record.decoded_pixel_bytes != expected_pixel_bytes ||
                record.source_bits_per_pixel == 0 || record.source_bits_per_pixel > 128 ||
                ((record.source_data_local_id == 0) != (record.source_bytes_size == 0)) ||
                record.frame_count == 0 || record.frame_count > 256 ||
                record.frame_index >= record.frame_count ||
                record.exif_orientation == 0 || record.exif_orientation > 8 ||
                !std::isfinite(record.dpi_x) || !std::isfinite(record.dpi_y) ||
                record.dpi_x <= 0 || record.dpi_y <= 0 ||
                record.dpi_x > 1000000 || record.dpi_y > 1000000 ||
                record.icc_profile.size() > kMaxRasterIccBytes ||
                !std::isfinite(record.x_mm) || !std::isfinite(record.y_mm) ||
                !std::isfinite(record.width_mm) || !std::isfinite(record.height_mm) ||
                !ValidRasterPlacementTransform(record.has_placement_transform,
                                               record.placement_transform) ||
                !((record.x_mm == 0.0 && record.y_mm == 0.0 &&
                   record.width_mm == 0.0 && record.height_mm == 0.0) ||
                  (std::abs(record.x_mm) <= 1.0e7 && std::abs(record.y_mm) <= 1.0e7 &&
                   record.width_mm > 0.0 && record.height_mm > 0.0 &&
                   record.width_mm <= 1.0e6 && record.height_mm <= 1.0e6)))
                return false;
            return writer.U32(record.local_id) &&
                   writer.String16(record.source_id, 128) &&
                   writer.String16(record.name, 128) &&
                   writer.String16(record.media_type, 32) &&
                   writer.U32(record.width_px) && writer.U32(record.height_px) &&
                   writer.U32(record.decoded_pixel_bytes) &&
                   writer.U32(record.source_bits_per_pixel) &&
                   writer.U32(record.frame_index) && writer.U32(record.frame_count) &&
                   writer.U16(record.exif_orientation) &&
                   writer.U8(record.orientation_applied ? 1 : 0) &&
                   writer.U8(record.dpi_from_source ? 1 : 0) &&
                   writer.Double(record.dpi_x) && writer.Double(record.dpi_y) &&
                   writer.U32(static_cast<uint32_t>(record.icc_profile.size())) &&
                   writer.Bytes(record.icc_profile) &&
                   writer.Double(record.x_mm) && writer.Double(record.y_mm) &&
                   writer.Double(record.width_mm) && writer.Double(record.height_mm) &&
                   writer.U32(record.pixel_data_local_id) &&
                   (!(record.has_placement_transform || record.source_bytes_size != 0) ||
                    (writer.U8(record.has_placement_transform ? 1 : 0) &&
                     writer.U8(0) && writer.U16(0) &&
                     (!record.has_placement_transform ||
                      (writer.Double(record.placement_transform[0]) &&
                       writer.Double(record.placement_transform[1]) &&
                       writer.Double(record.placement_transform[2]) &&
                       writer.Double(record.placement_transform[3]) &&
                       writer.Double(record.placement_transform[4]) &&
                       writer.Double(record.placement_transform[5]))) &&
                     (record.source_bytes_size == 0 ||
                      (record.source_data_local_id != 0 &&
                       writer.U32(record.source_data_local_id) &&
                       writer.U32(record.source_bytes_size)))));
        } else if constexpr (std::is_same_v<T, ImportRasterImageChunkRecord>) {
            type = ImportRecordType::RasterImageChunk;
            if (record.local_id == 0 || record.total_bytes == 0 ||
                record.total_bytes > kMaxRasterPixelBytes || record.bytes.empty() ||
                record.bytes.size() > kMaxRasterChunkBytes ||
                record.byte_offset > record.total_bytes ||
                record.bytes.size() > record.total_bytes - record.byte_offset)
                return false;
            return writer.U32(record.local_id) && writer.U32(record.byte_offset) &&
                   writer.U32(record.total_bytes) &&
                   writer.U32(static_cast<uint32_t>(record.bytes.size())) &&
                   writer.Bytes(record.bytes);
        } else if constexpr (std::is_same_v<T, ImportRasterSourceChunkRecord>) {
            type = ImportRecordType::RasterSourceChunk;
            if (record.local_id == 0 || record.total_bytes == 0 || record.bytes.empty() ||
                record.bytes.size() > kMaxRasterChunkBytes ||
                record.byte_offset > record.total_bytes ||
                record.bytes.size() > record.total_bytes - record.byte_offset)
                return false;
            return writer.U32(record.local_id) && writer.U32(record.byte_offset) &&
                   writer.U32(record.total_bytes) &&
                   writer.U32(static_cast<uint32_t>(record.bytes.size())) &&
                   writer.Bytes(record.bytes);
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
                   record.font_weight % 100 == 0 &&
                   (record.rotation_degrees == 0 ||
                    (record.rotation_degrees <= 270 && record.rotation_degrees % 90 == 0)) &&
                   std::isfinite(record.font_size_pt) && record.font_size_pt > 0 &&
                   record.font_size_pt <= 100000 && std::isfinite(record.x_mm) && std::abs(record.x_mm) <= 1.0e7 &&
                   std::isfinite(record.y_mm) && std::abs(record.y_mm) <= 1.0e7 && ValidColor(record.fill) &&
                   writer.U32(record.local_id) && writer.String16(record.source_id, 128) &&
                   writer.String16(record.name, 128) && writer.String16(record.utf8_text, 256 * 1024, true) &&
                   writer.String16(record.font_family, 256) && writer.U16(record.font_weight) &&
                   writer.U8(record.italic ? 1 : 0) && writer.U8(0) && writer.Double(record.font_size_pt) &&
                   writer.Double(record.x_mm) && writer.Double(record.y_mm) && WriteColor(writer, record.fill) &&
                   ((record.font_postscript_name.empty() && record.rotation_degrees == 0) ||
                    writer.String16(record.font_postscript_name, 256)) &&
                   (record.rotation_degrees == 0 || writer.U16(record.rotation_degrees));
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
        if (!reader.Double(record.width_mm) || !reader.Double(record.height_mm)) return std::nullopt;
        if (!reader.Done()) {
            uint8_t blank = 0, reserved = 0;
            if (!reader.U32(record.local_id) || !reader.U32(record.source_page_index) ||
                !reader.U8(blank) || blank > 1 || !reader.U8(reserved) || reserved != 0 ||
                !reader.String16(record.name, 256) ||
                (!reader.Done() && !reader.U16(record.rotation_degrees)) || !reader.Done())
                return std::nullopt;
            record.blank = blank != 0;
            record.has_manifest_metadata = true;
        }
        if (!ValidRecord(record)) return std::nullopt;
        return record;
    }
    if (type == ImportRecordType::Layer) {
        ImportLayerRecord record;
        uint8_t visible = 0, locked = 0;
        uint16_t reserved = 0;
        if (!reader.U32(record.local_id) || record.local_id == 0 ||
            !reader.String16(record.source_id, 128) || !reader.String16(record.name, 128) ||
            !reader.U8(visible) || visible > 1 || !reader.U8(locked) || locked > 1 ||
            !reader.U16(reserved) || reserved != 0 ||
            !ReadLocalReferences(reader, record.object_local_ids, 0)) return std::nullopt;
        if (!reader.Done()) {
            if (!reader.U32(record.page_local_id) || record.page_local_id == 0 || !reader.Done())
                return std::nullopt;
            record.has_page_association = true;
        }
        record.visible = visible != 0;
        record.locked = locked != 0;
        return record;
    }
    if (type == ImportRecordType::Group) {
        ImportGroupRecord record;
        if (!reader.U32(record.local_id) || record.local_id == 0 ||
            !reader.String16(record.source_id, 128) || !reader.String16(record.name, 128) ||
            !ReadLocalReferences(reader, record.child_local_ids, record.local_id)) return std::nullopt;
        // Older C6 group records ended after the child list.
        if (!reader.Done() && !reader.U32(record.clip_mask_local_id)) return std::nullopt;
        if (!reader.Done() ||
            (record.clip_mask_local_id != 0 &&
             std::find(record.child_local_ids.begin(), record.child_local_ids.end(),
                       record.clip_mask_local_id) == record.child_local_ids.end()))
            return std::nullopt;
        return record;
    }
    if (type == ImportRecordType::RasterImage) {
        ImportRasterImageRecord record;
        uint8_t orientation_applied = 0, dpi_from_source = 0;
        uint32_t icc_bytes = 0;
        if (!reader.U32(record.local_id) || record.local_id == 0 ||
            !reader.String16(record.source_id, 128) ||
            !reader.String16(record.name, 128) ||
            !reader.String16(record.media_type, 32) ||
            !reader.U32(record.width_px) || !reader.U32(record.height_px) ||
            !reader.U32(record.decoded_pixel_bytes) ||
            !reader.U32(record.source_bits_per_pixel) ||
            !reader.U32(record.frame_index) || !reader.U32(record.frame_count) ||
            !reader.U16(record.exif_orientation) ||
            !reader.U8(orientation_applied) || orientation_applied > 1 ||
            !reader.U8(dpi_from_source) || dpi_from_source > 1 ||
            !reader.Double(record.dpi_x) || !reader.Double(record.dpi_y) ||
            !reader.U32(icc_bytes) || icc_bytes > kMaxRasterIccBytes ||
            !reader.RawBytes(record.icc_profile, icc_bytes, kMaxRasterIccBytes))
            return std::nullopt;
        if (!reader.Done() &&
            (!reader.Double(record.x_mm) || !reader.Double(record.y_mm) ||
             !reader.Double(record.width_mm) || !reader.Double(record.height_mm)))
            return std::nullopt;
        if (!reader.Done() && !reader.U32(record.pixel_data_local_id))
            return std::nullopt;
        if (!reader.Done()) {
            uint8_t has_transform = 0, reserved = 0;
            uint16_t reserved16 = 0;
            if (!reader.U8(has_transform) || has_transform > 1 ||
                !reader.U8(reserved) || reserved != 0 ||
                !reader.U16(reserved16) || reserved16 != 0)
                return std::nullopt;
            record.has_placement_transform = has_transform != 0;
            if (record.has_placement_transform &&
                (!reader.Double(record.placement_transform[0]) ||
                 !reader.Double(record.placement_transform[1]) ||
                 !reader.Double(record.placement_transform[2]) ||
                 !reader.Double(record.placement_transform[3]) ||
                 !reader.Double(record.placement_transform[4]) ||
                 !reader.Double(record.placement_transform[5])))
                return std::nullopt;
        }
        if (!reader.Done() &&
            (!reader.U32(record.source_data_local_id) ||
             !reader.U32(record.source_bytes_size)))
            return std::nullopt;
        if (!reader.Done()) return std::nullopt;
        record.orientation_applied = orientation_applied != 0;
        record.dpi_from_source = dpi_from_source != 0;
        const uint64_t expected_pixel_bytes =
            static_cast<uint64_t>(record.width_px) * record.height_px * 4;
        if ((record.media_type != "image/png" && record.media_type != "image/jpeg" &&
             record.media_type != "image/tiff" && record.media_type != "image/bmp" &&
             record.media_type != "application/x-pikdraw-rgba8") ||
            record.width_px == 0 || record.height_px == 0 ||
            expected_pixel_bytes == 0 || expected_pixel_bytes > kMaxRasterPixelBytes ||
            record.decoded_pixel_bytes != expected_pixel_bytes ||
            record.source_bits_per_pixel == 0 || record.source_bits_per_pixel > 128 ||
            ((record.source_data_local_id == 0) != (record.source_bytes_size == 0)) ||
            record.frame_count == 0 || record.frame_count > 256 ||
            record.frame_index >= record.frame_count ||
            record.exif_orientation == 0 || record.exif_orientation > 8 ||
            record.dpi_x <= 0 || record.dpi_y <= 0 ||
            record.dpi_x > 1000000 || record.dpi_y > 1000000 ||
            !ValidRasterPlacementTransform(record.has_placement_transform,
                                           record.placement_transform))
            return std::nullopt;
        return record;
    }
    if (type == ImportRecordType::RasterImageChunk) {
        ImportRasterImageChunkRecord record;
        uint32_t chunk_bytes = 0;
        if (!reader.U32(record.local_id) || record.local_id == 0 ||
            !reader.U32(record.byte_offset) || !reader.U32(record.total_bytes) ||
            !reader.U32(chunk_bytes) || chunk_bytes == 0 ||
            chunk_bytes > kMaxRasterChunkBytes ||
            !reader.RawBytes(record.bytes, chunk_bytes, kMaxRasterChunkBytes) ||
            !reader.Done() || record.total_bytes == 0 ||
            record.total_bytes > kMaxRasterPixelBytes ||
            record.byte_offset > record.total_bytes ||
            chunk_bytes > record.total_bytes - record.byte_offset)
            return std::nullopt;
        return record;
    }
    if (type == ImportRecordType::RasterSourceChunk) {
        ImportRasterSourceChunkRecord record;
        uint32_t chunk_bytes = 0;
        if (!reader.U32(record.local_id) || record.local_id == 0 ||
            !reader.U32(record.byte_offset) || !reader.U32(record.total_bytes) ||
            !reader.U32(chunk_bytes) || chunk_bytes == 0 ||
            chunk_bytes > kMaxRasterChunkBytes ||
            !reader.RawBytes(record.bytes, chunk_bytes, kMaxRasterChunkBytes) ||
            !reader.Done() || record.total_bytes == 0 ||
            record.byte_offset > record.total_bytes ||
            chunk_bytes > record.total_bytes - record.byte_offset)
            return std::nullopt;
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
            (!reader.Done() && !reader.String16(record.font_postscript_name, 256)) ||
            (!reader.Done() && !reader.U16(record.rotation_degrees)) || !reader.Done() ||
            (record.rotation_degrees != 0 &&
             (record.rotation_degrees > 270 || record.rotation_degrees % 90 != 0)))
            return std::nullopt;
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


} // namespace pikdraw::interop
