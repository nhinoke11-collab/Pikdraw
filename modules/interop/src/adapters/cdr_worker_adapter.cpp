#include "pikdraw/interop/adapters/cdr_worker_adapter.hpp"
#include "pikdraw/interop/adapters/raster_image_decoder.hpp"
#include "cdr_worker_adapter_geometry.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace pikdraw::interop {
namespace {

constexpr size_t kMaxCdrSvgBytes = UINT32_MAX;
constexpr size_t kMaxCdrPacketBytes = UINT32_MAX;
constexpr size_t kCdrRasterChunkBytes = kMaxRasterChunkPayloadBytes;

struct CdrImageAsset {
    std::string media_type;
    std::span<const uint8_t> bytes;
};

struct CdrImageInstance {
    uint32_t instance_id{0};
    uint32_t asset_id{0};
};

struct CdrPackage {
    std::span<const uint8_t> svg;
    std::vector<CdrImageInstance> instances;
    std::vector<CdrImageAsset> assets;
};

uint32_t ReadPackageU32(std::span<const uint8_t> bytes, size_t& offset) noexcept {
    if (offset > bytes.size() || bytes.size() - offset < 4) return 0;
    const uint32_t value = static_cast<uint32_t>(bytes[offset]) |
        (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<uint32_t>(bytes[offset + 3]) << 24);
    offset += 4;
    return value;
}

bool DecodeCdrPackage(std::span<const uint8_t> bytes, CdrPackage& package,
                      std::string& diagnostic) {
    package = {};
    if (bytes.size() < 16 || bytes.size() > kMaxCdrPacketBytes) {
        diagnostic = "CDR parser returned an invalid compact image package";
        return false;
    }
    size_t offset = 0;
    if (ReadPackageU32(bytes, offset) != 0x31434450u) {
        diagnostic = "CDR parser returned an unknown compact package version";
        return false;
    }
    const uint32_t svg_size = ReadPackageU32(bytes, offset);
    const uint32_t instance_count = ReadPackageU32(bytes, offset);
    const uint32_t asset_count = ReadPackageU32(bytes, offset);
    if (svg_size == 0 || svg_size > kMaxCdrSvgBytes ||
        asset_count > instance_count || svg_size > bytes.size() - offset) {
        diagnostic = "CDR compact package exceeds the bounded SVG or image profile";
        return false;
    }
    package.svg = bytes.subspan(offset, svg_size);
    offset += svg_size;
    package.instances.reserve(instance_count);
    std::vector<bool> instance_ids(instance_count + 1, false);
    for (uint32_t index = 0; index < instance_count; ++index) {
        const uint32_t instance_id = ReadPackageU32(bytes, offset);
        const uint32_t asset_id = ReadPackageU32(bytes, offset);
        if (offset > bytes.size() || instance_id == 0 || instance_id > instance_count ||
            asset_id == 0 || asset_id > asset_count || instance_ids[instance_id]) {
            diagnostic = "CDR compact package contains an invalid image reference";
            return false;
        }
        instance_ids[instance_id] = true;
        package.instances.push_back({instance_id, asset_id});
    }
    package.assets.reserve(asset_count);
    uint64_t total_asset_bytes = 0;
    for (uint32_t index = 0; index < asset_count; ++index) {
        const uint32_t mime_size = ReadPackageU32(bytes, offset);
        const uint32_t data_size = ReadPackageU32(bytes, offset);
        if (mime_size == 0 || mime_size > 32 || data_size == 0 || offset > bytes.size() ||
            mime_size > bytes.size() - offset ||
            data_size > bytes.size() - offset - mime_size) {
            diagnostic = "CDR compact image asset exceeds the bounded package profile";
            return false;
        }
        CdrImageAsset asset;
        asset.media_type.assign(reinterpret_cast<const char*>(bytes.data() + offset), mime_size);
        offset += mime_size;
        asset.bytes = bytes.subspan(offset, data_size);
        offset += data_size;
        if (asset.media_type != "image/png" && asset.media_type != "image/jpeg" &&
            asset.media_type != "image/tiff" && asset.media_type != "image/bmp") {
            diagnostic = "CDR compact image asset has an unsupported media type";
            return false;
        }
        if (data_size > UINT64_MAX - total_asset_bytes) {
            diagnostic = "CDR compact image byte count overflowed";
            return false;
        }
        total_asset_bytes += data_size;
        package.assets.push_back(std::move(asset));
    }
    if (offset != bytes.size() || (instance_count == 0) != (asset_count == 0)) {
        diagnostic = "CDR compact package contains trailing or incomplete image data";
        return false;
    }
    return true;
}

#ifdef _WIN32
using CdrConvertFunction = int(__cdecl*)(
    const uint8_t*, uint64_t, uint8_t**, uint64_t*, char*, uint32_t);
using CdrFreePackageFunction = void(__cdecl*)(uint8_t*) noexcept;

struct CdrBridgeGuard {
    HMODULE module{nullptr};
    ~CdrBridgeGuard() { if (module) FreeLibrary(module); }
};

bool LoadCdrBridge(CdrBridgeGuard& guard, CdrConvertFunction& convert,
                   CdrFreePackageFunction& free_package, std::string& diagnostic) {
    std::array<wchar_t, 32768> executable_path{};
    const DWORD length = GetModuleFileNameW(nullptr, executable_path.data(),
                                             static_cast<DWORD>(executable_path.size()));
    if (length == 0 || length >= executable_path.size()) {
        diagnostic = "Could not resolve the isolated worker directory for the CDR parser";
        return false;
    }
    const auto bridge_path = std::filesystem::path(executable_path.data()).parent_path() /
                             L"pikdraw_cdr_bridge.dll";
    guard.module = LoadLibraryExW(bridge_path.c_str(), nullptr,
                                  LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                  LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!guard.module) {
        diagnostic = "Pinned libcdr runtime is not installed beside the isolated worker";
        return false;
    }
    convert = reinterpret_cast<CdrConvertFunction>(GetProcAddress(
        guard.module, "PikDrawCdrConvertToPackage"));
    free_package = reinterpret_cast<CdrFreePackageFunction>(GetProcAddress(
        guard.module, "PikDrawCdrFreePackage"));
    if (!convert || !free_package) {
        diagnostic = "Pinned libcdr bridge is missing its package C ABI";
        return false;
    }
    return true;
}
#endif

bool AddCdrFidelityIssue(std::vector<CanonicalImportRecord>& records,
                         std::string& diagnostic,
                         bool dropped_icc_profile) {
    ImportFidelityIssueRecord issue;
    issue.severity = ImportFidelitySeverity::Warning;
    // Empty source_locator means this conversion warning applies to the whole document.
    issue.feature_code = dropped_icc_profile
        ? "cdr-svg-normalization-and-icc-losses" : "cdr-svg-normalization-losses";
    issue.diagnostic_code = "cdr-libcdr-editable-vector-subset";
    auto encoded_issue = SerializeImportRecord(issue);
    if (!encoded_issue) {
        diagnostic = "CDR fidelity issue exceeds C6 record limits";
        return false;
    }

    size_t report_index = records.size();
    for (size_t index = 0; index < records.size(); ++index) {
        auto& record = records[index];
        if (record.type != ImportRecordType::FidelityReport) continue;
        auto decoded = DeserializeImportRecord(record.type, record.payload);
        auto* report = decoded ? std::get_if<ImportFidelityReportRecord>(&*decoded) : nullptr;
        if (!report || report_index != records.size() || index + 1 != records.size()) {
            diagnostic = "CDR vector proposal has an invalid or duplicate fidelity report";
            return false;
        }
        report->coverage_profile = "cdr-libcdr-svg-v2";
        report->parse_outcome = ImportParseOutcome::CompleteWithDiagnostics;
        report->coverage = ImportCoverage::Partial;
        report->appearance = ImportAppearance::Partial;
        report->editability = ImportEditability::NativeEditable;
        if (report->issue_count == std::numeric_limits<uint32_t>::max()) {
            diagnostic = "CDR fidelity issue count overflowed";
            return false;
        }
        ++report->issue_count;
        auto encoded_report = SerializeImportRecord(*report);
        if (!encoded_report) {
            diagnostic = "CDR fidelity summary exceeds C6 record limits";
            return false;
        }
        record = std::move(*encoded_report);
        report_index = index;
    }
    if (report_index == records.size()) {
        diagnostic = "CDR vector proposal has no fidelity summary";
        return false;
    }
    records.insert(records.begin() + static_cast<std::ptrdiff_t>(report_index),
                   std::move(*encoded_issue));
    return true;
}

bool HasCdrLayerId(std::string_view tag) noexcept {
    size_t offset = 0;
    while (offset < tag.size()) {
        while (offset < tag.size() && std::isspace(static_cast<unsigned char>(tag[offset]))) ++offset;
        const size_t name_begin = offset;
        while (offset < tag.size() &&
               (std::isalnum(static_cast<unsigned char>(tag[offset])) || tag[offset] == ':' ||
                tag[offset] == '_' || tag[offset] == '-')) ++offset;
        if (name_begin == offset) { ++offset; continue; }
        const std::string_view name = tag.substr(name_begin, offset - name_begin);
        while (offset < tag.size() && std::isspace(static_cast<unsigned char>(tag[offset]))) ++offset;
        if (offset == tag.size() || tag[offset++] != '=') continue;
        while (offset < tag.size() && std::isspace(static_cast<unsigned char>(tag[offset]))) ++offset;
        if (offset == tag.size() || (tag[offset] != '\'' && tag[offset] != '"')) return false;
        const char quote = tag[offset++];
        const size_t value_begin = offset;
        const size_t value_end = tag.find(quote, value_begin);
        if (value_end == std::string_view::npos) return false;
        if (name == "id") {
            const auto value = tag.substr(value_begin, value_end - value_begin);
            return value.size() >= 5 &&
                (value.starts_with("Layer") || value.starts_with("layer"));
        }
        offset = value_end + 1;
    }
    return false;
}

std::string_view CdrLayerName(std::string_view tag) noexcept {
    size_t offset = 0;
    std::string_view id_value;
    std::string_view label_value;
    while (offset < tag.size()) {
        while (offset < tag.size() && std::isspace(static_cast<unsigned char>(tag[offset]))) ++offset;
        const size_t name_begin = offset;
        while (offset < tag.size() &&
               (std::isalnum(static_cast<unsigned char>(tag[offset])) || tag[offset] == ':' ||
                tag[offset] == '_' || tag[offset] == '-')) ++offset;
        if (name_begin == offset) { ++offset; continue; }
        const auto name = tag.substr(name_begin, offset - name_begin);
        while (offset < tag.size() && std::isspace(static_cast<unsigned char>(tag[offset]))) ++offset;
        if (offset == tag.size() || tag[offset++] != '=') continue;
        while (offset < tag.size() && std::isspace(static_cast<unsigned char>(tag[offset]))) ++offset;
        if (offset == tag.size() || (tag[offset] != '\'' && tag[offset] != '"')) return {};
        const char quote = tag[offset++];
        const size_t value_end = tag.find(quote, offset);
        if (value_end == std::string_view::npos) return {};
        if (name == "id") id_value = tag.substr(offset, value_end - offset);
        else if (name == "inkscape:label" || name == "label" || name == "name")
            label_value = tag.substr(offset, value_end - offset);
        offset = value_end + 1;
    }
    return label_value.empty() ? id_value : label_value;
}

std::string_view TrimAscii(std::string_view value) noexcept {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
    return value;
}

bool ApplyCdrSvgOpacity(std::string& color, double opacity) {
    if (color == "none") return true;
    std::string normalized = color;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (normalized == "black") normalized = "#000000";
    else if (normalized == "white") normalized = "#ffffff";
    else if (normalized == "red") normalized = "#ff0000";
    else if (normalized == "green") normalized = "#008000";
    else if (normalized == "blue") normalized = "#0000ff";
    else if (normalized == "transparent") normalized = "#00000000";
    if (normalized.size() == 4 && normalized[0] == '#') {
        const char r = normalized[1], g = normalized[2], b = normalized[3];
        normalized = std::string("#") + r + r + g + g + b + b;
    }
    if ((normalized.size() != 7 && normalized.size() != 9) || normalized[0] != '#') return false;
    const auto hex = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        return -1;
    };
    int channels[4]{0, 0, 0, 255};
    for (size_t index = 0; index < (normalized.size() == 9 ? 4u : 3u); ++index) {
        const int high = hex(normalized[1 + index * 2]);
        const int low = hex(normalized[2 + index * 2]);
        if (high < 0 || low < 0) return false;
        channels[index] = high * 16 + low;
    }
    channels[3] = static_cast<int>(std::lround(static_cast<double>(channels[3]) * opacity));
    constexpr char digits[] = "0123456789abcdef";
    color.assign("#");
    for (const int channel : channels) {
        color.push_back(digits[(channel >> 4) & 0xf]);
        color.push_back(digits[channel & 0xf]);
    }
    return true;
}

bool IsSupportedCdrTextSpanAttributes(std::string_view tag_attributes) {
    constexpr std::array<std::string_view, 5> supported{
        "font-family", "font-size", "fill", "font-weight", "font-style"};
    std::array<bool, supported.size()> seen{};
    size_t offset = 0;
    while (offset < tag_attributes.size()) {
        while (offset < tag_attributes.size() &&
               std::isspace(static_cast<unsigned char>(tag_attributes[offset]))) ++offset;
        if (offset == tag_attributes.size()) return true;
        const size_t begin = offset;
        while (offset < tag_attributes.size() &&
               (std::isalnum(static_cast<unsigned char>(tag_attributes[offset])) ||
                tag_attributes[offset] == '-' || tag_attributes[offset] == '_' ||
                tag_attributes[offset] == ':')) ++offset;
        if (begin == offset) return false;
        const auto name = tag_attributes.substr(begin, offset - begin);
        const auto found = std::find(supported.begin(), supported.end(), name);
        if (found == supported.end()) return false;
        const size_t index = static_cast<size_t>(found - supported.begin());
        if (seen[index]) return false;
        seen[index] = true;
        while (offset < tag_attributes.size() &&
               std::isspace(static_cast<unsigned char>(tag_attributes[offset]))) ++offset;
        if (offset == tag_attributes.size() || tag_attributes[offset++] != '=') return false;
        while (offset < tag_attributes.size() &&
               std::isspace(static_cast<unsigned char>(tag_attributes[offset]))) ++offset;
        if (offset == tag_attributes.size() ||
            (tag_attributes[offset] != '\'' && tag_attributes[offset] != '"')) return false;
        const char quote = tag_attributes[offset++];
        const size_t value_end = tag_attributes.find(quote, offset);
        if (value_end == std::string_view::npos) return false;
        offset = value_end + 1;
    }
    return true;
}

bool ExpandLibcdrPathStyle(std::string_view tag, std::string& expanded,
                           std::string& diagnostic) {
    if (!tag.starts_with("<svg:path")) return false;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::string_view style;
    bool has_style = false;
    std::optional<std::string> fill_opacity;
    size_t offset = std::string_view("<svg:path").size();
    while (offset < tag.size()) {
        while (offset < tag.size() && std::isspace(static_cast<unsigned char>(tag[offset]))) ++offset;
        if (offset == tag.size() || tag[offset] == '>' || tag[offset] == '/') break;
        const size_t name_begin = offset;
        while (offset < tag.size() &&
               (std::isalnum(static_cast<unsigned char>(tag[offset])) || tag[offset] == ':' ||
                tag[offset] == '_' || tag[offset] == '-')) ++offset;
        if (name_begin == offset) {
            diagnostic = "libcdr returned a malformed path attribute";
            return false;
        }
        const std::string_view name = tag.substr(name_begin, offset - name_begin);
        while (offset < tag.size() && std::isspace(static_cast<unsigned char>(tag[offset]))) ++offset;
        if (offset == tag.size() || tag[offset++] != '=') {
            diagnostic = "libcdr returned a malformed path attribute";
            return false;
        }
        while (offset < tag.size() && std::isspace(static_cast<unsigned char>(tag[offset]))) ++offset;
        if (offset == tag.size() || (tag[offset] != '\'' && tag[offset] != '"')) {
            diagnostic = "libcdr returned an unquoted path attribute";
            return false;
        }
        const char quote = tag[offset++];
        const size_t value_begin = offset;
        const size_t value_end = tag.find(quote, value_begin);
        if (value_end == std::string_view::npos) {
            diagnostic = "libcdr returned an unterminated path attribute";
            return false;
        }
        const std::string value(tag.substr(value_begin, value_end - value_begin));
        if (name == "style") {
            if (has_style) {
                diagnostic = "libcdr returned duplicate path styles";
                return false;
            }
            has_style = true;
            style = tag.substr(value_begin, value_end - value_begin);
        } else if (name == "fill-opacity") {
            if (fill_opacity) {
                diagnostic = "libcdr returned duplicate fill opacity values";
                return false;
            }
            fill_opacity = value;
        } else {
            attributes.emplace_back(name, value);
        }
        offset = value_end + 1;
    }
    if (!has_style && !fill_opacity) return false;

    std::vector<std::pair<std::string, std::string>> declarations;
    size_t declaration_offset = 0;
    while (declaration_offset <= style.size()) {
        const size_t end = style.find(';', declaration_offset);
        const std::string_view declaration = TrimAscii(style.substr(
            declaration_offset, end == std::string_view::npos ? style.size() - declaration_offset
                                                               : end - declaration_offset));
        if (!declaration.empty()) {
            const size_t colon = declaration.find(':');
            if (colon == std::string_view::npos || declaration.find(':', colon + 1) != std::string_view::npos) {
                diagnostic = "libcdr returned an unsupported path style declaration";
                return false;
            }
            const auto name = TrimAscii(declaration.substr(0, colon));
            const auto value = TrimAscii(declaration.substr(colon + 1));
            if (name.empty() || value.empty()) {
                diagnostic = "libcdr returned an empty path style declaration";
                return false;
            }
            if (name == "fill" || name == "stroke" || name == "stroke-width" || name == "fill-rule") {
                const bool duplicate = std::any_of(attributes.begin(), attributes.end(), [&](const auto& item) {
                    return item.first == name;
                }) || std::any_of(declarations.begin(), declarations.end(), [&](const auto& item) {
                    return item.first == name;
                });
                if (duplicate) {
                    diagnostic = "libcdr returned duplicate inline and presentation path styles";
                    return false;
                }
                declarations.emplace_back(name, value);
            } else if (name == "fill-opacity") {
                if (fill_opacity) {
                    diagnostic = "libcdr returned duplicate inline and presentation fill opacity";
                    return false;
                }
                fill_opacity = std::string(value);
            } else if (name == "stroke-dasharray" && value == "none") {
                // The document model uses a solid stroke; this is the same representation.
            } else if (name == "stroke-linecap" &&
                       (value == "butt" || value == "round" || value == "square")) {
                // PikDraw currently renders round caps. The enclosing fidelity report marks this conversion partial.
            } else if (name == "stroke-linejoin" &&
                       (value == "miter" || value == "round" || value == "bevel")) {
                // PikDraw currently renders round joins. The enclosing fidelity report marks this conversion partial.
            } else {
                diagnostic = "CDR path style property is outside the editable subset: " +
                    std::string(name);
                return false;
            }
        }
        if (end == std::string_view::npos) break;
        declaration_offset = end + 1;
    }

    if (fill_opacity) {
        double opacity = 0.0;
        const auto parsed = std::from_chars(fill_opacity->data(),
            fill_opacity->data() + fill_opacity->size(), opacity);
        if (parsed.ec != std::errc{} || parsed.ptr != fill_opacity->data() + fill_opacity->size() ||
            !std::isfinite(opacity) || opacity < 0.0 || opacity > 1.0) {
            diagnostic = "CDR fill opacity is outside the supported 0 to 1 range";
            return false;
        }
        auto paint = std::find_if(declarations.begin(), declarations.end(),
            [](const auto& item) { return item.first == "fill"; });
        std::string* color = nullptr;
        if (paint != declarations.end()) color = &paint->second;
        else {
            auto attribute = std::find_if(attributes.begin(), attributes.end(),
                [](const auto& item) { return item.first == "fill"; });
            if (attribute != attributes.end()) color = &attribute->second;
            else {
                attributes.emplace_back("fill", "black");
                color = &attributes.back().second;
            }
        }
        if (!ApplyCdrSvgOpacity(*color, opacity)) {
            diagnostic = "CDR fill opacity is attached to a paint outside the editable color subset";
            return false;
        }
    }

    expanded = "<svg:path";
    for (const auto& [name, value] : attributes) {
        expanded.push_back(' ');
        expanded += name;
        expanded += "=\"";
        expanded += value;
        expanded.push_back('"');
    }
    for (const auto& [name, value] : declarations) {
        expanded.push_back(' ');
        expanded += name;
        expanded += "=\"";
        expanded += value;
        expanded.push_back('"');
    }
    const bool self_closing = tag.size() >= 2 && tag[tag.size() - 2] == '/';
    expanded += self_closing ? "/>" : ">";
    return true;
}

bool FlattenLibcdrTextSpans(std::string_view input, std::string& flattened,
                            std::string& diagnostic) {
    flattened.clear();
    flattened.reserve(input.size());
    size_t cursor = 0;
    while (cursor < input.size()) {
        const size_t text_begin = input.find("<svg:text", cursor);
        if (text_begin == std::string_view::npos) {
            flattened.append(input.substr(cursor));
            break;
        }
        const size_t text_open_end = input.find('>', text_begin + 9);
        const size_t text_close = text_open_end == std::string_view::npos
            ? std::string_view::npos : input.find("</svg:text>", text_open_end + 1);
        if (text_open_end == std::string_view::npos || text_close == std::string_view::npos) {
            diagnostic = "libcdr returned malformed SVG text markup";
            return false;
        }
        const std::string_view body = input.substr(text_open_end + 1, text_close - text_open_end - 1);
        const size_t span_begin = body.find("<svg:tspan");
        flattened.append(input.substr(cursor, text_begin - cursor));
        if (span_begin == std::string_view::npos) {
            if (body.find('<') != std::string_view::npos) {
                diagnostic = "libcdr text contains unsupported nested SVG markup";
                return false;
            }
            flattened.append(input.substr(text_begin, text_close + 11 - text_begin));
        } else {
            if (!TrimAscii(body.substr(0, span_begin)).empty()) {
                diagnostic = "CDR text has content outside its editable text spans";
                return false;
            }
            std::string_view span_attributes;
            std::string merged_text;
            size_t span_cursor = span_begin;
            size_t span_count = 0;
            while (span_cursor < body.size()) {
                const size_t current_span = body.find("<svg:tspan", span_cursor);
                if (current_span == std::string_view::npos) {
                    if (!TrimAscii(body.substr(span_cursor)).empty()) {
                        diagnostic = "CDR text has content outside its editable text spans";
                        return false;
                    }
                    break;
                }
                if (!TrimAscii(body.substr(span_cursor, current_span - span_cursor)).empty()) {
                    diagnostic = "CDR text has mixed content outside its editable text spans";
                    return false;
                }
                const size_t span_open_end = body.find('>', current_span + 10);
                const size_t span_close = span_open_end == std::string_view::npos
                    ? std::string_view::npos : body.find("</svg:tspan>", span_open_end + 1);
                if (span_open_end == std::string_view::npos || span_close == std::string_view::npos) {
                    diagnostic = "CDR text contains a malformed span";
                    return false;
                }
                const auto attributes = body.substr(current_span + 10, span_open_end - current_span - 10);
                const auto content = body.substr(span_open_end + 1, span_close - span_open_end - 1);
                if (!IsSupportedCdrTextSpanAttributes(attributes) ||
                    content.find('<') != std::string_view::npos || ++span_count > 256) {
                    diagnostic = "CDR text uses unsupported span styling or nested markup";
                    return false;
                }
                if (span_count == 1) span_attributes = attributes;
                const auto trimmed_content = TrimAscii(content);
                merged_text.append(trimmed_content);
                span_cursor = span_close + 12;
            }
            for (const auto& attribute : {"font-family=", "font-size=", "fill=", "font-weight=", "font-style="}) {
                const std::string_view name(attribute, std::char_traits<char>::length(attribute) - 1);
                if (input.substr(text_begin, text_open_end - text_begin).find(name) != std::string_view::npos &&
                    span_attributes.find(name) != std::string_view::npos) {
                    diagnostic = "CDR text duplicates a parent and span style attribute";
                    return false;
                }
            }
            flattened.append(input.substr(text_begin, text_open_end - text_begin));
            flattened.append(span_attributes);
            flattened.push_back('>');
            flattened.append(merged_text);
            flattened.append("</svg:text>");
        }
        cursor = text_close + 11;
        if (flattened.size() > kMaxCdrSvgBytes) {
            diagnostic = "Flattened CDR text exceeds the package representation size";
            return false;
        }
    }
    return true;
}

bool NormalizeLibcdrSvg(std::span<const uint8_t> source,
                        std::string& normalized,
                        std::string& diagnostic) {
    const std::string_view raw_input(reinterpret_cast<const char*>(source.data()), source.size());
    if (!raw_input.starts_with("<svg:svg") || raw_input.size() > kMaxCdrSvgBytes) {
        diagnostic = "libcdr returned an SVG outside the CDR normalization profile";
        return false;
    }
    std::string flattened_text;
    std::string_view input = raw_input;
    if (raw_input.find("<svg:text") != std::string_view::npos) {
        if (!FlattenLibcdrTextSpans(raw_input, flattened_text, diagnostic)) return false;
        input = flattened_text;
    }
    std::string output;
    output.reserve(input.size() + 256);
    size_t offset = 0;
    size_t group_depth = 0;
    size_t layer_count = 0;
    while (offset < input.size()) {
        if (input[offset] != '<') {
            output.push_back(input[offset++]);
            continue;
        }
        const size_t tag_end = input.find('>', offset + 1);
        if (tag_end == std::string_view::npos) {
            diagnostic = "libcdr returned a truncated SVG tag";
            return false;
        }
        std::string_view tag = input.substr(offset, tag_end + 1 - offset);
        std::string expanded_tag;
        if (tag.starts_with("<svg:path") &&
            (tag.find("style=") != std::string_view::npos ||
             tag.find("fill-opacity=") != std::string_view::npos)) {
            if (!ExpandLibcdrPathStyle(tag, expanded_tag, diagnostic)) {
                if (diagnostic.empty()) diagnostic = "libcdr returned a malformed or unsupported path style";
                return false;
            }
            tag = expanded_tag;
        }
        const bool closing = tag.starts_with("</svg:");
        const bool opening = tag.starts_with("<svg:");
        if (!opening && !closing) {
            output.append(tag);
            offset = tag_end + 1;
            continue;
        }

        const size_t prefix_offset = closing ? 6 : 5;
        const size_t name_end = tag.find_first_of(" />", prefix_offset);
        if (name_end == std::string_view::npos) {
            diagnostic = "libcdr returned an invalid SVG element name";
            return false;
        }
        const std::string_view name = tag.substr(prefix_offset, name_end - prefix_offset);
        output.push_back('<');
        if (closing) output.push_back('/');
        output.append(name);
        output.append(tag.substr(name_end));

        if (opening && name == "svg") {
            const size_t close = output.size() - 1;
            output.insert(close,
                " xmlns:inkscape=\"http://www.inkscape.org/namespaces/inkscape\"");
        } else if (opening && name == "g") {
            const std::string_view attributes = tag.substr(name_end, tag.size() - name_end - 1);
            const bool is_layer = HasCdrLayerId(attributes);
            if (is_layer) {
                if (++layer_count > IPC_MAX_OBJECTS) {
                    diagnostic = "CDR source layer count exceeds the supported profile";
                    return false;
                }
                const size_t close = output.size() - 1;
                std::string label(CdrLayerName(attributes));
                if (label.empty()) label = "Layer " + std::to_string(layer_count);
                output.insert(close, " inkscape:groupmode=\"layer\" inkscape:label=\"" + label + "\"");
            }
            const bool self_closing = tag.size() >= 2 && tag[tag.size() - 2] == '/';
            if (!self_closing) ++group_depth;
        } else if (closing && name == "g") {
            if (group_depth == 0) {
                diagnostic = "libcdr returned unbalanced SVG groups";
                return false;
            }
            --group_depth;
        }
        offset = tag_end + 1;
        if (output.size() > kMaxCdrSvgBytes) {
        diagnostic = "Normalized CDR SVG exceeds the package representation size";
            return false;
        }
    }
    if (group_depth != 0 || layer_count == 0) {
        // No source layer marker is available; the shared SVG importer will create one default layer.
        if (group_depth != 0) {
            diagnostic = "libcdr returned unbalanced SVG group nesting";
            return false;
        }
    }
    normalized.swap(output);
    return true;
}

bool GetCdrPlaceholderPlacement(const ImportPathRecord& path,
                                double& width, double& height,
                                std::array<double, 6>& transform) {
    std::vector<geometry::Point2D> corners;
    corners.reserve(4);
    for (const auto& element : path.elements) {
        if (element.verb == ImportPathVerb::Close) continue;
        if (element.verb != ImportPathVerb::Move && element.verb != ImportPathVerb::Line)
            return false;
        const double px = element.values[0];
        const double py = element.values[1];
        if (!std::isfinite(px) || !std::isfinite(py)) return false;
        const geometry::Point2D point{px, py};
        if (corners.empty() ||
            std::hypot(corners.back().x - px, corners.back().y - py) > 1.0e-9)
            corners.push_back(point);
    }
    if (corners.size() == 5 &&
        std::hypot(corners.front().x - corners.back().x,
                   corners.front().y - corners.back().y) <= 1.0e-9)
        corners.pop_back();
    if (corners.size() != 4) return false;
    // The SVG page transform already flipped Y into PikDraw's y-up coordinates.
    return detail::CdrImagePlacementFromPageCorners(corners, width, height, transform);
}

const char* CdrRasterMediaType(RasterImageFormat format) noexcept {
    switch (format) {
    case RasterImageFormat::Png: return "image/png";
    case RasterImageFormat::Jpeg: return "image/jpeg";
    case RasterImageFormat::Tiff: return "image/tiff";
    case RasterImageFormat::Bmp: return "image/bmp";
    case RasterImageFormat::Unknown: break;
    }
    return "";
}

bool ReplaceCdrImagePlaceholders(const CdrPackage& package,
                                 std::vector<CanonicalImportRecord>& records,
                                 std::string& diagnostic,
                                 bool& dropped_icc_profile,
                                 const CdrMemoryAdmissionCallback& memory_admission) {
    if (package.instances.empty()) return true;
    std::unordered_map<std::string, const CdrImageInstance*> instances_by_source;
    instances_by_source.reserve(package.instances.size());
    for (const auto& instance : package.instances) {
        const std::string source_id = "__pikdraw_cdr_raster_" + std::to_string(instance.instance_id);
        if (!instances_by_source.emplace(source_id, &instance).second) {
            diagnostic = "CDR compact package repeats an embedded-image instance ID";
            return false;
        }
    }

    std::vector<std::optional<RasterImageData>> decoded_assets(package.assets.size());
    std::vector<uint32_t> decoded_pixel_bytes(package.assets.size(), 0);
    std::vector<uint32_t> pixel_data_local_ids(package.assets.size(), 0);
    std::vector<uint32_t> source_data_local_ids(package.assets.size(), 0);
    std::vector<bool> seen_instances(package.instances.size() + 1, false);
    std::vector<CanonicalImportRecord> expanded;
    expanded.reserve(records.size() + package.instances.size() * 2);
    // Track explicit image/chunk lifetimes without imposing a fixed per-format
    // byte ceiling; the import host applies the shared RAM pressure wait.
    auto raster_memory_budget = std::make_shared<raster::RasterMemoryBudget>(
        std::numeric_limits<uint64_t>::max(), [&](uint64_t bytes) {
            if (!memory_admission) return true;
            return memory_admission(bytes, diagnostic);
        });

    const auto decode_asset = [&](uint32_t asset_id) -> const RasterImageData* {
        if (asset_id == 0 || asset_id > package.assets.size()) return nullptr;
        auto& cached = decoded_assets[asset_id - 1];
        if (cached) return &*cached;
        const auto& asset = package.assets[asset_id - 1];
        RasterImageDecodeOptions options;
        options.max_source_bytes = UINT64_MAX;
        options.max_decoded_pixels = UINT32_MAX / 4;
        options.max_decoded_bytes = UINT32_MAX;
        options.max_icc_profile_bytes = 256u * 1024u;
        options.max_frames = 1;
        options.memory_budget = raster_memory_budget;
        auto result = RasterImageDecoder::decode(asset.bytes, options);
        if (!result.success) {
            if (diagnostic.empty())
                diagnostic = "CDR embedded image could not be decoded (" +
                    result.diagnostic_code + "): " + result.message;
            return nullptr;
        }
        const char* actual_media_type = CdrRasterMediaType(result.image.format);
        if (!actual_media_type || asset.media_type != actual_media_type) {
            diagnostic = "CDR embedded image media type does not match its decoded content";
            return nullptr;
        }
        if (result.image.pixels_rgba.size() > UINT32_MAX) {
            diagnostic = "CDR embedded image pixels exceed the native wire address range";
            return nullptr;
        }
        decoded_pixel_bytes[asset_id - 1] = static_cast<uint32_t>(result.image.pixels_rgba.size());
        cached = std::move(result.image);
        return &*cached;
    };

    for (auto& canonical : records) {
        if (canonical.type != ImportRecordType::Path) {
            expanded.push_back(std::move(canonical));
            continue;
        }
        const auto decoded_record = DeserializeImportRecord(canonical.type, canonical.payload);
        const auto* path = decoded_record ? std::get_if<ImportPathRecord>(&*decoded_record) : nullptr;
        if (!path) {
            diagnostic = "CDR placeholder path record failed semantic decoding";
            return false;
        }
        const auto found_instance = instances_by_source.find(path->source_id);
        if (found_instance == instances_by_source.end()) {
            expanded.push_back(std::move(canonical));
            continue;
        }
        const CdrImageInstance& instance = *found_instance->second;
        if (instance.instance_id >= seen_instances.size() || seen_instances[instance.instance_id]) {
            diagnostic = "CDR image placeholder is missing or duplicated in the vector tree";
            return false;
        }
        const RasterImageData* decoded = decode_asset(instance.asset_id);
        if (!decoded) return false;
        if (instance.asset_id == 0 || instance.asset_id > pixel_data_local_ids.size()) {
            diagnostic = "CDR embedded image references an invalid decoded asset";
            return false;
        }
        uint32_t& pixel_data_local_id = pixel_data_local_ids[instance.asset_id - 1];
        const bool owns_pixel_data = pixel_data_local_id == 0;
        if (owns_pixel_data) {
            if (decoded->pixels_rgba.empty()) {
                diagnostic = "CDR embedded image pixels were released before their first placement";
                return false;
            }
            pixel_data_local_id = path->local_id;
        }
        if (package.assets[instance.asset_id - 1].bytes.empty() ||
            package.assets[instance.asset_id - 1].bytes.size() > UINT32_MAX) {
            diagnostic = "CDR embedded image source exceeds the native U32 storage representation";
            return false;
        }
        uint32_t& source_data_local_id = source_data_local_ids[instance.asset_id - 1];
        const bool owns_source_data = source_data_local_id == 0;
        if (owns_source_data) source_data_local_id = path->local_id;

        double width = 0.0, height = 0.0;
        std::array<double, 6> placement_transform{};
        if (!GetCdrPlaceholderPlacement(*path, width, height, placement_transform)) {
            diagnostic = "CDR embedded image placement could not be mapped into page coordinates";
            return false;
        }
        ImportRasterImageRecord image;
        image.local_id = path->local_id;
        image.source_id = path->source_id;
        image.name = "Embedded CDR image " + std::to_string(instance.instance_id);
        image.media_type = CdrRasterMediaType(decoded->format);
        image.width_px = decoded->width_px;
        image.height_px = decoded->height_px;
        image.decoded_pixel_bytes = decoded_pixel_bytes[instance.asset_id - 1];
        image.source_bits_per_pixel = decoded->source_bits_per_pixel;
        image.frame_index = decoded->frame_index;
        image.frame_count = decoded->frame_count;
        image.exif_orientation = decoded->exif_orientation;
        image.orientation_applied = decoded->orientation_applied;
        image.dpi_from_source = decoded->dpi_x != 96.0 || decoded->dpi_y != 96.0;
        image.dpi_x = decoded->dpi_x;
        image.dpi_y = decoded->dpi_y;
        switch (decoded->pixel_color_basis) {
        case RasterPixelColorBasis::AssumedSRgb:
            image.preview_pixel_color_basis = 1;
            break;
        case RasterPixelColorBasis::ConvertedToSRgb:
            image.preview_pixel_color_basis = 2;
            break;
        case RasterPixelColorBasis::EmbeddedProfileUnapplied:
            image.preview_pixel_color_basis = 3;
            break;
        }
        const bool icc_metadata_missing = decoded->has_embedded_icc_profile &&
            (decoded->icc_profile_status != RasterIccProfileStatus::Preserved ||
             decoded->embedded_icc_profile.size() > 256u * 1024u);
        if (!decoded->embedded_icc_profile.empty() && !icc_metadata_missing)
            image.icc_profile = decoded->embedded_icc_profile;
        else if (decoded->has_embedded_icc_profile)
            dropped_icc_profile = true;
        if (decoded->pixel_color_basis == RasterPixelColorBasis::EmbeddedProfileUnapplied)
            dropped_icc_profile = true;
        image.x_mm = 0.0;
        image.y_mm = 0.0;
        image.width_mm = width;
        image.height_mm = height;
        image.has_placement_transform = true;
        image.placement_transform = placement_transform;
        image.pixel_data_local_id = pixel_data_local_id;
        image.source_data_local_id = source_data_local_id;
        image.source_bytes_size = package.assets[instance.asset_id - 1].bytes.size();
        auto image_record = SerializeImportRecord(image);
        if (!image_record && !image.icc_profile.empty()) {
            image.icc_profile.clear();
            dropped_icc_profile = true;
            image_record = SerializeImportRecord(image);
        }
        if (!image_record) {
            diagnostic = "CDR embedded image metadata exceeds the C6 record profile";
            return false;
        }
        expanded.push_back(std::move(*image_record));

        if (owns_source_data) {
            const auto source_bytes = package.assets[instance.asset_id - 1].bytes;
            for (size_t offset = 0; offset < source_bytes.size(); offset += kCdrRasterChunkBytes) {
                const size_t length = (std::min)(static_cast<size_t>(kCdrRasterChunkBytes),
                                                  source_bytes.size() - offset);
                auto chunk_reservation = raster_memory_budget->try_reserve(length);
                if (!chunk_reservation) {
                    diagnostic = "CDR embedded image source chunk exceeds the shared raster memory budget";
                    return false;
                }
                auto chunk_record = SerializeRasterSourceChunk(
                    image.source_data_local_id, offset,
                    image.source_bytes_size, source_bytes.subspan(offset, length));
                if (!chunk_record) {
                    diagnostic = "CDR embedded image source bytes exceed C6 record limits";
                    return false;
                }
                expanded.push_back(std::move(*chunk_record));
            }
        }

        if (owns_pixel_data) {
            const auto pixels = std::span<const uint8_t>(decoded->pixels_rgba);
            for (size_t offset = 0; offset < pixels.size(); offset += kCdrRasterChunkBytes) {
                const size_t length = (std::min)(static_cast<size_t>(kCdrRasterChunkBytes),
                                                  pixels.size() - offset);
                auto chunk_reservation = raster_memory_budget->try_reserve(length);
                if (!chunk_reservation) {
                    diagnostic = "CDR embedded image pixel chunk exceeds the shared raster memory budget";
                    return false;
                }
                auto chunk_record = SerializeRasterImageChunk(
                    image.pixel_data_local_id, static_cast<uint32_t>(offset),
                    image.decoded_pixel_bytes, pixels.subspan(offset, length));
                if (!chunk_record) {
                    diagnostic = "CDR embedded image pixel chunks exceed C6 record limits";
                    return false;
                }
                expanded.push_back(std::move(*chunk_record));
            }
            auto& cached = *decoded_assets[instance.asset_id - 1];
            std::vector<uint8_t>().swap(cached.pixels_rgba);
            cached.pixel_memory_reservation.reset();
        }
        seen_instances[instance.instance_id] = true;
    }
    if (std::any_of(package.instances.begin(), package.instances.end(), [&](const auto& instance) {
            return instance.instance_id >= seen_instances.size() || !seen_instances[instance.instance_id];
        })) {
        diagnostic = "CDR compact package image reference is outside the imported vector tree";
        return false;
    }
    records = std::move(expanded);
    return true;
}

}  // namespace

namespace {
template <typename ReleaseSource>
bool BuildCdrImportRecordsInternal(std::span<const uint8_t> source,
                                   std::vector<CanonicalImportRecord>& records,
                                   std::string& diagnostic,
                                   const CdrMemoryAdmissionCallback& memory_admission,
                                   ReleaseSource&& release_source) {
    records.clear();
    diagnostic.clear();
    if (source.empty()) {
        diagnostic = "CDR source is empty";
        return false;
    }
#ifndef _WIN32
    diagnostic = "CDR vector import currently requires the isolated Windows libcdr runtime";
    return false;
#else
    try {
        CdrBridgeGuard bridge;
        CdrConvertFunction convert = nullptr;
        CdrFreePackageFunction free_package = nullptr;
        if (!LoadCdrBridge(bridge, convert, free_package, diagnostic)) return false;

        char bridge_diagnostic[512]{};
        uint8_t* package_data = nullptr;
        uint64_t package_size = 0;
        const int status = convert(source.data(), source.size(), &package_data, &package_size,
                         bridge_diagnostic, static_cast<uint32_t>(sizeof(bridge_diagnostic)));
        release_source();
        struct PackageGuard {
            uint8_t* data;
            CdrFreePackageFunction release;
            ~PackageGuard() { if (data && release) release(data); }
        } package_guard{package_data, free_package};
        if (status != 1 || !package_data || package_size == 0 || package_size > kMaxCdrPacketBytes) {
            diagnostic = bridge_diagnostic[0] != '\0' ? bridge_diagnostic :
                "libcdr returned an incomplete or oversized vector/image package";
            return false;
        }
        CdrPackage package;
        const bool package_decoded = DecodeCdrPackage(std::span<const uint8_t>(package_data,
                static_cast<size_t>(package_size)), package, diagnostic);
        if (!package_decoded) return false;
        std::string normalized_svg;
        if (!NormalizeLibcdrSvg(package.svg, normalized_svg, diagnostic) ||
            !BuildSvgImportRecords(std::span<const uint8_t>(
                reinterpret_cast<const uint8_t*>(normalized_svg.data()), normalized_svg.size()),
                records, diagnostic)) return false;
        std::string().swap(normalized_svg);
        bool dropped_icc_profile = false;
        if (!ReplaceCdrImagePlaceholders(package, records, diagnostic,
                                         dropped_icc_profile, memory_admission)) {
            records.clear();
            return false;
        }
        if (!AddCdrFidelityIssue(records, diagnostic, dropped_icc_profile)) {
            records.clear();
            return false;
        }
        return true;
    } catch (...) {
        records.clear();
        diagnostic = "CDR vector conversion allocation or bridge error";
        return false;
    }
#endif
}
}  // namespace

bool BuildCdrImportRecords(std::span<const uint8_t> source,
                           std::vector<CanonicalImportRecord>& records,
                           std::string& diagnostic,
                           const CdrMemoryAdmissionCallback& memory_admission) {
    return BuildCdrImportRecordsInternal(source, records, diagnostic,
                                         memory_admission, [] {});
}

bool BuildCdrImportRecordsAndReleaseSource(
    std::vector<uint8_t>& source,
    std::vector<CanonicalImportRecord>& records,
    std::string& diagnostic,
    const CdrMemoryAdmissionCallback& memory_admission) {
    return BuildCdrImportRecordsInternal(source, records, diagnostic,
                                         memory_admission, [&source] {
        std::vector<uint8_t>().swap(source);
    });
}

}  // namespace pikdraw::interop
