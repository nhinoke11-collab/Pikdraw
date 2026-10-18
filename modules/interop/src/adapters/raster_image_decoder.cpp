#include "pikdraw/interop/adapters/raster_image_decoder.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

#include <cstring>
#include "tinf.h"

#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#include <propvarutil.h>
#include <wincodec.h>
#endif

namespace pikdraw::interop {
namespace {

RasterImageDecodeResult failure(std::string code, std::string message) {
    RasterImageDecodeResult result;
    result.diagnostic_code = std::move(code);
    result.message = std::move(message);
    return result;
}

uint32_t read_be32(const uint8_t* p) noexcept {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

uint8_t paeth_predictor(int a, int b, int c) noexcept {
    const int p = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return static_cast<uint8_t>(a);
    if (pb <= pc) return static_cast<uint8_t>(b);
    return static_cast<uint8_t>(c);
}

RasterImageDecodeResult decode_png_pure(
    std::span<const uint8_t> source,
    const RasterImageDecodeOptions& options) {
    if (options.frame_index != 0)
        return failure("RASTER_FRAME_NOT_FOUND", "The requested PNG frame does not exist.");
    if (source.size() < 8)
        return failure("RASTER_DECODE_FAILED", "Invalid PNG source.");

    uint32_t width = 0, height = 0;
    uint8_t bit_depth = 0, color_type = 0;
    bool has_ihdr = false;
    double dpi_x = 96.0, dpi_y = 96.0;
    std::vector<uint8_t> idat_bytes;
    std::vector<uint8_t> palette;

    size_t offset = 8;
    while (offset + 12 <= source.size()) {
        const uint32_t chunk_len = read_be32(&source[offset]);
        const uint8_t* chunk_type = &source[offset + 4];
        if (offset + 12 + chunk_len > source.size()) break;
        const auto payload = source.subspan(offset + 8, chunk_len);
        offset += 12 + chunk_len;

        if (std::memcmp(chunk_type, "IHDR", 4) == 0) {
            if (payload.size() < 13) return failure("RASTER_DECODE_FAILED", "Malformed PNG IHDR.");
            width = read_be32(payload.data());
            height = read_be32(payload.data() + 4);
            bit_depth = payload[8];
            color_type = payload[9];
            const uint8_t compression = payload[10];
            const uint8_t filter_method = payload[11];
            const uint8_t interlace = payload[12];
            if (compression != 0 || filter_method != 0 || interlace != 0)
                return failure("RASTER_DECODE_FAILED", "Unsupported PNG compression/filter/interlace.");
            has_ihdr = true;
        } else if (std::memcmp(chunk_type, "IDAT", 4) == 0) {
            idat_bytes.insert(idat_bytes.end(), payload.begin(), payload.end());
        } else if (std::memcmp(chunk_type, "PLTE", 4) == 0) {
            palette.assign(payload.begin(), payload.end());
        } else if (std::memcmp(chunk_type, "pHYs", 4) == 0) {
            if (payload.size() >= 9 && payload[8] == 1) {
                const uint32_t ppux = read_be32(payload.data());
                const uint32_t ppuy = read_be32(payload.data() + 4);
                if (ppux > 0 && ppuy > 0) {
                    dpi_x = static_cast<double>(ppux) * 0.0254;
                    dpi_y = static_cast<double>(ppuy) * 0.0254;
                }
            }
        } else if (std::memcmp(chunk_type, "IEND", 4) == 0) {
            break;
        }
    }

    if (!has_ihdr || width == 0 || height == 0 || idat_bytes.empty())
        return failure("RASTER_DECODE_FAILED", "Missing IHDR or IDAT in PNG.");

    if (bit_depth != 8)
        return failure("RASTER_BIT_DEPTH_UNSUPPORTED", "Only 8-bit PNG channels are currently supported.");

    uint32_t channels = 0;
    switch (color_type) {
    case 0: channels = 1; break;
    case 2: channels = 3; break;
    case 3: channels = 1; break;
    case 4: channels = 2; break;
    case 6: channels = 4; break;
    default:
        return failure("RASTER_PIXEL_CONVERSION", "Unsupported PNG color type.");
    }

    if (color_type == 3 && palette.empty())
        return failure("RASTER_DECODE_FAILED", "Indexed PNG missing palette.");

    const uint64_t pixel_count = static_cast<uint64_t>(width) * height;
    const uint64_t output_bytes = pixel_count * 4;
    if (pixel_count > options.max_decoded_pixels || output_bytes > options.max_decoded_bytes)
        return failure("RASTER_DECODED_BUDGET", "Decoded raster dimensions exceed budget.");

    const size_t row_raw_bytes = 1 + static_cast<size_t>(width) * channels;
    const size_t total_raw_bytes = row_raw_bytes * height;
    std::vector<uint8_t> uncompressed(total_raw_bytes);
    unsigned int dest_len = static_cast<unsigned int>(total_raw_bytes);
    const int tinf_res = tinf_zlib_uncompress(uncompressed.data(), &dest_len,
                                              idat_bytes.data(), static_cast<unsigned int>(idat_bytes.size()));
    if (tinf_res != TINF_OK || dest_len != total_raw_bytes)
        return failure("RASTER_DECODE_FAILED", "Failed to decompress PNG IDAT stream.");

    for (uint32_t y = 0; y < height; ++y) {
        const size_t row_offset = y * row_raw_bytes;
        const uint8_t filter = uncompressed[row_offset];
        uint8_t* curr = &uncompressed[row_offset + 1];
        const uint8_t* prev = (y > 0) ? &uncompressed[(y - 1) * row_raw_bytes + 1] : nullptr;

        const size_t line_bytes = static_cast<size_t>(width) * channels;
        switch (filter) {
        case 0:
            break;
        case 1:
            for (size_t i = 0; i < line_bytes; ++i) {
                const uint8_t a = (i >= channels) ? curr[i - channels] : 0;
                curr[i] = static_cast<uint8_t>(curr[i] + a);
            }
            break;
        case 2:
            if (prev) {
                for (size_t i = 0; i < line_bytes; ++i) {
                    curr[i] = static_cast<uint8_t>(curr[i] + prev[i]);
                }
            }
            break;
        case 3:
            for (size_t i = 0; i < line_bytes; ++i) {
                const uint8_t a = (i >= channels) ? curr[i - channels] : 0;
                const uint8_t b = prev ? prev[i] : 0;
                curr[i] = static_cast<uint8_t>(curr[i] + ((a + b) >> 1));
            }
            break;
        case 4:
            for (size_t i = 0; i < line_bytes; ++i) {
                const int a = (i >= channels) ? curr[i - channels] : 0;
                const int b = prev ? prev[i] : 0;
                const int c = (prev && i >= channels) ? prev[i - channels] : 0;
                curr[i] = static_cast<uint8_t>(curr[i] + paeth_predictor(a, b, c));
            }
            break;
        default:
            return failure("RASTER_DECODE_FAILED", "Corrupt PNG scanline filter.");
        }
    }

    RasterImageData image;
    image.format = RasterImageFormat::Png;
    image.width_px = width;
    image.height_px = height;
    image.frame_count = 1;
    image.frame_index = options.frame_index;
    image.source_bits_per_pixel = channels * 8;
    image.exif_orientation = 1;
    image.orientation_applied = false;
    image.dpi_x = dpi_x;
    image.dpi_y = dpi_y;
    image.icc_profile_status = RasterIccProfileStatus::NotPresent;
    image.pixels_rgba.resize(static_cast<size_t>(output_bytes));

    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* src_row = &uncompressed[y * row_raw_bytes + 1];
        uint8_t* dst_row = &image.pixels_rgba[static_cast<size_t>(y) * width * 4];

        if (color_type == 6) {
            std::memcpy(dst_row, src_row, static_cast<size_t>(width) * 4);
        } else if (color_type == 2) {
            for (uint32_t x = 0; x < width; ++x) {
                dst_row[x * 4 + 0] = src_row[x * 3 + 0];
                dst_row[x * 4 + 1] = src_row[x * 3 + 1];
                dst_row[x * 4 + 2] = src_row[x * 3 + 2];
                dst_row[x * 4 + 3] = 255;
            }
        } else if (color_type == 0) {
            for (uint32_t x = 0; x < width; ++x) {
                const uint8_t g = src_row[x];
                dst_row[x * 4 + 0] = g;
                dst_row[x * 4 + 1] = g;
                dst_row[x * 4 + 2] = g;
                dst_row[x * 4 + 3] = 255;
            }
        } else if (color_type == 4) {
            for (uint32_t x = 0; x < width; ++x) {
                const uint8_t g = src_row[x * 2 + 0];
                const uint8_t a = src_row[x * 2 + 1];
                dst_row[x * 4 + 0] = g;
                dst_row[x * 4 + 1] = g;
                dst_row[x * 4 + 2] = g;
                dst_row[x * 4 + 3] = a;
            }
        } else if (color_type == 3) {
            for (uint32_t x = 0; x < width; ++x) {
                const size_t idx = src_row[x];
                if (idx * 3 + 2 < palette.size()) {
                    dst_row[x * 4 + 0] = palette[idx * 3 + 0];
                    dst_row[x * 4 + 1] = palette[idx * 3 + 1];
                    dst_row[x * 4 + 2] = palette[idx * 3 + 2];
                } else {
                    dst_row[x * 4 + 0] = 0;
                    dst_row[x * 4 + 1] = 0;
                    dst_row[x * 4 + 2] = 0;
                }
                dst_row[x * 4 + 3] = 255;
            }
        }
    }

    RasterImageDecodeResult result;
    result.success = true;
    result.image = std::move(image);
    return result;
}

#ifdef _WIN32

template <typename T>
class ComOwner final {
public:
    ComOwner() = default;
    ~ComOwner() { reset(); }
    ComOwner(const ComOwner&) = delete;
    ComOwner& operator=(const ComOwner&) = delete;
    ComOwner(ComOwner&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    ComOwner& operator=(ComOwner&& other) noexcept {
        if (this != &other) {
            reset();
            value_ = std::exchange(other.value_, nullptr);
        }
        return *this;
    }
    T** put() noexcept {
        reset();
        return &value_;
    }
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

struct Ole32Dll {
    HMODULE module{nullptr};
    using CoInitFn = HRESULT(WINAPI*)(LPVOID, DWORD);
    using CoUninitFn = void(WINAPI*)();
    using CoCreateFn = HRESULT(WINAPI*)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID*);
    using PropClearFn = HRESULT(WINAPI*)(PROPVARIANT*);

    CoInitFn co_init{nullptr};
    CoUninitFn co_uninit{nullptr};
    CoCreateFn co_create{nullptr};
    PropClearFn prop_clear{nullptr};

    static Ole32Dll& get() {
        static Ole32Dll instance;
        return instance;
    }

    Ole32Dll() {
        module = LoadLibraryW(L"ole32.dll");
        if (module) {
            co_init = reinterpret_cast<CoInitFn>(GetProcAddress(module, "CoInitializeEx"));
            co_uninit = reinterpret_cast<CoUninitFn>(GetProcAddress(module, "CoUninitialize"));
            co_create = reinterpret_cast<CoCreateFn>(GetProcAddress(module, "CoCreateInstance"));
            prop_clear = reinterpret_cast<PropClearFn>(GetProcAddress(module, "PropVariantClear"));
        }
    }

    bool is_available() const noexcept {
        return module && co_init && co_uninit && co_create && prop_clear;
    }
};

class ComApartment final {
public:
    ComApartment() noexcept {
        auto& ole = Ole32Dll::get();
        if (ole.co_init) {
            result_ = ole.co_init(nullptr, COINIT_MULTITHREADED);
        }
    }
    ~ComApartment() {
        auto& ole = Ole32Dll::get();
        if (SUCCEEDED(result_) && ole.co_uninit) {
            ole.co_uninit();
        }
    }
    [[nodiscard]] bool usable() const noexcept {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }
private:
    HRESULT result_{E_FAIL};
};

GUID decoder_class(RasterImageFormat format) noexcept {
    switch (format) {
    case RasterImageFormat::Jpeg: return CLSID_WICJpegDecoder;
    case RasterImageFormat::Png: return CLSID_WICPngDecoder;
    case RasterImageFormat::Tiff: return CLSID_WICTiffDecoder;
    case RasterImageFormat::Unknown: break;
    }
    return GUID_NULL;
}

GUID container_format(RasterImageFormat format) noexcept {
    switch (format) {
    case RasterImageFormat::Jpeg: return GUID_ContainerFormatJpeg;
    case RasterImageFormat::Png: return GUID_ContainerFormatPng;
    case RasterImageFormat::Tiff: return GUID_ContainerFormatTiff;
    case RasterImageFormat::Unknown: break;
    }
    return GUID_NULL;
}

bool checked_mul(uint64_t left, uint64_t right, uint64_t& out) noexcept {
    if (right != 0 && left > std::numeric_limits<uint64_t>::max() / right)
        return false;
    out = left * right;
    return true;
}

uint16_t read_orientation(IWICBitmapFrameDecode* frame,
                          RasterImageFormat format) noexcept {
    if (!frame) return 1;
    ComOwner<IWICMetadataQueryReader> metadata;
    if (FAILED(frame->GetMetadataQueryReader(metadata.put())) || !metadata)
        return 1;
    const wchar_t* path = format == RasterImageFormat::Jpeg
        ? L"/app1/ifd/{ushort=274}"
        : L"/ifd/{ushort=274}";
    PROPVARIANT value;
    PropVariantInit(&value);
    const HRESULT result = metadata->GetMetadataByName(path, &value);
    uint16_t orientation = 1;
    if (SUCCEEDED(result)) {
        switch (value.vt) {
        case VT_UI1: orientation = value.bVal; break;
        case VT_UI2: orientation = value.uiVal; break;
        case VT_UI4:
            if (value.ulVal <= 8) orientation = static_cast<uint16_t>(value.ulVal);
            break;
        case VT_I2:
            if (value.iVal > 0 && value.iVal <= 8)
                orientation = static_cast<uint16_t>(value.iVal);
            break;
        case VT_I4:
            if (value.lVal > 0 && value.lVal <= 8)
                orientation = static_cast<uint16_t>(value.lVal);
            break;
        default: break;
        }
    }
    auto& ole = Ole32Dll::get();
    if (ole.prop_clear) ole.prop_clear(&value);
    return orientation >= 1 && orientation <= 8 ? orientation : 1;
}

WICBitmapTransformOptions orientation_transform(uint16_t orientation) noexcept {
    switch (orientation) {
    case 2: return WICBitmapTransformFlipHorizontal;
    case 3: return WICBitmapTransformRotate180;
    case 4: return WICBitmapTransformFlipVertical;
    case 5: return static_cast<WICBitmapTransformOptions>(
        WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal);
    case 6: return WICBitmapTransformRotate90;
    case 7: return static_cast<WICBitmapTransformOptions>(
        WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal);
    case 8: return WICBitmapTransformRotate270;
    default: return WICBitmapTransformRotate0;
    }
}

RasterIccProfileStatus read_icc_profile(IWICImagingFactory* factory,
                                        IWICBitmapFrameDecode* frame,
                                        uint64_t max_bytes,
                                        RasterImageData& image) {
    UINT context_count = 0;
    if (FAILED(frame->GetColorContexts(0, nullptr, &context_count)))
        return RasterIccProfileStatus::Unavailable;
    if (context_count == 0) return RasterIccProfileStatus::NotPresent;
    image.has_embedded_icc_profile = true;
    if (context_count > 16)
        return RasterIccProfileStatus::MultipleContextsUnsupported;
    std::vector<ComOwner<IWICColorContext>> contexts(context_count);
    std::vector<IWICColorContext*> raw_contexts;
    raw_contexts.reserve(context_count);
    for (auto& context : contexts) {
        if (FAILED(factory->CreateColorContext(context.put())))
            return RasterIccProfileStatus::Unavailable;
        raw_contexts.push_back(context.get());
    }
    UINT actual_count = 0;
    if (FAILED(frame->GetColorContexts(context_count, raw_contexts.data(), &actual_count)))
        return RasterIccProfileStatus::Unavailable;
    if (actual_count > 1)
        return RasterIccProfileStatus::MultipleContextsUnsupported;
    if (actual_count == 0) return RasterIccProfileStatus::Unavailable;
    for (UINT index = 0; index < actual_count; ++index) {
        UINT byte_count = 0;
        const HRESULT size_result = contexts[index]->GetProfileBytes(0, nullptr, &byte_count);
        if (FAILED(size_result) || byte_count == 0)
            return RasterIccProfileStatus::Unavailable;
        if (byte_count > max_bytes) return RasterIccProfileStatus::ExceedsBudget;
        std::vector<uint8_t> profile(byte_count);
        if (FAILED(contexts[index]->GetProfileBytes(byte_count, profile.data(), &byte_count)) ||
            byte_count != profile.size())
            return RasterIccProfileStatus::Unavailable;
        image.embedded_icc_profile = std::move(profile);
        return RasterIccProfileStatus::Preserved;
    }
    return RasterIccProfileStatus::Unavailable;
}

bool source_bits_per_pixel(IWICImagingFactory* factory, REFWICPixelFormatGUID format,
                           uint32_t& bits) noexcept {
    ComOwner<IWICComponentInfo> component;
    if (FAILED(factory->CreateComponentInfo(format, component.put())) || !component)
        return false;
    ComOwner<IWICPixelFormatInfo> pixel_format;
    if (FAILED(component->QueryInterface(IID_PPV_ARGS(pixel_format.put()))) || !pixel_format)
        return false;
    UINT value = 0;
    if (FAILED(pixel_format->GetBitsPerPixel(&value)) || value == 0) return false;
    bits = value;
    return true;
}

bool profile_is_sdr8_or_less(REFWICPixelFormatGUID format) noexcept {
    const GUID rejected_formats[]{
        GUID_WICPixelFormat16bppGray,
        GUID_WICPixelFormat16bppGrayFixedPoint,
        GUID_WICPixelFormat16bppGrayHalf,
        GUID_WICPixelFormat32bppGrayFloat,
        GUID_WICPixelFormat32bppGrayFixedPoint,
        GUID_WICPixelFormat32bppBGR101010,
        GUID_WICPixelFormat32bppRGBE,
        GUID_WICPixelFormat32bppRGBA1010102,
        GUID_WICPixelFormat32bppRGBA1010102XR,
        GUID_WICPixelFormat32bppR10G10B10A2,
        GUID_WICPixelFormat32bppR10G10B10A2HDR10};
    return std::none_of(std::begin(rejected_formats), std::end(rejected_formats),
        [&](const GUID& rejected) { return IsEqualGUID(format, rejected) != FALSE; });
}

#endif

} // namespace

RasterImageFormat RasterImageDecoder::sniff_format(
    std::span<const uint8_t> source) noexcept {
    constexpr uint8_t png_signature[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (source.size() >= sizeof(png_signature) &&
        std::equal(std::begin(png_signature), std::end(png_signature), source.begin()))
        return RasterImageFormat::Png;
    if (source.size() >= 3 && source[0] == 0xFF && source[1] == 0xD8 && source[2] == 0xFF)
        return RasterImageFormat::Jpeg;
    if (source.size() >= 4 &&
        ((source[0] == 'I' && source[1] == 'I' && source[2] == 0x2A && source[3] == 0x00) ||
         (source[0] == 'M' && source[1] == 'M' && source[2] == 0x00 && source[3] == 0x2A)))
        return RasterImageFormat::Tiff;
    return RasterImageFormat::Unknown;
}

RasterImageDecodeResult RasterImageDecoder::decode(
    std::span<const uint8_t> source,
    const RasterImageDecodeOptions& options) {
    if (source.empty()) return failure("RASTER_EMPTY_SOURCE", "The raster source is empty.");
    if (options.max_source_bytes == 0 || source.size() > options.max_source_bytes ||
        source.size() > std::numeric_limits<uint32_t>::max())
        return failure("RASTER_SOURCE_LIMIT", "The raster source exceeds the configured byte limit.");
    if (options.max_decoded_pixels == 0 || options.max_decoded_bytes == 0 ||
        options.max_decoded_bytes > std::numeric_limits<uint32_t>::max() ||
        options.max_frames == 0 || options.max_icc_profile_bytes > std::numeric_limits<uint32_t>::max())
        return failure("RASTER_INVALID_BUDGET", "The raster decode budget is invalid.");

    const auto format = sniff_format(source);
    if (format == RasterImageFormat::Unknown)
        return failure("RASTER_SIGNATURE_UNSUPPORTED", "The file signature is not PNG, JPEG or TIFF.");

    if (format == RasterImageFormat::Png)
        return decode_png_pure(source, options);

#ifdef _WIN32
    auto& ole = Ole32Dll::get();
    if (!ole.is_available())
        return failure("RASTER_WIC_UNAVAILABLE", "Windows Imaging Component is unavailable.");

    ComApartment apartment;
    if (!apartment.usable())
        return failure("RASTER_COM_INITIALIZATION", "Windows Imaging Component could not initialize COM.");

    ComOwner<IWICImagingFactory> factory;
    if (FAILED(ole.co_create(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                             IID_PPV_ARGS(factory.put()))) || !factory)
        return failure("RASTER_WIC_UNAVAILABLE", "Windows Imaging Component is unavailable.");

    // WIC's memory stream accepts mutable memory, so use a private bounded copy of the source.
    std::vector<uint8_t> source_copy(source.begin(), source.end());
    ComOwner<IWICStream> stream;
    if (FAILED(factory->CreateStream(stream.put())) || !stream ||
        FAILED(stream->InitializeFromMemory(source_copy.data(),
            static_cast<DWORD>(source_copy.size()))))
        return failure("RASTER_STREAM_INVALID", "The source bytes could not be staged for WIC.");

    ComOwner<IWICBitmapDecoder> decoder;
    const GUID clsid = decoder_class(format);
    if (IsEqualGUID(clsid, GUID_NULL) ||
        FAILED(ole.co_create(clsid, nullptr, CLSCTX_INPROC_SERVER,
                             IID_PPV_ARGS(decoder.put()))) || !decoder ||
        FAILED(decoder->Initialize(stream.get(), WICDecodeMetadataCacheOnDemand)))
        return failure("RASTER_DECODE_FAILED", "The selected Windows raster decoder rejected this file.");

    GUID actual_container{};
    if (FAILED(decoder->GetContainerFormat(&actual_container)) ||
        !IsEqualGUID(actual_container, container_format(format)))
        return failure("RASTER_CONTAINER_MISMATCH", "The decoded container does not match its signature.");

    UINT frame_count = 0;
    if (FAILED(decoder->GetFrameCount(&frame_count)) || frame_count == 0 ||
        frame_count > options.max_frames)
        return failure("RASTER_FRAME_LIMIT", "The raster frame count is empty or exceeds the configured limit.");
    if (options.frame_index >= frame_count)
        return failure("RASTER_FRAME_NOT_FOUND", "The requested raster frame does not exist.");

    ComOwner<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(options.frame_index, frame.put())) || !frame)
        return failure("RASTER_FRAME_DECODE_FAILED", "The requested raster frame could not be decoded.");

    UINT width = 0, height = 0;
    GUID source_pixel_format{};
    double dpi_x = 96.0, dpi_y = 96.0;
    if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0 ||
        FAILED(frame->GetPixelFormat(&source_pixel_format)) ||
        FAILED(frame->GetResolution(&dpi_x, &dpi_y)) ||
        !std::isfinite(dpi_x) || !std::isfinite(dpi_y) || dpi_x <= 0 || dpi_y <= 0)
        return failure("RASTER_FRAME_METADATA", "The selected frame has invalid dimensions or resolution.");

    uint32_t bits_per_pixel = 0;
    if (!source_bits_per_pixel(factory.get(), source_pixel_format, bits_per_pixel) ||
        bits_per_pixel == 0 || bits_per_pixel > 32 ||
        !profile_is_sdr8_or_less(source_pixel_format))
        return failure("RASTER_BIT_DEPTH_UNSUPPORTED",
            "This profile accepts source pixel formats up to 8 bits per channel; wider precision is not down-converted silently.");

    const uint16_t orientation = read_orientation(frame.get(), format);
    if (orientation != 1 && !options.apply_orientation)
        return failure("RASTER_ORIENTATION_REQUIRES_CHOICE",
            "The source has a non-default EXIF orientation; choose whether to apply it before import.");

    uint64_t pixel_count = 0, output_bytes = 0;
    if (!checked_mul(width, height, pixel_count) ||
        pixel_count > options.max_decoded_pixels ||
        !checked_mul(pixel_count, 4, output_bytes) ||
        output_bytes > options.max_decoded_bytes ||
        output_bytes > std::numeric_limits<UINT>::max())
        return failure("RASTER_DECODED_BUDGET", "Decoded raster dimensions exceed the configured pixel or memory budget.");

    ComOwner<IWICBitmapSource> oriented_source;
    IWICBitmapSource* selected_source = frame.get();
    ComOwner<IWICBitmapFlipRotator> rotator;
    const bool orientation_applied = orientation != 1;
    if (orientation_applied) {
        if (FAILED(factory->CreateBitmapFlipRotator(rotator.put())) || !rotator ||
            FAILED(rotator->Initialize(frame.get(), orientation_transform(orientation))) ||
            FAILED(rotator->QueryInterface(IID_PPV_ARGS(oriented_source.put()))) || !oriented_source)
            return failure("RASTER_ORIENTATION_FAILED", "The EXIF orientation transform could not be applied.");
        selected_source = oriented_source.get();
    }

    UINT output_width = 0, output_height = 0;
    if (FAILED(selected_source->GetSize(&output_width, &output_height)) ||
        output_width == 0 || output_height == 0 ||
        static_cast<uint64_t>(output_width) * output_height != pixel_count)
        return failure("RASTER_OUTPUT_DIMENSIONS", "The oriented raster dimensions are invalid.");
    uint64_t stride = 0;
    if (!checked_mul(output_width, 4, stride) ||
        stride > std::numeric_limits<UINT>::max())
        return failure("RASTER_DECODED_BUDGET", "Decoded raster dimensions exceed the configured pixel or memory budget.");

    ComOwner<IWICFormatConverter> converter;
    BOOL can_convert = FALSE;
    if (FAILED(factory->CreateFormatConverter(converter.put())) || !converter ||
        FAILED(converter->CanConvert(source_pixel_format, GUID_WICPixelFormat32bppRGBA,
                                    &can_convert)) || !can_convert ||
        FAILED(converter->Initialize(selected_source, GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
        return failure("RASTER_PIXEL_CONVERSION", "The source pixel format cannot be converted to bounded RGBA8 pixels.");

    RasterImageData image;
    image.format = format;
    image.width_px = output_width;
    image.height_px = output_height;
    image.frame_count = frame_count;
    image.frame_index = options.frame_index;
    image.source_bits_per_pixel = bits_per_pixel;
    image.exif_orientation = orientation;
    image.orientation_applied = orientation_applied;
    image.dpi_x = dpi_x;
    image.dpi_y = dpi_y;
    image.pixels_rgba.resize(static_cast<size_t>(output_bytes));
    if (FAILED(converter->CopyPixels(nullptr, static_cast<UINT>(stride),
            static_cast<UINT>(output_bytes), image.pixels_rgba.data())))
        return failure("RASTER_PIXEL_COPY", "WIC could not copy the converted pixels within the configured budget.");
    image.icc_profile_status = read_icc_profile(
        factory.get(), frame.get(), options.max_icc_profile_bytes, image);

    RasterImageDecodeResult result;
    result.success = true;
    result.image = std::move(image);
    return result;
#else
    (void)options;
    return failure("RASTER_PLATFORM_UNAVAILABLE",
        "The bounded raster codec uses Windows Imaging Component and is unavailable on this platform.");
#endif
}

} // namespace pikdraw::interop
