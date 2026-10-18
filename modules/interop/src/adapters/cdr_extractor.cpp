#include "pikdraw/interop/adapters/cdr_extractor.hpp"
#include "tinf.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <string_view>

namespace pikdraw::interop {
namespace {

uint16_t read_u16_le(const uint8_t* p) noexcept {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

uint32_t read_u32_le(const uint8_t* p) noexcept {
    return static_cast<uint32_t>(p[0] |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24));
}

uint32_t read_u32_be(const uint8_t* p) noexcept {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

bool decompress_entry(const uint8_t* src, size_t compressed_len,
                      uint16_t method, size_t uncompressed_len,
                      std::vector<uint8_t>& out) {
    if (uncompressed_len == 0 || uncompressed_len > 128ULL * 1024ULL * 1024ULL)
        return false;
    try {
        out.resize(uncompressed_len);
    } catch (...) {
        return false;
    }
    if (method == 0) {
        if (compressed_len < uncompressed_len) return false;
        std::memcpy(out.data(), src, uncompressed_len);
        return true;
    }
    if (method == 8) {
        unsigned int dest_len = static_cast<unsigned int>(uncompressed_len);
        static bool tinf_init_done = false;
        if (!tinf_init_done) {
            tinf_init();
            tinf_init_done = true;
        }
        const int status = tinf_uncompress(out.data(), &dest_len, src,
                                           static_cast<unsigned int>(compressed_len));
        if (status != TINF_OK || dest_len != uncompressed_len) {
            out.clear();
            return false;
        }
        return true;
    }
    return false;
}

void parse_page_dimensions_from_xml(std::string_view xml,
                                    double& out_w_mm, double& out_h_mm) {
    // Look for <cdrinfo:PageDimensions>215.9 x 279.4 mm</cdrinfo:PageDimensions>
    constexpr std::string_view kDimTag = "<cdrinfo:PageDimensions>";
    auto pos = xml.find(kDimTag);
    if (pos != std::string_view::npos) {
        pos += kDimTag.size();
        const auto end_pos = xml.find("</cdrinfo:PageDimensions>", pos);
        if (end_pos != std::string_view::npos) {
            const auto val_str = xml.substr(pos, end_pos - pos);
            const auto x_pos = val_str.find('x');
            if (x_pos != std::string_view::npos) {
                try {
                    const double w = std::stod(std::string(val_str.substr(0, x_pos)));
                    auto rest = val_str.substr(x_pos + 1);
                    const auto mm_pos = rest.find("mm");
                    if (mm_pos != std::string_view::npos) rest = rest.substr(0, mm_pos);
                    const double h = std::stod(std::string(rest));
                    if (w > 0.0 && h > 0.0) {
                        out_w_mm = w;
                        out_h_mm = h;
                        return;
                    }
                } catch (...) {}
            }
        }
    }

    // Fallback: look for <cdrinfo:PageWidth>2159000</cdrinfo:PageWidth> (0.1 micron units)
    constexpr std::string_view kWTag = "<cdrinfo:PageWidth>";
    constexpr std::string_view kHTag = "<cdrinfo:PageHeight>";
    const auto w_pos = xml.find(kWTag);
    const auto h_pos = xml.find(kHTag);
    if (w_pos != std::string_view::npos && h_pos != std::string_view::npos) {
        const auto w_end = xml.find("</cdrinfo:PageWidth>", w_pos);
        const auto h_end = xml.find("</cdrinfo:PageHeight>", h_pos);
        if (w_end != std::string_view::npos && h_end != std::string_view::npos) {
            try {
                const double w_val = std::stod(std::string(xml.substr(w_pos + kWTag.size(), w_end - (w_pos + kWTag.size()))));
                const double h_val = std::stod(std::string(xml.substr(h_pos + kHTag.size(), h_end - (h_pos + kHTag.size()))));
                if (w_val > 0.0 && h_val > 0.0) {
                    out_w_mm = w_val / 10000.0;
                    out_h_mm = h_val / 10000.0;
                }
            } catch (...) {}
        }
    }
}

} // namespace

bool CdrExtractor::is_cdr_zip_container(std::span<const uint8_t> bytes) noexcept {
    if (bytes.size() < 4) return false;
    return bytes[0] == 0x50 && bytes[1] == 0x4B && bytes[2] == 0x03 && bytes[3] == 0x04;
}

CdrExtractionResult CdrExtractor::extract_preview(std::span<const uint8_t> cdr_bytes) {
    CdrExtractionResult result;
    if (cdr_bytes.size() < 22 || !is_cdr_zip_container(cdr_bytes)) {
        result.error_message = "The file is not a valid CorelDRAW ZIP container (PK header missing).";
        return result;
    }

    // Locate End of Central Directory record (PK\x05\x06) from end of file
    const size_t search_len = (std::min)(cdr_bytes.size(), static_cast<size_t>(65536 + 22));
    const size_t search_start = cdr_bytes.size() - search_len;
    size_t eocd_pos = std::string_view::npos;
    for (size_t i = cdr_bytes.size() - 22; i >= search_start; --i) {
        if (cdr_bytes[i] == 0x50 && cdr_bytes[i + 1] == 0x4B &&
            cdr_bytes[i + 2] == 0x05 && cdr_bytes[i + 3] == 0x06) {
            eocd_pos = i;
            break;
        }
        if (i == search_start) break;
    }

    if (eocd_pos == std::string_view::npos || eocd_pos + 22 > cdr_bytes.size()) {
        result.error_message = "Corrupt CorelDRAW container: End of Central Directory record not found.";
        return result;
    }

    const uint32_t cd_size = read_u32_le(cdr_bytes.data() + eocd_pos + 12);
    const uint32_t cd_offset = read_u32_le(cdr_bytes.data() + eocd_pos + 16);
    if (static_cast<uint64_t>(cd_offset) + cd_size > cdr_bytes.size()) {
        result.error_message = "Corrupt CorelDRAW container: Central Directory exceeds file bounds.";
        return result;
    }

    std::vector<uint8_t> metadata_bytes;
    std::vector<uint8_t> fallback_thumbnail_png;

    size_t pos = cd_offset;
    const size_t cd_end = static_cast<size_t>(cd_offset) + cd_size;
    while (pos + 46 <= cd_end) {
        if (cdr_bytes[pos] != 0x50 || cdr_bytes[pos + 1] != 0x4B ||
            cdr_bytes[pos + 2] != 0x01 || cdr_bytes[pos + 3] != 0x02) {
            break;
        }

        const uint16_t method = read_u16_le(cdr_bytes.data() + pos + 10);
        const uint32_t csize = read_u32_le(cdr_bytes.data() + pos + 20);
        const uint32_t usize = read_u32_le(cdr_bytes.data() + pos + 24);
        const uint16_t nlen = read_u16_le(cdr_bytes.data() + pos + 28);
        const uint16_t elen = read_u16_le(cdr_bytes.data() + pos + 30);
        const uint16_t clen = read_u16_le(cdr_bytes.data() + pos + 32);
        const uint32_t lhdr_off = read_u32_le(cdr_bytes.data() + pos + 42);

        if (pos + 46 + nlen > cd_end) break;
        std::string_view name(reinterpret_cast<const char*>(cdr_bytes.data() + pos + 46), nlen);
        pos += 46 + nlen + elen + clen;

        const bool is_thumb = name == "previews/thumbnail.png";
        const bool is_page1 = name == "previews/page1.png";
        const bool is_meta = name == "META-INF/metadata.xml";

        if (!is_thumb && !is_page1 && !is_meta) continue;

        // Read from local file header
        if (static_cast<uint64_t>(lhdr_off) + 30 > cdr_bytes.size()) continue;
        if (cdr_bytes[lhdr_off] != 0x50 || cdr_bytes[lhdr_off + 1] != 0x4B ||
            cdr_bytes[lhdr_off + 2] != 0x03 || cdr_bytes[lhdr_off + 3] != 0x04) {
            continue;
        }

        const uint16_t local_nlen = read_u16_le(cdr_bytes.data() + lhdr_off + 26);
        const uint16_t local_elen = read_u16_le(cdr_bytes.data() + lhdr_off + 28);
        const size_t data_start = static_cast<size_t>(lhdr_off) + 30 + local_nlen + local_elen;
        if (data_start + csize > cdr_bytes.size()) continue;

        if (is_thumb) {
            decompress_entry(cdr_bytes.data() + data_start, csize, method, usize, result.thumbnail_png);
        } else if (is_page1 && result.thumbnail_png.empty()) {
            decompress_entry(cdr_bytes.data() + data_start, csize, method, usize, fallback_thumbnail_png);
        } else if (is_meta) {
            decompress_entry(cdr_bytes.data() + data_start, csize, method, usize, metadata_bytes);
        }
    }

    if (result.thumbnail_png.empty() && !fallback_thumbnail_png.empty()) {
        result.thumbnail_png = std::move(fallback_thumbnail_png);
    }

    if (result.thumbnail_png.empty()) {
        result.error_message = "No preview thumbnail found in the CorelDRAW container.";
        return result;
    }

    // Try parsing page dimensions from metadata XML
    double meta_w = 0.0, meta_h = 0.0;
    if (!metadata_bytes.empty()) {
        std::string_view xml(reinterpret_cast<const char*>(metadata_bytes.data()), metadata_bytes.size());
        parse_page_dimensions_from_xml(xml, meta_w, meta_h);
    }

    if (meta_w > 0.0 && meta_h > 0.0) {
        result.page_width_mm = meta_w;
        result.page_height_mm = meta_h;
    } else {
        // Fallback: derive aspect ratio from PNG IHDR
        if (result.thumbnail_png.size() >= 24) {
            const uint32_t w_px = read_u32_be(result.thumbnail_png.data() + 16);
            const uint32_t h_px = read_u32_be(result.thumbnail_png.data() + 20);
            if (w_px > 0 && h_px > 0) {
                if (w_px >= h_px) {
                    result.page_width_mm = 297.0;
                    result.page_height_mm = 297.0 * static_cast<double>(h_px) / static_cast<double>(w_px);
                } else {
                    result.page_height_mm = 297.0;
                    result.page_width_mm = 297.0 * static_cast<double>(w_px) / static_cast<double>(h_px);
                }
            }
        }
    }

    result.success = true;
    return result;
}

} // namespace pikdraw::interop
