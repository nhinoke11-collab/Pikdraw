#include "pikdraw/interop/adapters/svg_importer.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cctype>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace pikdraw::interop {
namespace {

constexpr double kPxToMm = 25.4 / 96.0;
constexpr size_t kMaxTotalPathElements = 100000;
constexpr size_t kMaxTextBytes = 256 * 1024;
constexpr size_t kMaxTagBytes = 64 * 1024;
constexpr size_t kMaxAttributesPerTag = 64;

struct XmlTag {
    std::string name;
    std::unordered_map<std::string, std::string> attributes;
    bool closing{false};
    bool self_closing{false};
};

bool IsNameStart(char c) {
    const auto value = static_cast<unsigned char>(c);
    return std::isalpha(value) != 0 || c == '_' || c == ':';
}

bool IsNameChar(char c) {
    const auto value = static_cast<unsigned char>(c);
    return std::isalnum(value) != 0 || c == '_' || c == ':' || c == '-' || c == '.';
}

void AppendUtf8(uint32_t codepoint, std::string& output) {
    if (codepoint <= 0x7f) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
        output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
        output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
        output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
}

bool IsValidUtf8(std::string_view input) {
    size_t index = 0;
    while (index < input.size()) {
        const uint8_t lead = static_cast<uint8_t>(input[index++]);
        if (lead < 0x80) {
            if (lead < 0x20 && lead != '\t' && lead != '\n' && lead != '\r') return false;
            continue;
        }
        uint32_t codepoint = 0;
        size_t continuation = 0;
        if ((lead & 0xe0) == 0xc0) { codepoint = lead & 0x1f; continuation = 1; }
        else if ((lead & 0xf0) == 0xe0) { codepoint = lead & 0x0f; continuation = 2; }
        else if ((lead & 0xf8) == 0xf0) { codepoint = lead & 0x07; continuation = 3; }
        else return false;
        if (continuation > input.size() - index) return false;
        for (size_t i = 0; i < continuation; ++i) {
            const uint8_t next = static_cast<uint8_t>(input[index++]);
            if ((next & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (next & 0x3f);
        }
        if ((continuation == 1 && codepoint < 0x80) ||
            (continuation == 2 && codepoint < 0x800) ||
            (continuation == 3 && codepoint < 0x10000) ||
            codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
    }
    return true;
}

bool DecodeEntities(std::string_view input, std::string& output) {
    output.clear();
    if (input.size() > kMaxTextBytes) return false;
    for (size_t index = 0; index < input.size();) {
        if (input[index] != '&') {
            if (input[index] == '\0') return false;
            output.push_back(input[index++]);
            continue;
        }
        const size_t end = input.find(';', index + 1);
        if (end == std::string_view::npos || end - index > 16) return false;
        const auto entity = input.substr(index + 1, end - index - 1);
        if (entity == "amp") output.push_back('&');
        else if (entity == "lt") output.push_back('<');
        else if (entity == "gt") output.push_back('>');
        else if (entity == "quot") output.push_back('"');
        else if (entity == "apos") output.push_back('\'');
        else if (!entity.empty() && entity.front() == '#') {
            uint32_t value = 0;
            const bool hex = entity.size() > 2 && (entity[1] == 'x' || entity[1] == 'X');
            const auto digits = entity.substr(hex ? 2 : 1);
            const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), value, hex ? 16 : 10);
            if (digits.empty() || parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size() ||
                value == 0 || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
            AppendUtf8(value, output);
        } else return false;
        index = end + 1;
        if (output.size() > kMaxTextBytes) return false;
    }
    return IsValidUtf8(output);
}

class XmlCursor final {
public:
    explicit XmlCursor(std::string_view source) : source_(source) {}

    bool SkipDocumentPreamble() {
        SkipSpace();
        if (source_.substr(position_).starts_with("<?xml")) {
            if (position_ + 5 >= source_.size() ||
                std::isspace(static_cast<unsigned char>(source_[position_ + 5])) == 0) return false;
            const size_t end = source_.find("?>", position_ + 5);
            if (end == std::string_view::npos || end - position_ > 256) return false;
            const auto declaration = source_.substr(position_, end + 2 - position_);
            std::string lower(declaration);
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            const size_t encoding = lower.find("encoding");
            if (encoding != std::string::npos) {
                if (encoding == 0 || std::isspace(static_cast<unsigned char>(lower[encoding - 1])) == 0) return false;
                size_t cursor = encoding + 8;
                while (cursor < lower.size() && std::isspace(static_cast<unsigned char>(lower[cursor])) != 0) ++cursor;
                if (cursor >= lower.size() || lower[cursor++] != '=') return false;
                while (cursor < lower.size() && std::isspace(static_cast<unsigned char>(lower[cursor])) != 0) ++cursor;
                if (cursor >= lower.size() || (lower[cursor] != '\'' && lower[cursor] != '"')) return false;
                const char quote = lower[cursor++];
                const size_t value_end = lower.find(quote, cursor);
                if (value_end == std::string::npos || lower.substr(cursor, value_end - cursor) != "utf-8") return false;
            }
            position_ = end + 2;
        }
        return SkipMisc();
    }

    bool SkipMisc() {
        while (true) {
            SkipSpace();
            if (!source_.substr(position_).starts_with("<!--")) return true;
            const size_t end = source_.find("-->", position_ + 4);
            if (end == std::string_view::npos || end - position_ > kMaxTagBytes) return false;
            if (source_.substr(position_ + 4, end - position_ - 4).find("--") != std::string_view::npos) return false;
            position_ = end + 3;
        }
    }

    bool ParseTag(XmlTag& tag) {
        if (position_ >= source_.size() || source_[position_++] != '<') return false;
        tag = {};
        if (position_ < source_.size() && source_[position_] == '/') {
            tag.closing = true;
            ++position_;
        }
        if (position_ >= source_.size() || !IsNameStart(source_[position_])) return false;
        const size_t name_start = position_++;
        while (position_ < source_.size() && IsNameChar(source_[position_])) ++position_;
        tag.name.assign(source_.substr(name_start, position_ - name_start));
        if (tag.closing) {
            SkipSpace();
            if (position_ >= source_.size() || source_[position_++] != '>') return false;
            return true;
        }

        while (position_ < source_.size()) {
            SkipSpace();
            if (source_.substr(position_).starts_with("/>")) {
                position_ += 2;
                tag.self_closing = true;
                return true;
            }
            if (source_[position_] == '>') { ++position_; return true; }
            if (!IsNameStart(source_[position_])) return false;
            const size_t attr_start = position_++;
            while (position_ < source_.size() && IsNameChar(source_[position_])) ++position_;
            std::string name(source_.substr(attr_start, position_ - attr_start));
            SkipSpace();
            if (position_ >= source_.size() || source_[position_++] != '=') return false;
            SkipSpace();
            if (position_ >= source_.size() || (source_[position_] != '\'' && source_[position_] != '"')) return false;
            const char quote = source_[position_++];
            const size_t value_start = position_;
            const size_t end = source_.find(quote, position_);
            if (end == std::string_view::npos || end - value_start > kMaxTagBytes) return false;
            if (source_.substr(value_start, end - value_start).find('<') != std::string_view::npos ||
                tag.attributes.size() >= kMaxAttributesPerTag) return false;
            std::string value;
            if (!DecodeEntities(source_.substr(value_start, end - value_start), value) ||
                !tag.attributes.emplace(std::move(name), std::move(value)).second) return false;
            position_ = end + 1;
        }
        return false;
    }

    bool ReadText(std::string& text) {
        const size_t end = source_.find('<', position_);
        if (end == std::string_view::npos || end - position_ > kMaxTextBytes) return false;
        if (!DecodeEntities(source_.substr(position_, end - position_), text)) return false;
        position_ = end;
        return true;
    }

    [[nodiscard]] bool AtEnd() const noexcept { return position_ == source_.size(); }
    [[nodiscard]] bool AtTagStart() const noexcept { return position_ < source_.size() && source_[position_] == '<'; }
    [[nodiscard]] std::string_view Remaining() const noexcept { return source_.substr(position_); }
    void SkipSpace() {
        while (position_ < source_.size() && std::isspace(static_cast<unsigned char>(source_[position_])) != 0) ++position_;
    }

private:
    std::string_view source_;
    size_t position_{0};
};

bool ParseNumber(std::string_view input, double& value, size_t& consumed) {
    if (input.empty()) return false;
    const auto result = std::from_chars(input.data(), input.data() + input.size(), value, std::chars_format::general);
    if (result.ec != std::errc{} || result.ptr == input.data() || !std::isfinite(value)) return false;
    consumed = static_cast<size_t>(result.ptr - input.data());
    return true;
}

struct SvgLength { double value{0}; std::string_view unit; };

bool ParseLength(std::string_view source, SvgLength& output) {
    while (!source.empty() && std::isspace(static_cast<unsigned char>(source.front())) != 0) source.remove_prefix(1);
    while (!source.empty() && std::isspace(static_cast<unsigned char>(source.back())) != 0) source.remove_suffix(1);
    size_t consumed = 0;
    if (!ParseNumber(source, output.value, consumed)) return false;
    output.unit = source.substr(consumed);
    return output.unit.empty() || output.unit == "px" || output.unit == "mm" || output.unit == "cm" ||
           output.unit == "in" || output.unit == "pt" || output.unit == "pc";
}

double LengthToMm(const SvgLength& length) {
    if (length.unit.empty() || length.unit == "px") return length.value * kPxToMm;
    if (length.unit == "mm") return length.value;
    if (length.unit == "cm") return length.value * 10.0;
    if (length.unit == "in") return length.value * 25.4;
    if (length.unit == "pt") return length.value * 25.4 / 72.0;
    return length.value * 25.4 / 6.0;
}

bool LengthToUserUnits(const SvgLength& length, double& value) {
    if (length.unit.empty() || length.unit == "px") value = length.value;
    else value = LengthToMm(length) / kPxToMm;
    return std::isfinite(value);
}

class NumberCursor final {
public:
    explicit NumberCursor(std::string_view source) : source_(source) {}
    bool Next(double& value) {
        SkipSeparators();
        if (position_ >= source_.size() || IsCommand(source_[position_])) return false;
        size_t consumed = 0;
        if (!ParseNumber(source_.substr(position_), value, consumed)) return false;
        position_ += consumed;
        return true;
    }
    bool MoreNumbers() {
        SkipSeparators();
        return position_ < source_.size() && !IsCommand(source_[position_]);
    }
    bool Done() { SkipSeparators(); return position_ == source_.size(); }
    char TakeCommand() {
        SkipSeparators();
        return position_ < source_.size() && IsCommand(source_[position_]) ? source_[position_++] : 0;
    }
    bool IsCommandAhead() {
        SkipSeparators();
        return position_ < source_.size() && IsCommand(source_[position_]);
    }

private:
    static bool IsCommand(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }
    void SkipSeparators() {
        while (position_ < source_.size() &&
               (std::isspace(static_cast<unsigned char>(source_[position_])) != 0 || source_[position_] == ',')) ++position_;
    }
    std::string_view source_;
    size_t position_{0};
};

enum class SvgPathVerb : uint8_t { Move, Line, Quad, Cubic, Arc, Close };

bool AddPathElement(geometry::Path2D& path, const geometry::Transform2D& transform,
                    SvgPathVerb verb, const std::array<double, 9>& values, uint8_t flags,
                    size_t maximum) {
    if (path.size() >= maximum) return false;
    const auto point = [&](size_t index) { return transform.map_point({values[index], values[index + 1]}); };
    switch (verb) {
        case SvgPathVerb::Move: { const auto p = point(0); path.move_to(p); return true; }
        case SvgPathVerb::Line: { const auto p = point(0); path.line_to(p); return true; }
        case SvgPathVerb::Quad: {
            const auto control = point(2); const auto p = point(0); path.quad_to(control.x, control.y, p.x, p.y); return true;
        }
        case SvgPathVerb::Cubic: {
            const auto first = point(2); const auto second = point(4); const auto p = point(0);
            path.cubic_to(first.x, first.y, second.x, second.y, p.x, p.y); return true;
        }
        case SvgPathVerb::Close: path.close(); return true;
        case SvgPathVerb::Arc: (void)flags; return false;
    }
    return false;
}

bool ParsePath(std::string_view data, const geometry::Transform2D& transform,
               geometry::Path2D& output, size_t maximum) {
    NumberCursor cursor(data);
    char command = 0;
    char previous = 0;
    double x = 0, y = 0, start_x = 0, start_y = 0, previous_cx = 0, previous_cy = 0;
    bool has_move = false;
    size_t elements = 0;
    while (!cursor.Done()) {
        if (cursor.IsCommandAhead()) command = cursor.TakeCommand();
        else if (command == 0) return false;
        const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(command)));
        const bool relative = std::islower(static_cast<unsigned char>(command)) != 0;
        if (upper == 'Z') {
            if (!has_move || !AddPathElement(output, transform, SvgPathVerb::Close, {}, 0, maximum)) return false;
            x = start_x; y = start_y; previous = 'Z'; command = 0;
            continue;
        }
        if (upper == 'A' || (upper != 'M' && upper != 'L' && upper != 'H' && upper != 'V' &&
                             upper != 'C' && upper != 'S' && upper != 'Q' && upper != 'T')) return false;
        size_t sets = 0;
        while (cursor.MoreNumbers()) {
            std::array<double, 9> values{};
            SvgPathVerb verb = SvgPathVerb::Line;
            double nx = x, ny = y;
            if (upper == 'M' || upper == 'L' || upper == 'T') {
                if (!cursor.Next(nx) || !cursor.Next(ny)) return false;
                if (relative) { nx += x; ny += y; }
                values[0] = nx; values[1] = ny;
                if (upper == 'M' && sets == 0) {
                    verb = SvgPathVerb::Move; start_x = nx; start_y = ny; has_move = true;
                } else if (upper == 'T') {
                    const bool reflect = previous == 'Q' || previous == 'T';
                    const double cx = reflect ? 2 * x - previous_cx : x;
                    const double cy = reflect ? 2 * y - previous_cy : y;
                    values[2] = cx; values[3] = cy; values[0] = nx; values[1] = ny;
                    verb = SvgPathVerb::Quad; previous_cx = cx; previous_cy = cy;
                } else { verb = upper == 'M' && sets == 0 ? SvgPathVerb::Move : SvgPathVerb::Line; }
            } else if (upper == 'H') {
                if (!cursor.Next(nx)) return false;
                if (relative) nx += x;
                values[0] = nx; values[1] = y; verb = SvgPathVerb::Line;
            } else if (upper == 'V') {
                if (!cursor.Next(ny)) return false;
                if (relative) ny += y;
                values[0] = x; values[1] = ny; verb = SvgPathVerb::Line;
            } else if (upper == 'Q' || upper == 'T') {
                double cx = 0, cy = 0;
                if (!cursor.Next(cx) || !cursor.Next(cy) || !cursor.Next(nx) || !cursor.Next(ny)) return false;
                if (relative) { cx += x; cy += y; nx += x; ny += y; }
                values[0] = nx; values[1] = ny; values[2] = cx; values[3] = cy;
                verb = SvgPathVerb::Quad; previous_cx = cx; previous_cy = cy;
            } else if (upper == 'C' || upper == 'S') {
                double c1x = x, c1y = y, c2x = 0, c2y = 0;
                if (upper == 'C') {
                    if (!cursor.Next(c1x) || !cursor.Next(c1y) || !cursor.Next(c2x) || !cursor.Next(c2y) ||
                        !cursor.Next(nx) || !cursor.Next(ny)) return false;
                    if (relative) { c1x += x; c1y += y; c2x += x; c2y += y; nx += x; ny += y; }
                } else {
                    const bool reflect = previous == 'C' || previous == 'S';
                    c1x = reflect ? 2 * x - previous_cx : x;
                    c1y = reflect ? 2 * y - previous_cy : y;
                    if (!cursor.Next(c2x) || !cursor.Next(c2y) || !cursor.Next(nx) || !cursor.Next(ny)) return false;
                    if (relative) { c2x += x; c2y += y; nx += x; ny += y; }
                }
                values[0] = nx; values[1] = ny; values[2] = c1x; values[3] = c1y;
                values[4] = c2x; values[5] = c2y; verb = SvgPathVerb::Cubic;
                previous_cx = c2x; previous_cy = c2y;
            }
            for (double value : values) if (!std::isfinite(value) || std::abs(value) > 1.0e9) return false;
            if (!has_move && verb != SvgPathVerb::Move) return false;
            if (!AddPathElement(output, transform, verb, values, 0, maximum)) return false;
            x = nx; y = ny; previous = upper; ++sets; ++elements;
            if (upper == 'M' && sets == 1) command = relative ? 'l' : 'L';
        }
        if (sets == 0) return false;
    }
    return has_move && elements > 0;
}

bool AttributesAllowed(const XmlTag& tag, std::initializer_list<std::string_view> allowed) {
    for (const auto& [name, value] : tag.attributes) {
        (void)value;
        if (std::find(allowed.begin(), allowed.end(), name) == allowed.end()) return false;
    }
    return true;
}

bool RootAttributesAllowed(const XmlTag& tag) {
    for (const auto& [name, value] : tag.attributes) {
        if (name.starts_with("xmlns:") && name.size() > 6 && name.size() <= 70 &&
            value.size() <= 512 && IsValidUtf8(value)) continue;
        if (name != "xmlns" && name != "id" && name != "version" && name != "width" &&
            name != "height" && name != "viewBox" && name != "preserveAspectRatio") return false;
    }
    return true;
}

std::string Attribute(const XmlTag& tag, std::string_view name, std::string fallback = {}) {
    const auto found = tag.attributes.find(std::string(name));
    return found == tag.attributes.end() ? std::move(fallback) : found->second;
}

bool ParseUserLength(std::string_view source, double fallback, double& value, bool positive = false) {
    if (source.empty()) { value = fallback; return !positive || value > 0; }
    SvgLength length;
    return ParseLength(source, length) && LengthToUserUnits(length, value) && std::isfinite(value) &&
           (!positive || value > 0) && std::abs(value) <= 1.0e9;
}

bool ParseViewBox(std::string_view source, std::array<double, 4>& values) {
    NumberCursor cursor(source);
    for (double& value : values) if (!cursor.Next(value) || !std::isfinite(value)) return false;
    return cursor.Done() && values[2] > 0 && values[3] > 0 &&
           std::abs(values[0]) <= 1.0e9 && std::abs(values[1]) <= 1.0e9 &&
           values[2] <= 1.0e9 && values[3] <= 1.0e9;
}

bool ParseColor(std::string_view source, color::ColorValue& value) {
    std::string normalized(source);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    if (normalized == "black") value = color::RGBColor{0, 0, 0, 1};
    else if (normalized == "white") value = color::RGBColor{1, 1, 1, 1};
    else if (normalized == "red") value = color::RGBColor{1, 0, 0, 1};
    else if (normalized == "green") value = color::RGBColor{0, 0.5019607843137255, 0, 1};
    else if (normalized == "blue") value = color::RGBColor{0, 0, 1, 1};
    else if (normalized == "transparent") value = color::RGBColor{0, 0, 0, 0};
    else if (normalized.size() == 4 && normalized.front() == '#') {
        const int r = hex(normalized[1]), g = hex(normalized[2]), b = hex(normalized[3]);
        if (r < 0 || g < 0 || b < 0) return false;
        value = color::RGBColor{r / 15.0, g / 15.0, b / 15.0, 1};
    } else if ((normalized.size() == 7 || normalized.size() == 9) && normalized.front() == '#') {
        int channels[4]{0, 0, 0, 255};
        for (size_t i = 0; i < (normalized.size() == 9 ? 4u : 3u); ++i) {
            const int high = hex(normalized[1 + i * 2]), low = hex(normalized[2 + i * 2]);
            if (high < 0 || low < 0) return false;
            channels[i] = high * 16 + low;
        }
        value = color::RGBColor{channels[0] / 255.0, channels[1] / 255.0,
                                channels[2] / 255.0, channels[3] / 255.0};
    } else return false;
    return value.is_valid();
}

bool ParsePaint(const XmlTag& tag, std::string_view key, std::string_view default_value,
                color::ColorValue& value, bool& enabled) {
    const std::string paint = Attribute(tag, key, std::string(default_value));
    enabled = paint != "none";
    return !enabled || ParseColor(paint, value);
}

geometry::Path2D TransformPath(const geometry::Path2D& source, const geometry::Transform2D& transform) {
    geometry::Path2D result;
    result.set_winding_rule(source.winding_rule());
    for (const auto& element : source.elements()) {
        const auto endpoint = transform.map_point(element.pt);
        if (element.verb == geometry::PathVerb::MoveTo) result.move_to(endpoint);
        else if (element.verb == geometry::PathVerb::LineTo) result.line_to(endpoint);
        else if (element.verb == geometry::PathVerb::QuadTo) {
            const auto control = transform.map_point(element.ctrl1);
            result.quad_to(control.x, control.y, endpoint.x, endpoint.y);
        } else if (element.verb == geometry::PathVerb::CubicTo) {
            const auto first = transform.map_point(element.ctrl1);
            const auto second = transform.map_point(element.ctrl2);
            result.cubic_to(first.x, first.y, second.x, second.y, endpoint.x, endpoint.y);
        } else if (element.verb == geometry::PathVerb::Close) result.close();
        else return {};
    }
    return result;
}

SvgImportResult Failure(std::string diagnostic, std::string feature = "unsupported_svg_subset") {
    SvgImportResult result;
    result.page = document::Page::make_default();
    result.error_message = std::move(diagnostic);
    result.report.add_issue(FidelitySeverity::Unsupported, "svg", std::move(feature), result.error_message);
    return result;
}

bool ParseSourceId(const XmlTag& tag, uint32_t ordinal, std::string& id,
                   std::unordered_set<std::string>& used) {
    id = Attribute(tag, "id");
    if (id.empty()) id = "svg-element-" + std::to_string(ordinal);
    if (id.size() > 128 || !IsValidUtf8(id) || !used.insert(id).second) return false;
    return true;
}

} // namespace

namespace {

SvgImportResult ImportSvgString(const std::string& input) {
    if (input.empty() || input.size() > SvgImporter::kMaxSourceBytes) return Failure("SVG source exceeds the bounded input profile", "resource_limit");
    std::string_view source(input);
    if (source.starts_with("\xef\xbb\xbf")) source.remove_prefix(3);
    if (!IsValidUtf8(source)) return Failure("SVG source is not valid UTF-8", "invalid_encoding");

    XmlCursor cursor(source);
    if (!cursor.SkipDocumentPreamble()) return Failure("SVG XML preamble is malformed or prohibited", "xml_security");
    XmlTag root;
    if (!cursor.ParseTag(root) || root.closing || root.name != "svg" || !RootAttributesAllowed(root)) {
        return Failure("SVG root or root attributes are outside the supported profile", "unsupported_root");
    }
    const std::string namespace_uri = Attribute(root, "xmlns");
    if (!namespace_uri.empty() && namespace_uri != "http://www.w3.org/2000/svg") {
        return Failure("SVG namespace is not supported", "unsupported_namespace");
    }
    const std::string version = Attribute(root, "version", "1.1");
    if (version != "1.0" && version != "1.1") return Failure("Only static SVG 1.0/1.1 is in the supported profile", "unsupported_version");

    SvgLength width_length, height_length;
    if (!ParseLength(Attribute(root, "width"), width_length) || !ParseLength(Attribute(root, "height"), height_length)) {
        return Failure("SVG requires explicit width and height with supported units", "unsupported_units");
    }
    const double width_mm = LengthToMm(width_length), height_mm = LengthToMm(height_length);
    if (!std::isfinite(width_mm) || !std::isfinite(height_mm) || width_mm <= 0 || height_mm <= 0 ||
        width_mm > SvgImporter::kMaxPageMillimeters || height_mm > SvgImporter::kMaxPageMillimeters) {
        return Failure("SVG page dimensions exceed the supported range", "resource_limit");
    }

    geometry::Transform2D root_transform{kPxToMm, 0, 0, kPxToMm, 0, 0};
    const std::string view_box_text = Attribute(root, "viewBox");
    if (!view_box_text.empty()) {
        std::array<double, 4> view_box{};
        if (!ParseViewBox(view_box_text, view_box)) return Failure("SVG viewBox is malformed or outside the supported range", "invalid_viewbox");
        const double sx = width_mm / view_box[2], sy = height_mm / view_box[3];
        const std::string aspect = Attribute(root, "preserveAspectRatio", "xMidYMid meet");
        if (aspect == "none") {
            if (std::abs(sx - sy) > 1e-12) return Failure("Non-uniform viewBox scaling is not supported", "unsupported_viewbox_transform");
            root_transform = {sx, 0, 0, sy, -view_box[0] * sx, -view_box[1] * sy};
        } else if (aspect == "xMidYMid meet" || aspect == "xMidYMid") {
            const double scale = std::min(sx, sy);
            root_transform = {scale, 0, 0, scale,
                (width_mm - view_box[2] * scale) / 2.0 - view_box[0] * scale,
                (height_mm - view_box[3] * scale) / 2.0 - view_box[1] * scale};
        } else return Failure("Only default xMidYMid meet or uniform none viewBox mapping is supported", "unsupported_viewbox_transform");
    } else if (Attribute(root, "preserveAspectRatio") != "") {
        return Failure("preserveAspectRatio requires viewBox", "unsupported_viewbox_transform");
    }

    SvgImportResult result;
    result.page = document::Page::make_default();
    result.page.width_mm = width_mm;
    result.page.height_mm = height_mm;
    std::unordered_set<std::string> source_ids;
    size_t total_path_elements = 0;
    uint32_t ordinal = 0;
    bool root_closed = root.self_closing;
    while (!root_closed) {
        if (!cursor.SkipMisc()) return Failure("Malformed XML comment or prohibited declaration", "xml_security");
        if (cursor.AtEnd() || !cursor.AtTagStart()) return Failure("Unexpected XML content in SVG document", "malformed_xml");
        XmlTag tag;
        if (!cursor.ParseTag(tag)) return Failure("Malformed SVG tag or attribute", "malformed_xml");
        if (tag.closing) {
            if (tag.name != "svg") return Failure("Unexpected closing SVG tag", "malformed_xml");
            root_closed = true;
            break;
        }
        if (++ordinal > SvgImporter::kMaxObjects) return Failure("SVG object count exceeds the supported profile", "resource_limit");
        std::string source_id;
        if (!ParseSourceId(tag, ordinal, source_id, source_ids)) return Failure("SVG element IDs are invalid or duplicated", "invalid_source_id");
        bool element_close_already_consumed = false;

        if (tag.name == "rect") {
            if (!AttributesAllowed(tag, {"id", "x", "y", "width", "height", "fill", "stroke", "stroke-width"})) {
                return Failure("SVG rectangle uses unsupported attributes", "unsupported_rectangle_feature");
            }
            double x = 0, y = 0, w = 0, h = 0;
            if (!ParseUserLength(Attribute(tag, "x"), 0, x) || !ParseUserLength(Attribute(tag, "y"), 0, y) ||
                !ParseUserLength(Attribute(tag, "width"), 0, w, true) || !ParseUserLength(Attribute(tag, "height"), 0, h, true)) {
                return Failure("SVG rectangle dimensions are malformed", "invalid_geometry");
            }
            geometry::Path2D source_path = geometry::Path2D::make_rect(x, y, w, h);
            document::PathObject object;
            object.id = document::generate_object_id();
            object.name = "Imported Rectangle";
            object.path = TransformPath(source_path, root_transform);
            if (object.path.empty() || !ParsePaint(tag, "fill", "black", object.fill, object.has_fill) ||
                !ParsePaint(tag, "stroke", "none", object.stroke, object.has_stroke)) {
                return Failure("SVG rectangle paint or geometry is unsupported", "unsupported_paint");
            }
            SvgLength stroke;
            if (!ParseLength(Attribute(tag, "stroke-width", "1"), stroke) || !LengthToUserUnits(stroke, object.stroke_width_mm)) {
                return Failure("SVG stroke width is malformed", "unsupported_units");
            }
            object.stroke_width_mm *= std::abs(root_transform.a);
            if (!std::isfinite(object.stroke_width_mm) || object.stroke_width_mm < 0) return Failure("SVG stroke width is invalid", "invalid_geometry");
            total_path_elements += object.path.size();
            result.objects.emplace_back(std::move(object));
            result.source_ids.push_back(std::move(source_id));
        } else if (tag.name == "path") {
            if (!AttributesAllowed(tag, {"id", "d", "fill", "stroke", "stroke-width", "fill-rule"})) {
                return Failure("SVG path uses unsupported attributes", "unsupported_path_feature");
            }
            document::PathObject object;
            object.id = document::generate_object_id();
            object.name = "Imported Path";
            if (!ParsePath(Attribute(tag, "d"), root_transform, object.path, SvgImporter::kMaxPathElements)) {
                return Failure("SVG path data is malformed or uses an unsupported command", "unsupported_path_command");
            }
            const std::string rule = Attribute(tag, "fill-rule", "nonzero");
            if (rule == "evenodd") object.path.set_winding_rule(geometry::WindingRule::EvenOdd);
            else if (rule != "nonzero") return Failure("SVG fill-rule is unsupported", "unsupported_fill_rule");
            if (!ParsePaint(tag, "fill", "black", object.fill, object.has_fill) ||
                !ParsePaint(tag, "stroke", "none", object.stroke, object.has_stroke)) {
                return Failure("SVG path paint is unsupported", "unsupported_paint");
            }
            SvgLength stroke;
            if (!ParseLength(Attribute(tag, "stroke-width", "1"), stroke) || !LengthToUserUnits(stroke, object.stroke_width_mm)) {
                return Failure("SVG stroke width is malformed", "unsupported_units");
            }
            object.stroke_width_mm *= std::abs(root_transform.a);
            total_path_elements += object.path.size();
            result.objects.emplace_back(std::move(object));
            result.source_ids.push_back(std::move(source_id));
        } else if (tag.name == "text") {
            if (!AttributesAllowed(tag, {"id", "x", "y", "fill", "font-family", "font-size", "font-weight", "font-style"}) || tag.self_closing) {
                return Failure("SVG text uses unsupported attributes or empty-element syntax", "unsupported_text_feature");
            }
            double x = 0, y = 0, font_size = 16;
            if (!ParseUserLength(Attribute(tag, "x"), 0, x) || !ParseUserLength(Attribute(tag, "y"), 0, y) ||
                !ParseUserLength(Attribute(tag, "font-size", "16"), 16, font_size, true)) {
                return Failure("SVG text metrics are malformed", "unsupported_units");
            }
            std::string text;
            if (!cursor.ReadText(text)) return Failure("SVG text is malformed or too large", "resource_limit");
            XmlTag close;
            if (!cursor.ParseTag(close) || !close.closing || close.name != "text") return Failure("Nested SVG text markup is not supported", "unsupported_text_markup");
            element_close_already_consumed = true;
            document::TextObject object;
            object.id = document::generate_object_id();
            object.name = "Imported Text";
            object.text = std::move(text);
            const auto position = root_transform.map_point({x, y});
            object.position = position;
            object.font.family_name = Attribute(tag, "font-family", "sans-serif");
            if (object.font.family_name.size() > 256) return Failure("SVG font family is too long", "resource_limit");
            const std::string weight = Attribute(tag, "font-weight", "400");
            if (weight == "normal") object.font.weight = 400;
            else if (weight == "bold") object.font.weight = 700;
            else {
                unsigned value = 0;
                const auto parsed = std::from_chars(weight.data(), weight.data() + weight.size(), value);
                if (parsed.ec != std::errc{} || parsed.ptr != weight.data() + weight.size() || value < 100 || value > 900 || value % 100 != 0) {
                    return Failure("SVG font weight is unsupported", "unsupported_font_weight");
                }
                object.font.weight = static_cast<uint16_t>(value);
            }
            const std::string style = Attribute(tag, "font-style", "normal");
            if (style != "normal" && style != "italic") return Failure("SVG font style is unsupported", "unsupported_font_style");
            object.font.italic = style == "italic";
            object.font.size_pt = font_size * std::abs(root_transform.a) * 72.0 / 25.4;
            bool has_fill = true;
            if (!ParsePaint(tag, "fill", "black", object.fill, has_fill)) return Failure("SVG text paint is unsupported", "unsupported_paint");
            if (!has_fill) object.fill = color::RGBColor{0, 0, 0, 0};
            result.report.add_issue(FidelitySeverity::Warning, source_id, "font_resolution",
                                    "Font family is retained; rendered appearance is not verified");
            result.objects.emplace_back(std::move(object));
            result.source_ids.push_back(std::move(source_id));
        } else {
            return Failure("SVG contains an element outside the supported flat subset", "unsupported_element");
        }

        if (total_path_elements > kMaxTotalPathElements) return Failure("SVG total path complexity exceeds the supported profile", "resource_limit");
        if (!tag.self_closing && !element_close_already_consumed) {
            if (!cursor.SkipMisc()) return Failure("Malformed XML inside SVG element", "malformed_xml");
            XmlTag close;
            if (!cursor.ParseTag(close) || !close.closing || close.name != tag.name) {
                return Failure("SVG element content or closing tag is unsupported", "malformed_xml");
            }
        }
    }
    if (!cursor.SkipMisc() || !cursor.AtEnd()) return Failure("Trailing XML content is not supported", "malformed_xml");
    result.success = true;
    return result;
}

} // namespace

SvgImportResult SvgImporter::import_from_string(const std::string& input) {
    try {
        return ImportSvgString(input);
    } catch (...) {
        return Failure("SVG parser allocation or codec failure", "resource_limit");
    }
}

SvgImportResult SvgImporter::import_from_file(const std::string& path) {
    try {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return Failure("Cannot read selected SVG file", "source_read_failed");
    const auto end = input.tellg();
    if (end <= 0 || static_cast<uint64_t>(end) > kMaxSourceBytes) return Failure("SVG source exceeds the bounded input profile", "resource_limit");
    std::string bytes(static_cast<size_t>(end), '\0');
    input.seekg(0, std::ios::beg);
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) return Failure("Cannot read selected SVG file", "source_read_failed");
    return import_from_string(bytes);
    } catch (...) {
        return Failure("SVG file read allocation failure", "resource_limit");
    }
}

} // namespace pikdraw::interop
