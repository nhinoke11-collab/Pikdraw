#include "pikdraw/interop/adapters/pdf_ai_extractor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace pikdraw::interop {
namespace {

void append_be32(std::vector<uint8_t>& buf, uint32_t val) {
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
}

uint32_t calc_crc32(std::span<const uint8_t> data) {
    uint32_t crc = 0xFFFFFFFFu;
    for (uint8_t b : data) {
        crc ^= b;
        for (int i = 0; i < 8; ++i) {
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
        }
    }
    return ~crc;
}

void append_png_chunk(std::vector<uint8_t>& png, const char type[4],
                      std::span<const uint8_t> payload) {
    append_be32(png, static_cast<uint32_t>(payload.size()));
    const size_t type_start = png.size();
    for (int i = 0; i < 4; ++i) png.push_back(static_cast<uint8_t>(type[i]));
    png.insert(png.end(), payload.begin(), payload.end());
    const uint32_t crc = calc_crc32(
        std::span<const uint8_t>(png.data() + type_start, payload.size() + 4));
    append_be32(png, crc);
}

std::vector<uint8_t> create_placeholder_png(double width_mm, double height_mm) {
    uint32_t w = 256;
    uint32_t h = 256;
    if (width_mm >= height_mm && width_mm > 0.0) {
        w = 256;
        h = (std::max)(32u, static_cast<uint32_t>(std::round(256.0 * height_mm / width_mm)));
    } else if (height_mm > 0.0) {
        h = 256;
        w = (std::max)(32u, static_cast<uint32_t>(std::round(256.0 * width_mm / height_mm)));
    }

    std::vector<uint8_t> png{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    append_be32(ihdr, w);
    append_be32(ihdr, h);
    ihdr.push_back(8);
    ihdr.push_back(6);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    append_png_chunk(png, "IHDR", ihdr);

    const size_t row_stride = 1 + static_cast<size_t>(w) * 4;
    std::vector<uint8_t> raw_scanlines(row_stride * h, 0);
    for (uint32_t y = 0; y < h; ++y) {
        uint8_t* row = raw_scanlines.data() + y * row_stride;
        row[0] = 0;
        uint8_t* pixels = row + 1;
        const bool border_y = (y < 2 || y >= h - 2);
        for (uint32_t x = 0; x < w; ++x) {
            const bool is_border = border_y || (x < 2 || x >= w - 2);
            if (is_border) {
                pixels[x * 4 + 0] = 190;
                pixels[x * 4 + 1] = 195;
                pixels[x * 4 + 2] = 205;
                pixels[x * 4 + 3] = 255;
            } else {
                pixels[x * 4 + 0] = 252;
                pixels[x * 4 + 1] = 252;
                pixels[x * 4 + 2] = 254;
                pixels[x * 4 + 3] = 255;
            }
        }
    }

    std::vector<uint8_t> zlib;
    zlib.push_back(0x78);
    zlib.push_back(0x01);
    size_t offset = 0;
    while (offset < raw_scanlines.size()) {
        const size_t chunk_len = (std::min)(static_cast<size_t>(32768), raw_scanlines.size() - offset);
        const bool is_final = (offset + chunk_len == raw_scanlines.size());
        zlib.push_back(is_final ? 0x01 : 0x00);
        const auto len16 = static_cast<uint16_t>(chunk_len);
        zlib.push_back(static_cast<uint8_t>(len16 & 0xFF));
        zlib.push_back(static_cast<uint8_t>((len16 >> 8) & 0xFF));
        const auto nlen16 = static_cast<uint16_t>(~len16);
        zlib.push_back(static_cast<uint8_t>(nlen16 & 0xFF));
        zlib.push_back(static_cast<uint8_t>((nlen16 >> 8) & 0xFF));
        zlib.insert(zlib.end(), raw_scanlines.begin() + static_cast<std::ptrdiff_t>(offset),
                    raw_scanlines.begin() + static_cast<std::ptrdiff_t>(offset + chunk_len));
        offset += chunk_len;
    }
    uint32_t a = 1, b = 0;
    for (uint8_t byte : raw_scanlines) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    append_be32(zlib, (b << 16) | a);

    append_png_chunk(png, "IDAT", zlib);
    append_png_chunk(png, "IEND", {});
    return png;
}

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
                const char* start_ptr = text.data() + cur;
                char* end_ptr = nullptr;
                const double val = std::strtod(start_ptr, &end_ptr);
                if (end_ptr == start_ptr) break;
                coords[static_cast<size_t>(found_count++)] = val;
                cur += static_cast<size_t>(end_ptr - start_ptr);
            }
            if (found_count == 4) {
                const double w_pt = std::abs(coords[2] - coords[0]);
                const double h_pt = std::abs(coords[3] - coords[1]);
                if (w_pt > 0.01 && h_pt > 0.01) {
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

std::vector<uint8_t> extract_dct_image_stream(std::span<const uint8_t> bytes, std::string_view text) {
    constexpr std::string_view kDct = "/DCTDecode";
    auto dct_pos = text.find(kDct);
    while (dct_pos != std::string_view::npos) {
        auto stream_pos = text.find("stream", dct_pos);
        if (stream_pos != std::string_view::npos && stream_pos - dct_pos < 512) {
            size_t data_start = stream_pos + 6;
            if (data_start < bytes.size() && bytes[data_start] == '\r') ++data_start;
            if (data_start < bytes.size() && bytes[data_start] == '\n') ++data_start;

            if (data_start + 4 <= bytes.size() &&
                bytes[data_start] == 0xFF && bytes[data_start + 1] == 0xD8 && bytes[data_start + 2] == 0xFF) {
                auto endstream_pos = text.find("endstream", data_start);
                if (endstream_pos != std::string_view::npos && endstream_pos > data_start) {
                    size_t data_end = endstream_pos;
                    while (data_end > data_start &&
                           (bytes[data_end - 1] == '\r' || bytes[data_end - 1] == '\n' ||
                            bytes[data_end - 1] == ' '  || bytes[data_end - 1] == '\t')) {
                        --data_end;
                    }
                    if (data_end > data_start + 4) {
                        return std::vector<uint8_t>(bytes.data() + data_start, bytes.data() + data_end);
                    }
                }
            }
        }
        dct_pos = text.find(kDct, dct_pos + kDct.size());
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

    auto xmp_thumb = extract_xmp_thumbnail(text);
    if (!xmp_thumb.empty() && xmp_thumb.size() >= 8 &&
        xmp_thumb[0] == 0x89 && xmp_thumb[1] == 'P' && xmp_thumb[2] == 'N' && xmp_thumb[3] == 'G') {
        result.thumbnail_png = std::move(xmp_thumb);
    } else {
        auto dct_thumb = extract_dct_image_stream(bytes, text);
        if (!dct_thumb.empty() && dct_thumb.size() >= 8 &&
            dct_thumb[0] == 0x89 && dct_thumb[1] == 'P' && dct_thumb[2] == 'N' && dct_thumb[3] == 'G') {
            result.thumbnail_png = std::move(dct_thumb);
        } else {
            result.thumbnail_png = create_placeholder_png(result.page_width_mm, result.page_height_mm);
        }
    }

    result.success = !result.thumbnail_png.empty();
    if (!result.success) {
        result.error_message = "Failed to extract or generate preview raster for PDF/AI document.";
    }
    return result;
}

} // namespace pikdraw::interop
