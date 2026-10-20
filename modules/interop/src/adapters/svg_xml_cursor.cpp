#include "svg_xml_cursor.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>

namespace pikdraw::interop {
namespace {

constexpr size_t kMaxTextBytes = 256 * 1024;
constexpr size_t kMaxTagBytes = 4 * 1024 * 1024;
constexpr size_t kMaxAttributesPerTag = 64;

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

bool DecodeEntities(std::string_view input, std::string& output,
                    size_t max_bytes = kMaxTextBytes) {
    output.clear();
    if (input.size() > max_bytes) return false;
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
        if (output.size() > max_bytes) return false;
    }
    return IsValidUtf8(output);
}

} // namespace

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

bool XmlCursor::SkipDocumentPreamble() {
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

bool XmlCursor::SkipMisc() {
    while (true) {
        SkipSpace();
        if (!source_.substr(position_).starts_with("<!--")) return true;
        const size_t end = source_.find("-->", position_ + 4);
        if (end == std::string_view::npos || end - position_ > kMaxTagBytes) return false;
        if (source_.substr(position_ + 4, end - position_ - 4).find("--") != std::string_view::npos) return false;
        position_ = end + 3;
    }
}

bool XmlCursor::ParseTag(XmlTag& tag) {
    if (position_ >= source_.size() || source_[position_++] != '<') return false;
    const size_t tag_start = position_ - 1;
    const auto within_tag_limit = [&] { return position_ - tag_start <= kMaxTagBytes; };
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
        return within_tag_limit();
    }

    while (position_ < source_.size()) {
        SkipSpace();
        if (source_.substr(position_).starts_with("/>")) {
            position_ += 2;
            tag.self_closing = true;
            return within_tag_limit();
        }
        if (source_[position_] == '>') { ++position_; return within_tag_limit(); }
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
        if (!DecodeEntities(source_.substr(value_start, end - value_start), value, kMaxTagBytes) ||
            !tag.attributes.emplace(std::move(name), std::move(value)).second) return false;
        position_ = end + 1;
        if (!within_tag_limit()) return false;
    }
    return false;
}

bool XmlCursor::ReadText(std::string& text) {
    const size_t end = source_.find('<', position_);
    if (end == std::string_view::npos || end - position_ > kMaxTextBytes) return false;
    if (!DecodeEntities(source_.substr(position_, end - position_), text)) return false;
    position_ = end;
    return true;
}

bool XmlCursor::AtEnd() const noexcept { return position_ == source_.size(); }
bool XmlCursor::AtTagStart() const noexcept { return position_ < source_.size() && source_[position_] == '<'; }
std::string_view XmlCursor::Remaining() const noexcept { return source_.substr(position_); }

void XmlCursor::SkipSpace() {
    while (position_ < source_.size() && std::isspace(static_cast<unsigned char>(source_[position_])) != 0) ++position_;
}

} // namespace pikdraw::interop
