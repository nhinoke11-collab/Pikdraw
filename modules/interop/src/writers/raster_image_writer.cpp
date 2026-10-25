#include "pikdraw/interop/writers/raster_image_writer.hpp"

#include "pikdraw/color/preview/icc_preview.hpp"
#include "pikdraw/color/preview/profile_color_transform.hpp"
#include "pikdraw/color/profiles/profile_resource.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#endif

namespace pikdraw::interop {
namespace {

enum class WicRasterLayout : uint8_t { Rgba8, Rgba16Le };

bool has_rgb_icc_signature(std::span<const uint8_t> profile) noexcept {
    return profile.size() >= 128 && profile[16] == 'R' && profile[17] == 'G' &&
        profile[18] == 'B' && profile[19] == ' ';
}

bool has_cmyk_icc_signature(std::span<const uint8_t> profile) noexcept {
    return profile.size() >= 128 && profile[16] == 'C' && profile[17] == 'M' &&
        profile[18] == 'Y' && profile[19] == 'K';
}

bool same_bytes(std::span<const uint8_t> left,
                std::span<const uint8_t> right) noexcept {
    return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin());
}

bool valid_options(const RasterImageWriteOptions& options) noexcept {
    if (!std::isfinite(options.dpi_x) || !std::isfinite(options.dpi_y) ||
        options.dpi_x <= 0.0 || options.dpi_y <= 0.0 ||
        options.dpi_x > std::numeric_limits<float>::max() ||
        options.dpi_y > std::numeric_limits<float>::max() ||
        options.source_icc_profile.size() > 1u * 1024u * 1024u ||
        options.destination_icc_profile.size() > 1u * 1024u * 1024u ||
        options.embedded_icc_profile.size() > 1u * 1024u * 1024u)
        return false;
    if (options.format == RasterImageOutputFormat::Tiff) return true;
    return options.format == RasterImageOutputFormat::Jpeg &&
        std::isfinite(options.jpeg_quality) && options.jpeg_quality >= 0.1f &&
        options.jpeg_quality <= 1.0f;
}

#ifdef _WIN32
template <typename T>
class ComOwner final {
public:
    ComOwner() = default;
    ~ComOwner() { reset(); }
    ComOwner(const ComOwner&) = delete;
    ComOwner& operator=(const ComOwner&) = delete;
    T** put() noexcept { reset(); return &value_; }
    T* get() const noexcept { return value_; }
    T* operator->() const noexcept { return value_; }
    explicit operator bool() const noexcept { return value_ != nullptr; }
    void reset(T* value = nullptr) noexcept {
        if (value_) value_->Release();
        value_ = value;
    }
private:
    T* value_{nullptr};
};

class ComApartment final {
public:
    ComApartment() noexcept : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComApartment() {
        if (result_ == S_OK || result_ == S_FALSE) CoUninitialize();
    }
    [[nodiscard]] bool usable() const noexcept {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }
private:
    HRESULT result_{E_FAIL};
};

bool write_encoder_options(IPropertyBag2* bag,
                           const RasterImageWriteOptions& options) noexcept {
    if (!bag) return false;
    PROPBAG2 property{};
    VARIANT value;
    VariantInit(&value);
    if (options.format == RasterImageOutputFormat::Tiff) {
        property.pstrName = const_cast<LPOLESTR>(L"TiffCompressionMethod");
        value.vt = VT_UI1;
        value.bVal = static_cast<BYTE>(WICTiffCompressionLZW);
    } else {
        PROPBAG2 jpeg_properties[2]{};
        VARIANT jpeg_values[2];
        VariantInit(&jpeg_values[0]);
        VariantInit(&jpeg_values[1]);
        jpeg_properties[0].pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        jpeg_properties[1].pstrName = const_cast<LPOLESTR>(L"JpegYCrCbSubsampling");
        jpeg_values[0].vt = VT_R4;
        jpeg_values[0].fltVal = options.jpeg_quality;
        jpeg_values[1].vt = VT_UI1;
        jpeg_values[1].bVal = static_cast<BYTE>(WICJpegYCrCbSubsampling444);
        const HRESULT result = bag->Write(2, jpeg_properties, jpeg_values);
        VariantClear(&jpeg_values[0]);
        VariantClear(&jpeg_values[1]);
        return SUCCEEDED(result);
    }
    const HRESULT result = bag->Write(1, &property, &value);
    VariantClear(&value);
    return SUCCEEDED(result);
}

bool write_pixels(IWICBitmapFrameEncode* frame,
                  const color::ProfileColorTransform* color_transform,
                  bool identity_transform,
                  const RasterImageWriteOptions& options,
                  uint32_t width_px, uint32_t height_px,
                  std::span<const uint8_t> rgba, WicRasterLayout layout,
                  std::string& diagnostic) {
    const bool rgba16le = layout == WicRasterLayout::Rgba16Le;
    if (rgba16le &&
        (options.format != RasterImageOutputFormat::Tiff ||
         options.convert_pixels_to_destination_profile)) {
        diagnostic = "16-bit raster output requires lossless TIFF with already-converted samples";
        return false;
    }
    const uint64_t stride64 = static_cast<uint64_t>(width_px) *
        (rgba16le ? 8u :
            options.format == RasterImageOutputFormat::Tiff ? 4u : 3u);
    if (stride64 == 0 || stride64 > UINT32_MAX) {
        diagnostic = "Raster row stride exceeds the WIC encoder range";
        return false;
    }
    const uint32_t stride = static_cast<uint32_t>(stride64);
    const uint64_t input_stride64 = static_cast<uint64_t>(width_px) * 4u;
    if (options.convert_pixels_to_destination_profile &&
        (input_stride64 == 0 || input_stride64 > UINT32_MAX)) {
        diagnostic = "ICC conversion input row exceeds the WIC range";
        return false;
    }
    const uint32_t input_stride = options.convert_pixels_to_destination_profile
        ? static_cast<uint32_t>(input_stride64) : 0u;
    const uint32_t work_stride = (std::max)(stride, input_stride);
    constexpr uint32_t kPreferredStripBytes = 4u * 1024u * 1024u;
    const uint32_t rows_per_chunk = (std::max)(1u,
        (std::min)((std::min)(64u, kPreferredStripBytes / work_stride),
                   UINT32_MAX / work_stride));
    std::chrono::milliseconds fallback_waited{0};
    for (uint32_t y = 0; y < height_px;) {
        const uint32_t rows = (std::min)(rows_per_chunk, height_px - y);
        const uint64_t output_bytes = static_cast<uint64_t>(stride) * rows;
        const uint64_t rgba_chunk_bytes = options.convert_pixels_to_destination_profile
            ? static_cast<uint64_t>(input_stride) * rows : 0u;
        uint64_t scratch_bytes = output_bytes + rgba_chunk_bytes * 2u;
        constexpr uint64_t kCmmScratchBytes = 2u * 1024u * 1024u;
        if (options.convert_pixels_to_destination_profile) {
            if (scratch_bytes > UINT64_MAX - kCmmScratchBytes) {
                diagnostic = "Raster color conversion scratch exceeds the WIC encoder range";
                return false;
            }
            scratch_bytes += kCmmScratchBytes;
        }
        if (output_bytes > SIZE_MAX || output_bytes > UINT32_MAX ||
            rgba_chunk_bytes > SIZE_MAX || rgba_chunk_bytes > UINT32_MAX ||
            scratch_bytes < output_bytes) {
            diagnostic = "Raster strip exceeds the WIC encoder range";
            return false;
        }
        const auto headroom_ready = [&]() {
            if (options.wait_for_memory_headroom) {
                try {
                    return options.wait_for_memory_headroom(scratch_bytes);
                } catch (...) {
                    diagnostic = "Raster output RAM waiter failed";
                    return false;
                }
            }
            constexpr auto kMaximumWait = std::chrono::minutes(5);
            constexpr auto kMaximumWaitMs =
                std::chrono::duration_cast<std::chrono::milliseconds>(kMaximumWait);
            for (;;) {
                MEMORYSTATUSEX memory{};
                memory.dwLength = sizeof(memory);
                if (!GlobalMemoryStatusEx(&memory) || memory.ullTotalPhys == 0 ||
                    memory.ullAvailPhys > memory.ullTotalPhys) {
                    diagnostic = "Could not verify system RAM headroom before raster encoding";
                    return false;
                }
                const uint64_t threshold = (memory.ullTotalPhys / 100u) * 95u +
                    ((memory.ullTotalPhys % 100u) * 95u) / 100u;
                const uint64_t used = memory.ullTotalPhys - memory.ullAvailPhys;
                if (used < threshold && scratch_bytes <= threshold - used) return true;
                if (fallback_waited >= kMaximumWaitMs) {
                    diagnostic = "Raster output stopped after five minutes waiting for system RAM below 95%";
                    return false;
                }
                const auto pause = (std::min)(
                    std::chrono::milliseconds(100), kMaximumWaitMs - fallback_waited);
                const auto started = std::chrono::steady_clock::now();
                std::this_thread::sleep_for(pause);
                const auto actual = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started);
                fallback_waited += (std::min)(kMaximumWaitMs,
                    (std::max)(pause, actual));
            }
        };
        if (!headroom_ready()) {
            if (diagnostic.empty())
                diagnostic = "Raster output was cancelled or timed out waiting for system RAM below 95%";
            return false;
        }
        std::vector<uint8_t> strip;
        std::vector<uint8_t> input_strip;
        std::vector<uint8_t> converted_strip;
        try {
            strip.resize(static_cast<size_t>(output_bytes));
            if (options.convert_pixels_to_destination_profile) {
                input_strip.resize(static_cast<size_t>(rgba_chunk_bytes));
                converted_strip.resize(static_cast<size_t>(rgba_chunk_bytes));
            }
        } catch (...) {
            diagnostic = "Could not allocate a temporary raster output strip";
            return false;
        }
        const uint8_t* pixel_data = rgba.data();
        if (options.convert_pixels_to_destination_profile) {
            for (uint32_t local_y = 0; local_y < rows; ++local_y) {
                const uint64_t source_offset = static_cast<uint64_t>(y + local_y) * input_stride;
                const uint64_t strip_offset = static_cast<uint64_t>(local_y) * input_stride;
                std::copy_n(rgba.data() + static_cast<size_t>(source_offset), input_stride,
                    input_strip.data() + static_cast<size_t>(strip_offset));
            }
            if (color_transform) {
                if (!color_transform->convert_rgb8(input_strip, converted_strip)) {
                    diagnostic = "Windows CMM could not convert raster pixels to the destination ICC profile";
                    return false;
                }
            } else if (identity_transform) {
                std::copy(input_strip.begin(), input_strip.end(), converted_strip.begin());
            } else {
                diagnostic = "ICC pixel conversion transform is unavailable";
                return false;
            }
            pixel_data = converted_strip.data();
        }
        for (uint32_t local_y = 0; local_y < rows; ++local_y) {
            const uint64_t source_row = options.convert_pixels_to_destination_profile
                ? static_cast<uint64_t>(local_y) * input_stride
                : static_cast<uint64_t>(y + local_y) * width_px *
                (rgba16le ? 8u : 4u);
            const uint64_t destination_row = static_cast<uint64_t>(local_y) * stride;
            if (rgba16le) {
                std::copy_n(pixel_data + static_cast<size_t>(source_row),
                    static_cast<size_t>(width_px) * 8u,
                    strip.data() + static_cast<size_t>(destination_row));
                continue;
            }
            for (uint32_t x = 0; x < width_px; ++x) {
                const size_t source = static_cast<size_t>(source_row +
                    static_cast<uint64_t>(x) * 4u);
                const size_t destination = static_cast<size_t>(destination_row +
                    static_cast<uint64_t>(x) * (options.format == RasterImageOutputFormat::Tiff ? 4u : 3u));
                const uint32_t alpha = pixel_data[source + 3];
                if (options.format == RasterImageOutputFormat::Tiff) {
                    strip[destination + 0] = pixel_data[source + 2];
                    strip[destination + 1] = pixel_data[source + 1];
                    strip[destination + 2] = pixel_data[source + 0];
                    strip[destination + 3] = pixel_data[source + 3];
                } else {
                    const auto flatten = [&](uint8_t channel, uint8_t background) {
                        return static_cast<uint8_t>((static_cast<uint32_t>(channel) * alpha +
                            static_cast<uint32_t>(background) * (255u - alpha) + 127u) / 255u);
                    };
                    strip[destination + 0] = flatten(pixel_data[source + 2], options.jpeg_background_b);
                    strip[destination + 1] = flatten(pixel_data[source + 1], options.jpeg_background_g);
                    strip[destination + 2] = flatten(pixel_data[source + 0], options.jpeg_background_r);
                }
            }
        }
        if (FAILED(frame->WritePixels(rows, stride,
                static_cast<UINT>(strip.size()), strip.data()))) {
            diagnostic = "WIC rejected a raster output strip";
            return false;
        }
        y += rows;
    }
    return true;
}

bool encode_wic(const std::wstring& temporary,
                uint32_t width_px, uint32_t height_px,
                std::span<const uint8_t> rgba, WicRasterLayout layout,
                const color::ProfileColorTransform* color_transform,
                bool identity_transform,
                const RasterImageWriteOptions& options,
                std::string& diagnostic) {
    ComApartment apartment;
    if (!apartment.usable()) {
        diagnostic = "COM could not initialize for raster output";
        return false;
    }
    ComOwner<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put())))) {
        diagnostic = "Windows Imaging Component is unavailable";
        return false;
    }
    ComOwner<IWICStream> stream;
    if (FAILED(factory->CreateStream(stream.put())) ||
        FAILED(stream->InitializeFromFilename(temporary.c_str(), GENERIC_WRITE))) {
        diagnostic = "Could not initialize the staged raster output stream";
        return false;
    }
    const GUID& container = options.format == RasterImageOutputFormat::Tiff
        ? GUID_ContainerFormatTiff : GUID_ContainerFormatJpeg;
    ComOwner<IWICBitmapEncoder> encoder;
    if (FAILED(factory->CreateEncoder(container, nullptr, encoder.put())) ||
        FAILED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache))) {
        diagnostic = "WIC could not initialize the requested raster encoder";
        return false;
    }
    ComOwner<IWICBitmapFrameEncode> frame;
    ComOwner<IPropertyBag2> options_bag;
    if (FAILED(encoder->CreateNewFrame(frame.put(), options_bag.put())) ||
        !write_encoder_options(options_bag.get(), options)) {
        diagnostic = "WIC rejected the requested lossless or JPEG quality settings";
        return false;
    }
    if (FAILED(frame->Initialize(options_bag.get())) ||
        FAILED(frame->SetSize(width_px, height_px))) {
        diagnostic = "WIC could not initialize the output frame dimensions";
        return false;
    }
    ComOwner<IWICColorContext> embedded_context;
    if (!options.embedded_icc_profile.empty()) {
        if (FAILED(factory->CreateColorContext(embedded_context.put())) ||
            FAILED(embedded_context->InitializeFromMemory(options.embedded_icc_profile.data(),
                static_cast<UINT>(options.embedded_icc_profile.size())))) {
            diagnostic = "The requested embedded ICC profile is invalid or unavailable";
            return false;
        }
    }
    if (embedded_context) {
        IWICColorContext* contexts[] = {embedded_context.get()};
        if (FAILED(frame->SetColorContexts(1, contexts))) {
            diagnostic = "WIC cannot attach the requested ICC profile to this output format";
            return false;
        }
    }
    if (FAILED(frame->SetResolution(options.dpi_x, options.dpi_y))) {
        diagnostic = "WIC could not store the requested output DPI";
        return false;
    }
    const WICPixelFormatGUID requested_pixel_format =
        layout == WicRasterLayout::Rgba16Le ? GUID_WICPixelFormat64bppRGBA
        : options.format == RasterImageOutputFormat::Tiff
            ? GUID_WICPixelFormat32bppBGRA : GUID_WICPixelFormat24bppBGR;
    WICPixelFormatGUID pixel_format = requested_pixel_format;
    if (FAILED(frame->SetPixelFormat(&pixel_format)) ||
        !IsEqualGUID(pixel_format, requested_pixel_format)) {
        diagnostic = "WIC encoder cannot preserve the requested pixel channels";
        return false;
    }
    if (!write_pixels(frame.get(), color_transform, identity_transform, options,
            width_px, height_px, rgba, layout, diagnostic) ||
        FAILED(frame->Commit()) || FAILED(encoder->Commit()) ||
        FAILED(stream->Commit(STGC_DEFAULT))) {
        if (diagnostic.empty()) diagnostic = "WIC failed to finalize the raster output";
        return false;
    }
    return true;
}

bool create_temporary_file(const std::wstring& path, std::wstring& temporary) {
    static std::atomic<uint64_t> sequence{1};
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        temporary = path + L".tmp-pikdraw-" + std::to_wstring(GetCurrentProcessId()) +
            L"-" + std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed));
        HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
            return true;
        }
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
            return false;
    }
    return false;
}

class TemporaryOutput final {
public:
    explicit TemporaryOutput(std::wstring path) : path_(std::move(path)) {}
    ~TemporaryOutput() { if (active_) DeleteFileW(path_.c_str()); }
    TemporaryOutput(const TemporaryOutput&) = delete;
    TemporaryOutput& operator=(const TemporaryOutput&) = delete;
    [[nodiscard]] const std::wstring& path() const noexcept { return path_; }
    void published() noexcept { active_ = false; }
private:
    std::wstring path_;
    bool active_{true};
};

struct TiffEntry {
    uint16_t tag{0};
    uint16_t type{0};
    uint32_t count{0};
    uint32_t value{0};
};

void append_u16le(std::vector<uint8_t>& bytes, uint16_t value) {
    bytes.push_back(static_cast<uint8_t>(value & 0xffu));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
}

void append_u32le(std::vector<uint8_t>& bytes, uint32_t value) {
    bytes.push_back(static_cast<uint8_t>(value & 0xffu));
    bytes.push_back(static_cast<uint8_t>((value >> 8) & 0xffu));
    bytes.push_back(static_cast<uint8_t>((value >> 16) & 0xffu));
    bytes.push_back(static_cast<uint8_t>(value >> 24));
}

bool append_exact(HANDLE file, std::span<const uint8_t> bytes) noexcept {
    size_t offset = 0;
    while (offset < bytes.size()) {
        const DWORD requested = static_cast<DWORD>((std::min)(
            bytes.size() - offset, static_cast<size_t>(1u << 30)));
        DWORD written = 0;
        if (!WriteFile(file, bytes.data() + offset, requested, &written, nullptr) ||
            written == 0)
            return false;
        offset += written;
    }
    return true;
}

bool write_cmyk_tiff(const std::wstring& temporary, uint32_t width_px,
                     uint32_t height_px, std::span<const uint8_t> cmyk16le,
                     const RasterCmykImageWriteOptions& options,
                     std::string& diagnostic) {
    const uint64_t row_bytes64 = static_cast<uint64_t>(width_px) * 8u;
    const uint64_t pixel_bytes64 = row_bytes64 * height_px;
    if (row_bytes64 == 0 || row_bytes64 > UINT32_MAX ||
        pixel_bytes64 != cmyk16le.size()) {
        diagnostic = "CMYK16 TIFF row or pixel byte count exceeds the classic TIFF range";
        return false;
    }
    constexpr uint32_t kPreferredStripBytes = 16u * 1024u * 1024u;
    const uint32_t row_bytes = static_cast<uint32_t>(row_bytes64);
    const uint32_t rows_per_strip = (std::max)(1u, (std::min)(height_px,
        (std::min)(kPreferredStripBytes / row_bytes,
                   UINT32_MAX / row_bytes)));
    const uint32_t strip_count = static_cast<uint32_t>(
        (static_cast<uint64_t>(height_px) + rows_per_strip - 1u) / rows_per_strip);
    if (strip_count == 0) {
        diagnostic = "CMYK16 TIFF strip count is invalid";
        return false;
    }
    if constexpr (sizeof(size_t) < sizeof(uint64_t)) {
        if (static_cast<uint64_t>(strip_count) * 4u >
            static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
            diagnostic = "CMYK16 TIFF strip table exceeds the addressable range";
            return false;
        }
    }

    if (options.dpi_x > static_cast<double>(UINT32_MAX) / 1000.0 ||
        options.dpi_y > static_cast<double>(UINT32_MAX) / 1000.0) {
        diagnostic = "CMYK16 TIFF DPI cannot be represented as a classic TIFF rational";
        return false;
    }
    const uint64_t x_resolution_numerator = std::llround(options.dpi_x * 1000.0);
    const uint64_t y_resolution_numerator = std::llround(options.dpi_y * 1000.0);
    if (x_resolution_numerator == 0 || y_resolution_numerator == 0 ||
        x_resolution_numerator > UINT32_MAX || y_resolution_numerator > UINT32_MAX) {
        diagnostic = "CMYK16 TIFF DPI cannot be represented as a classic TIFF rational";
        return false;
    }

    constexpr uint16_t kEntryCount = 16;
    const uint64_t ifd_end64 = 8u + 2u +
        static_cast<uint64_t>(kEntryCount) * 12u + 4u;
    if (ifd_end64 > UINT32_MAX) {
        diagnostic = "CMYK16 TIFF metadata exceeds the classic TIFF offset range";
        return false;
    }
    const uint32_t ifd_end = static_cast<uint32_t>(ifd_end64);
    std::vector<uint8_t> extra;
    const auto append_extra = [&](std::span<const uint8_t> value,
                                  size_t alignment) -> uint32_t {
        while ((ifd_end + extra.size()) % alignment != 0) extra.push_back(0);
        if (extra.size() > UINT32_MAX - ifd_end ||
            value.size() > UINT32_MAX - ifd_end - extra.size())
            throw std::length_error("TIFF extra data exceeds 32-bit offsets");
        const uint32_t offset = ifd_end + static_cast<uint32_t>(extra.size());
        extra.insert(extra.end(), value.begin(), value.end());
        return offset;
    };

    std::vector<uint8_t> bits_per_sample;
    for (unsigned channel = 0; channel < 4; ++channel)
        append_u16le(bits_per_sample, 16);
    const uint32_t bits_offset = append_extra(bits_per_sample, 2);
    std::vector<uint8_t> strip_offsets;
    std::vector<uint8_t> strip_byte_counts;
    if (strip_count > 1) {
        strip_offsets.resize(static_cast<size_t>(strip_count) * 4u);
        strip_byte_counts.resize(static_cast<size_t>(strip_count) * 4u);
    }
    const uint32_t strip_offsets_offset = strip_count > 1
        ? append_extra(strip_offsets, 4) : 0;
    const uint32_t strip_byte_counts_offset = strip_count > 1
        ? append_extra(strip_byte_counts, 4) : 0;
    std::vector<uint8_t> x_resolution;
    append_u32le(x_resolution, static_cast<uint32_t>(x_resolution_numerator));
    append_u32le(x_resolution, 1000);
    const uint32_t x_resolution_offset = append_extra(x_resolution, 4);
    std::vector<uint8_t> y_resolution;
    append_u32le(y_resolution, static_cast<uint32_t>(y_resolution_numerator));
    append_u32le(y_resolution, 1000);
    const uint32_t y_resolution_offset = append_extra(y_resolution, 4);
    const uint32_t icc_offset = append_extra(options.embedded_icc_profile, 2);
    if ((ifd_end + extra.size()) % 2u != 0u) extra.push_back(0);
    const uint64_t data_start64 = ifd_end64 + extra.size();
    if (data_start64 > UINT32_MAX ||
        pixel_bytes64 > UINT32_MAX - data_start64) {
        diagnostic = "CMYK16 TIFF exceeds the 4 GiB classic TIFF limit; BigTIFF is not available on this route";
        return false;
    }
    const uint32_t data_start = static_cast<uint32_t>(data_start64);
    uint64_t pixel_offset = data_start;
    for (uint32_t strip = 0; strip < strip_count; ++strip) {
        const uint32_t first_row = strip * rows_per_strip;
        const uint32_t rows = (std::min)(rows_per_strip, height_px - first_row);
        const uint32_t byte_count = rows * row_bytes;
        if (strip_count > 1) {
            const size_t offset_index = static_cast<size_t>(strip) * 4u;
            for (unsigned byte = 0; byte < 4; ++byte) {
                strip_offsets[offset_index + byte] =
                    static_cast<uint8_t>((static_cast<uint32_t>(pixel_offset) >> (byte * 8)) & 0xffu);
                strip_byte_counts[offset_index + byte] =
                    static_cast<uint8_t>((byte_count >> (byte * 8)) & 0xffu);
            }
        }
        pixel_offset += byte_count;
    }
    if (strip_count > 1) {
        const size_t offset_in_extra = strip_offsets_offset - ifd_end;
        const size_t counts_in_extra = strip_byte_counts_offset - ifd_end;
        std::copy(strip_offsets.begin(), strip_offsets.end(),
                  extra.begin() + static_cast<ptrdiff_t>(offset_in_extra));
        std::copy(strip_byte_counts.begin(), strip_byte_counts.end(),
                  extra.begin() + static_cast<ptrdiff_t>(counts_in_extra));
    }

    std::vector<TiffEntry> entries{
        {256, 4, 1, width_px},
        {257, 4, 1, height_px},
        {258, 3, 4, bits_offset},
        {259, 3, 1, 1},
        {262, 3, 1, 5},
        {273, 4, strip_count, strip_count > 1 ? strip_offsets_offset : data_start},
        {277, 3, 1, 4},
        {278, 4, 1, rows_per_strip},
        {279, 4, strip_count, strip_count > 1
            ? strip_byte_counts_offset : static_cast<uint32_t>(pixel_bytes64)},
        {282, 5, 1, x_resolution_offset},
        {283, 5, 1, y_resolution_offset},
        {284, 3, 1, 1},
        {296, 3, 1, 2},
        {332, 3, 1, 1},
        {334, 3, 1, 4},
        {34675, 7, static_cast<uint32_t>(options.embedded_icc_profile.size()), icc_offset}};

    std::vector<uint8_t> metadata;
    metadata.reserve(data_start);
    metadata.push_back('I');
    metadata.push_back('I');
    append_u16le(metadata, 42);
    append_u32le(metadata, 8);
    append_u16le(metadata, kEntryCount);
    for (const auto& entry : entries) {
        append_u16le(metadata, entry.tag);
        append_u16le(metadata, entry.type);
        append_u32le(metadata, entry.count);
        append_u32le(metadata, entry.value);
    }
    append_u32le(metadata, 0);
    metadata.insert(metadata.end(), extra.begin(), extra.end());
    if (metadata.size() != data_start) {
        diagnostic = "CMYK16 TIFF metadata offset calculation failed";
        return false;
    }

    if (options.wait_for_memory_headroom &&
        !options.wait_for_memory_headroom(static_cast<uint64_t>(metadata.size()))) {
        diagnostic = "CMYK16 TIFF output was cancelled or could not reserve metadata memory";
        return false;
    }
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        diagnostic = "Could not open the staged CMYK16 TIFF output";
        return false;
    }
    struct HandleGuard {
        HANDLE value;
        ~HandleGuard() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    } guard{file};
    if (!append_exact(file, metadata)) {
        diagnostic = "Could not write CMYK16 TIFF metadata";
        return false;
    }
    for (uint32_t strip = 0; strip < strip_count; ++strip) {
        if (options.wait_for_memory_headroom &&
            !options.wait_for_memory_headroom(0)) {
            diagnostic = "CMYK16 TIFF output was cancelled while writing image strips";
            return false;
        }
        const uint32_t first_row = strip * rows_per_strip;
        const uint32_t rows = (std::min)(rows_per_strip, height_px - first_row);
        const size_t offset = static_cast<size_t>(first_row) * row_bytes;
        const size_t bytes = static_cast<size_t>(rows) * row_bytes;
        if (!append_exact(file, cmyk16le.subspan(offset, bytes))) {
            diagnostic = "Could not write a CMYK16 TIFF image strip";
            return false;
        }
    }
    if (!FlushFileBuffers(file)) {
        diagnostic = "Could not flush the completed CMYK16 TIFF";
        return false;
    }
    return true;
}
#endif

} // namespace

RasterImageWriteResult WriteRasterImageFile(
    std::wstring_view output_path, uint32_t width_px, uint32_t height_px,
    std::span<const uint8_t> rgba8, const RasterImageWriteOptions& options) {
    RasterImageWriteResult result;
    if (output_path.empty() || width_px == 0 || height_px == 0 || !valid_options(options)) {
        result.diagnostic = "Raster output path, dimensions, DPI or quality is invalid";
        return result;
    }
    if (!options.embedded_icc_profile.empty() &&
        !has_rgb_icc_signature(options.embedded_icc_profile)) {
        result.diagnostic = "TIFF/JPEG raster output requires an RGB embedded ICC profile";
        return result;
    }
    if (options.convert_pixels_to_destination_profile) {
        if (options.destination_icc_profile.empty() ||
            !has_rgb_icc_signature(options.destination_icc_profile)) {
            result.diagnostic = "ICC pixel conversion requires a destination RGB profile";
            return result;
        }
        if (!options.source_icc_profile.empty() &&
            !has_rgb_icc_signature(options.source_icc_profile)) {
            result.diagnostic = "Raster pixel conversion accepts only an RGB source profile";
            return result;
        }
        if (!options.embedded_icc_profile.empty() &&
            !same_bytes(options.embedded_icc_profile, options.destination_icc_profile)) {
            result.diagnostic = "Embedded ICC profile must exactly match the converted pixel destination profile";
            return result;
        }
    } else if (!options.source_icc_profile.empty() ||
               !options.destination_icc_profile.empty()) {
        result.diagnostic = "Source and destination ICC profiles require pixel conversion";
        return result;
    }
    const uint64_t pixel_count = static_cast<uint64_t>(width_px) * height_px;
    if (pixel_count > SIZE_MAX / 4u || pixel_count * 4u != rgba8.size()) {
        result.diagnostic = "RGBA8 input size does not match the output dimensions";
        return result;
    }
#ifndef _WIN32
    result.diagnostic = "WIC raster output is available only in the Windows desktop build";
    return result;
#else
    try {
        std::shared_ptr<const color::ProfileResource> source_profile;
        std::shared_ptr<const color::ProfileResource> destination_profile;
        std::shared_ptr<const color::ProfileColorTransform> color_transform;
        bool identity_transform = false;
        if (options.convert_pixels_to_destination_profile) {
            if (options.source_icc_profile.empty()) {
                const auto srgb_path = color::IccPreview::system_srgb_profile();
                if (srgb_path.empty()) {
                    result.diagnostic = "System sRGB profile is unavailable for raster conversion";
                    return result;
                }
                auto loaded = color::ProfileResource::load_file(srgb_path, color::ColorSpace::Rgb);
                if (!loaded) {
                    result.diagnostic = "System sRGB profile could not be loaded for raster conversion";
                    return result;
                }
                source_profile = std::move(loaded.resource);
            } else {
                auto loaded = color::ProfileResource::create("Raster source", color::ColorSpace::Rgb,
                    {options.source_icc_profile.begin(), options.source_icc_profile.end()});
                if (!loaded) {
                    result.diagnostic = "Source RGB ICC profile is invalid or unavailable";
                    return result;
                }
                source_profile = std::move(loaded.resource);
            }
            auto loaded_destination = color::ProfileResource::create("Raster destination",
                color::ColorSpace::Rgb,
                {options.destination_icc_profile.begin(), options.destination_icc_profile.end()});
            if (!loaded_destination) {
                result.diagnostic = "Destination RGB ICC profile is invalid or unavailable";
                return result;
            }
            destination_profile = std::move(loaded_destination.resource);
            identity_transform = same_bytes(source_profile->bytes(), destination_profile->bytes());
            if (!identity_transform) {
                auto created = color::ProfileColorTransform::create(
                    source_profile, destination_profile);
                if (!created) {
                    result.diagnostic = created.diagnostic.empty()
                        ? "Windows CMM could not create the raster ICC conversion"
                        : std::move(created.diagnostic);
                    return result;
                }
                color_transform = std::move(created.transform);
            }
        }
        const std::wstring target(output_path);
        const std::filesystem::path target_path(target);
        std::wstring extension = target_path.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
        const bool extension_matches = options.format == RasterImageOutputFormat::Tiff
            ? extension == L".tif" || extension == L".tiff"
            : extension == L".jpg" || extension == L".jpeg";
        if (!extension_matches) {
            result.diagnostic = "Output extension does not match the selected TIFF/JPEG format";
            return result;
        }
        const DWORD attributes = GetFileAttributesW(target.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && !options.allow_replace_existing) {
            result.diagnostic = "Output file already exists and replacement was not authorized";
            return result;
        }
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            result.diagnostic = "Output path names a directory";
            return result;
        }
        std::wstring temporary_path;
        if (!create_temporary_file(target, temporary_path)) {
            result.diagnostic = "Could not create a staged raster output beside the destination";
            return result;
        }
        TemporaryOutput temporary(std::move(temporary_path));
        if (!encode_wic(temporary.path(), width_px, height_px, rgba8,
                WicRasterLayout::Rgba8,
                color_transform.get(), identity_transform, options, result.diagnostic)) {
            return result;
        }
        const DWORD move_flags = MOVEFILE_WRITE_THROUGH |
            (options.allow_replace_existing ? MOVEFILE_REPLACE_EXISTING : 0u);
        if (!MoveFileExW(temporary.path().c_str(), target.c_str(), move_flags)) {
            result.diagnostic = "Could not atomically publish the completed raster output";
            return result;
        }
        temporary.published();
        result.success = true;
        return result;
    } catch (...) {
        result.diagnostic = "Raster output failed while allocating or validating metadata";
        return result;
    }
#endif
}

RasterImageWriteResult WriteRasterImageFile16(
    std::wstring_view output_path, uint32_t width_px, uint32_t height_px,
    std::span<const uint8_t> rgba16le, const RasterImageWriteOptions& options) {
    RasterImageWriteResult result;
    if (output_path.empty() || width_px == 0 || height_px == 0 || !valid_options(options)) {
        result.diagnostic = "Raster output path, dimensions, DPI or quality is invalid";
        return result;
    }
    if (options.format != RasterImageOutputFormat::Tiff) {
        result.diagnostic = "RGBA16LE output is supported only for lossless TIFF";
        return result;
    }
    if (options.convert_pixels_to_destination_profile ||
        !options.source_icc_profile.empty() ||
        !options.destination_icc_profile.empty()) {
        result.diagnostic = "RGBA16LE TIFF output requires samples already in the embedded profile";
        return result;
    }
    if (!options.embedded_icc_profile.empty() &&
        !has_rgb_icc_signature(options.embedded_icc_profile)) {
        result.diagnostic = "TIFF raster output requires an RGB embedded ICC profile";
        return result;
    }
    const uint64_t pixel_count = static_cast<uint64_t>(width_px) * height_px;
    if (pixel_count > SIZE_MAX / 8u || pixel_count * 8u != rgba16le.size()) {
        result.diagnostic = "RGBA16LE input size does not match the output dimensions";
        return result;
    }
#ifndef _WIN32
    result.diagnostic = "WIC raster output is available only in the Windows desktop build";
    return result;
#else
    try {
        const std::wstring target(output_path);
        const std::filesystem::path target_path(target);
        std::wstring extension = target_path.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
        if (extension != L".tif" && extension != L".tiff") {
            result.diagnostic = "Output extension does not match the selected TIFF format";
            return result;
        }
        const DWORD attributes = GetFileAttributesW(target.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && !options.allow_replace_existing) {
            result.diagnostic = "Output file already exists and replacement was not authorized";
            return result;
        }
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            result.diagnostic = "Output path names a directory";
            return result;
        }
        std::wstring temporary_path;
        if (!create_temporary_file(target, temporary_path)) {
            result.diagnostic = "Could not create a staged raster output beside the destination";
            return result;
        }
        TemporaryOutput temporary(std::move(temporary_path));
        if (!encode_wic(temporary.path(), width_px, height_px, rgba16le,
                WicRasterLayout::Rgba16Le,
                nullptr, false, options, result.diagnostic))
            return result;
        const DWORD move_flags = MOVEFILE_WRITE_THROUGH |
            (options.allow_replace_existing ? MOVEFILE_REPLACE_EXISTING : 0u);
        if (!MoveFileExW(temporary.path().c_str(), target.c_str(), move_flags)) {
            result.diagnostic = "Could not atomically publish the completed raster output";
            return result;
        }
        temporary.published();
        result.success = true;
        return result;
    } catch (...) {
        result.diagnostic = "RGBA16LE raster output failed while allocating or validating metadata";
        return result;
    }
#endif
}

RasterImageWriteResult WriteRasterImageCmykFile16(
    std::wstring_view output_path, uint32_t width_px, uint32_t height_px,
    std::span<const uint8_t> cmyk16le,
    const RasterCmykImageWriteOptions& options) {
    RasterImageWriteResult result;
    if (output_path.empty() || width_px == 0 || height_px == 0 ||
        !std::isfinite(options.dpi_x) || !std::isfinite(options.dpi_y) ||
        options.dpi_x <= 0.0 || options.dpi_y <= 0.0 ||
        options.dpi_x > std::numeric_limits<float>::max() ||
        options.dpi_y > std::numeric_limits<float>::max() ||
        options.embedded_icc_profile.size() > 1024u * 1024u) {
        result.diagnostic = "CMYK TIFF output path, dimensions, DPI, or ICC size is invalid";
        return result;
    }
    if (!has_cmyk_icc_signature(options.embedded_icc_profile)) {
        result.diagnostic = "CMYK TIFF output requires an embedded CMYK ICC profile";
        return result;
    }
    const uint64_t pixel_count = static_cast<uint64_t>(width_px) * height_px;
    if (pixel_count > SIZE_MAX / 8u || pixel_count * 8u != cmyk16le.size()) {
        result.diagnostic = "CMYK16LE input size does not match the output dimensions";
        return result;
    }
#ifndef _WIN32
    result.diagnostic = "CMYK TIFF output is available only in the Windows desktop build";
    return result;
#else
    try {
        const std::wstring target(output_path);
        const std::filesystem::path target_path(target);
        std::wstring extension = target_path.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
        if (extension != L".tif" && extension != L".tiff") {
            result.diagnostic = "Output extension does not match the selected TIFF format";
            return result;
        }
        const DWORD attributes = GetFileAttributesW(target.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && !options.allow_replace_existing) {
            result.diagnostic = "Output file already exists and replacement was not authorized";
            return result;
        }
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            result.diagnostic = "Output path names a directory";
            return result;
        }
        std::wstring temporary_path;
        if (!create_temporary_file(target, temporary_path)) {
            result.diagnostic = "Could not create a staged CMYK TIFF beside the destination";
            return result;
        }
        TemporaryOutput temporary(std::move(temporary_path));
        if (!write_cmyk_tiff(temporary.path(), width_px, height_px,
                cmyk16le, options, result.diagnostic))
            return result;
        const DWORD move_flags = MOVEFILE_WRITE_THROUGH |
            (options.allow_replace_existing ? MOVEFILE_REPLACE_EXISTING : 0u);
        if (!MoveFileExW(temporary.path().c_str(), target.c_str(), move_flags)) {
            result.diagnostic = "Could not atomically publish the completed CMYK TIFF";
            return result;
        }
        temporary.published();
        result.success = true;
        return result;
    } catch (...) {
        result.diagnostic = "CMYK TIFF output failed while allocating or validating metadata";
        return result;
    }
#endif
}

} // namespace pikdraw::interop
