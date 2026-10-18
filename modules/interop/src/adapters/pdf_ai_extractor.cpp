#include "pikdraw/interop/adapters/pdf_ai_extractor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace pikdraw::interop {
namespace {

bool parse_box_coordinates(std::string_view text, std::string_view box_name,
                           double& out_w_mm, double& out_h_mm) {
    auto pos = text.find(box_name);
    while (pos != std::string_view::npos) {
        size_t cur = pos + box_name.size();
        while (cur < text.size() && (text[cur] == ' ' || text[cur] == '\t' ||
                                     text[cur] == '\r' || text[cur] == '\n')) {
            ++cur;
        }
        if (cur < text.size() && text[cur] == '[') {
            ++cur;
            std::array<double, 4> coords{};
            int found_count = 0;
            while (cur < text.size() && found_count < 4) {
                while (cur < text.size() && (text[cur] == ' ' || text[cur] == '\t' ||
                                             text[cur] == '\r' || text[cur] == '\n')) {
                    ++cur;
                }
                if (cur >= text.size() || text[cur] == ']') break;
                const std::string number_tail(text.substr(cur));
                const char* start_ptr = number_tail.c_str();
                char* end_ptr = nullptr;
                const double val = std::strtod(start_ptr, &end_ptr);
                if (end_ptr == start_ptr) break;
                coords[static_cast<size_t>(found_count++)] = val;
                cur += static_cast<size_t>(end_ptr - start_ptr);
            }
            if (found_count == 4) {
                const double w_pt = std::abs(coords[2] - coords[0]);
                const double h_pt = std::abs(coords[3] - coords[1]);
                if (std::isfinite(w_pt) && std::isfinite(h_pt) && w_pt > 0.01 && h_pt > 0.01) {
                    out_w_mm = w_pt * 25.4 / 72.0;
                    out_h_mm = h_pt * 25.4 / 72.0;
                    return true;
                }
            }
        }
        pos = text.find(box_name, pos + box_name.size());
    }
    return false;
}

std::vector<uint8_t> decode_base64_xmp(std::string_view str) {
    std::string clean;
    clean.reserve(str.size());
    size_t i = 0;
    while (i < str.size()) {
        if (str[i] == '&' && i + 3 < str.size()) {
            if (str.substr(i, 5) == "&#xA;" || str.substr(i, 5) == "&#10;") {
                i += 5;
                continue;
            }
            if (str.substr(i, 5) == "&#xD;" || str.substr(i, 5) == "&#13;") {
                i += 5;
                continue;
            }
        }
        const char ch = str[i++];
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '+' || ch == '/' || ch == '=') {
            clean.push_back(ch);
        }
    }

    std::vector<uint8_t> out;
    out.reserve(clean.size() * 3 / 4);
    uint32_t val = 0;
    int bits = -8;
    for (char c : clean) {
        if (c == '=') break;
        int d = -1;
        if (c >= 'A' && c <= 'Z') d = c - 'A';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
        else if (c >= '0' && c <= '9') d = c - '0' + 52;
        else if (c == '+') d = 62;
        else if (c == '/') d = 63;
        if (d < 0) continue;
        val = (val << 6) | static_cast<uint32_t>(d);
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<uint8_t>((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

std::vector<uint8_t> extract_xmp_thumbnail(std::string_view text) {
    constexpr std::string_view kOpenTag = "<xmpGImg:image>";
    constexpr std::string_view kCloseTag = "</xmpGImg:image>";
    const auto open_pos = text.find(kOpenTag);
    if (open_pos == std::string_view::npos) return {};
    const auto content_start = open_pos + kOpenTag.size();
    const auto close_pos = text.find(kCloseTag, content_start);
    if (close_pos == std::string_view::npos) return {};

    const std::string_view b64_view = text.substr(content_start, close_pos - content_start);
    auto decoded = decode_base64_xmp(b64_view);
    if (decoded.size() >= 4) {
        if ((decoded[0] == 0xFF && decoded[1] == 0xD8 && decoded[2] == 0xFF) ||
            (decoded[0] == 0x89 && decoded[1] == 'P' && decoded[2] == 'N' && decoded[3] == 'G')) {
            return decoded;
        }
    }
    return {};
}

} // namespace

bool PdfAiExtractor::is_pdf_or_ai_container(std::span<const uint8_t> bytes) noexcept {
    if (bytes.size() < 5) return false;
    const size_t check_len = (std::min)(bytes.size(), static_cast<size_t>(1024));
    const std::string_view head(reinterpret_cast<const char*>(bytes.data()), check_len);
    return head.find("%PDF-") != std::string_view::npos;
}

PdfAiExtractionResult PdfAiExtractor::extract_preview(std::span<const uint8_t> bytes) {
    PdfAiExtractionResult result;
    if (!is_pdf_or_ai_container(bytes)) {
        result.error_message = "The file is not a valid PDF or Adobe Illustrator container (%PDF- header missing).";
        return result;
    }

    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());

    const bool has_ai_markers = text.find("Illustrator") != std::string_view::npos ||
                                text.find("AIPrivateData") != std::string_view::npos ||
                                text.find("xmpGImg:image") != std::string_view::npos ||
                                text.find("Adobe Illustrator") != std::string_view::npos;
    result.is_ai = has_ai_markers;
    result.is_pdf = !has_ai_markers;

    double page_w = 0.0, page_h = 0.0;
    if (parse_box_coordinates(text, "/CropBox", page_w, page_h) ||
        parse_box_coordinates(text, "/MediaBox", page_w, page_h)) {
        result.page_width_mm = page_w;
        result.page_height_mm = page_h;
    } else {
        result.page_width_mm = 297.0;
        result.page_height_mm = 210.0;
    }

    // Only a document thumbnail represents a source preview. An arbitrary PDF
    // image XObject may be a logo, mask or background rather than the whole page.
    result.thumbnail_png = extract_xmp_thumbnail(text);
    result.success = !result.thumbnail_png.empty();
    if (!result.success) {
        result.error_message = "No embedded document thumbnail is available; a PDF renderer is required to display this PDF/AI file.";
    }
    return result;
}

} // namespace pikdraw::interop
