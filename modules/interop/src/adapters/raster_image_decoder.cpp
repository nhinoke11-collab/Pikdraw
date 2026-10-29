#include "pikdraw/interop/adapters/raster_image_decoder.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <utility>

#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#include <propvarutil.h>
#include <wincodec.h>
#endif

namespace pikdraw::interop {

#ifndef _WIN32
RasterImageDecodeResult decode_png_pure(std::span<const uint8_t> source,
    const RasterImageDecodeOptions& options);
#endif


namespace {

RasterImageDecodeResult failure(std::string code, std::string message) {
    RasterImageDecodeResult result;
    result.diagnostic_code = std::move(code);
    result.message = std::move(message);
    return result;
}

std::shared_ptr<raster::RasterMemoryBudget::Reservation> reserve_decode_memory(
    const RasterImageDecodeOptions& options, uint64_t bytes, bool& rejected) {
    if (!options.memory_budget) return {};
    try {
        auto reservation = options.memory_budget->try_reserve(bytes);
        rejected = !reservation;
        return reservation;
    } catch (...) {
        rejected = true;
        return {};
    }
}

constexpr uint32_t kWicCopyBandRows = 64;

uint32_t read_be32(const uint8_t* p) noexcept {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}



#ifdef _WIN32

bool copy_wic_pixels_in_bands(IWICBitmapSource* source, UINT width, UINT height,
                              UINT stride, std::vector<uint8_t>& pixels) {
    if (!source || width == 0 || height == 0 || stride == 0 ||
        pixels.size() != static_cast<size_t>(stride) * height)
        return false;
    for (UINT row = 0; row < height;) {
        const UINT rows = (std::min)(kWicCopyBandRows, height - row);
        WICRect rect{0, static_cast<INT>(row), static_cast<INT>(width),
                     static_cast<INT>(rows)};
        const UINT bytes = stride * rows;
        if (FAILED(source->CopyPixels(&rect, stride, bytes,
                pixels.data() + static_cast<size_t>(row) * stride)))
            return false;
        row += rows;
    }
    return true;
}

bool copy_wic_rows_in_bands(IWICBitmapSource* source, UINT width, UINT height,
                            UINT stride, const RasterRowCallback& callback,
                            const volatile bool* cancelled,
                            bool swap_blue_red, bool unpremultiply_alpha,
                            const std::shared_ptr<raster::RasterMemoryBudget>& memory_budget,
                            bool& budget_rejected, UINT origin_x = 0,
                            UINT origin_y = 0) {
    if (!source || !callback || width == 0 || height == 0 || stride == 0)
        return false;
    const size_t rows_by_wic = (std::max)(size_t{1},
        static_cast<size_t>(std::numeric_limits<UINT>::max()) / stride);
    const UINT rows_per_band = static_cast<UINT>((std::min)(
        static_cast<size_t>(height), (std::min)(size_t{64}, rows_by_wic)));
    if (rows_per_band == 0) return false;
    const size_t band_bytes = static_cast<size_t>(stride) * rows_per_band;
    std::shared_ptr<raster::RasterMemoryBudget::Reservation> band_reservation;
    if (memory_budget) {
        try {
            band_reservation = memory_budget->try_reserve(band_bytes);
        } catch (...) {
            budget_rejected = true;
            return false;
        }
        if (!band_reservation) {
            budget_rejected = true;
            return false;
        }
    }
    std::vector<uint8_t> band;
    try {
        band.resize(band_bytes);
    } catch (...) {
        return false;
    }

    for (UINT first_row = 0; first_row < height;) {
        if (cancelled && *cancelled) return false;
        const UINT rows = (std::min)(rows_per_band, height - first_row);
        const uint64_t bytes64 = static_cast<uint64_t>(stride) * rows;
        if (bytes64 > band.size() || bytes64 > std::numeric_limits<UINT>::max())
            return false;
        WICRect rect{static_cast<INT>(origin_x),
                     static_cast<INT>(origin_y + first_row), static_cast<INT>(width),
                     static_cast<INT>(rows)};
        if (FAILED(source->CopyPixels(&rect, stride, static_cast<UINT>(bytes64),
                                      band.data())))
            return false;
        const size_t bytes = static_cast<size_t>(bytes64);
        if (swap_blue_red || unpremultiply_alpha) {
            for (size_t offset = 0; offset < bytes; offset += 4) {
                if (swap_blue_red) std::swap(band[offset], band[offset + 2]);
                if (unpremultiply_alpha) {
                    const uint32_t alpha = band[offset + 3];
                    for (size_t channel = 0; channel < 3; ++channel) {
                        const uint32_t value = band[offset + channel];
                        band[offset + channel] = alpha == 0 ? 0 :
                            static_cast<uint8_t>((std::min)(255u,
                                (value * 255u + alpha / 2) / alpha));
                    }
                }
            }
        }
        for (UINT row = 0; row < rows; ++row) {
            if (cancelled && *cancelled) return false;
            const auto pixels = std::span<const uint8_t>(band).subspan(
                static_cast<size_t>(row) * stride, stride);
            if (!callback(origin_y + first_row + row, width, pixels)) return false;
        }
        first_row += rows;
    }
    return true;
}

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

struct WicDll {
    HMODULE module{nullptr};
    using CreateFn = HRESULT(WINAPI*)(UINT, IWICImagingFactory**);
    CreateFn create_factory{nullptr};

    static WicDll& get() {
        static WicDll instance;
        return instance;
    }

    WicDll() {
        module = GetModuleHandleW(L"windowscodecs.dll");
        if (!module) module = LoadLibraryExW(L"windowscodecs.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module) {
            create_factory = reinterpret_cast<CreateFn>(GetProcAddress(module, "WICCreateImagingFactory_Proxy"));
        }
    }
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
        module = GetModuleHandleW(L"ole32.dll");
        if (!module) module = LoadLibraryExW(L"ole32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module) {
            co_init = reinterpret_cast<CoInitFn>(GetProcAddress(module, "CoInitializeEx"));
            co_uninit = reinterpret_cast<CoUninitFn>(GetProcAddress(module, "CoUninitialize"));
            co_create = reinterpret_cast<CoCreateFn>(GetProcAddress(module, "CoCreateInstance"));
            prop_clear = reinterpret_cast<PropClearFn>(GetProcAddress(module, "PropVariantClear"));
        }
        if (!prop_clear) {
            HMODULE propsys = GetModuleHandleW(L"propsys.dll");
            if (!propsys) propsys = LoadLibraryExW(L"propsys.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (propsys) {
                prop_clear = reinterpret_cast<PropClearFn>(GetProcAddress(propsys, "PropVariantClear"));
            }
        }
    }

    bool is_available() const noexcept {
        return (module && co_init && co_create) || (WicDll::get().create_factory != nullptr);
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

class RasterByteSourceStream final : public IStream {
public:
    explicit RasterByteSourceStream(const RasterByteSource& source,
                                    uint64_t position = 0) noexcept
        : source_(source), position_(position) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, IID_ISequentialStream) ||
            IsEqualIID(iid, IID_IStream)) {
            *object = static_cast<IStream*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return references_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0) delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE Read(void* buffer, ULONG requested,
                                   ULONG* received) override {
        if (received) *received = 0;
        if (requested == 0) return S_OK;
        if (!buffer) return STG_E_INVALIDPOINTER;
        std::lock_guard lock(mutex_);
        const uint64_t size = source_.size_bytes();
        if (position_ >= size) return S_FALSE;
        const size_t count = static_cast<size_t>((std::min)(
            static_cast<uint64_t>(requested), size - position_));
        size_t actual = 0;
        if (!source_.read_at(position_,
                std::span<uint8_t>(static_cast<uint8_t*>(buffer), count), actual) ||
            actual > count)
            return STG_E_READFAULT;
        position_ += actual;
        if (received) *received = static_cast<ULONG>(actual);
        return actual == requested ? S_OK : S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG* written) override {
        if (written) *written = 0;
        return STG_E_ACCESSDENIED;
    }

    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move, DWORD origin,
                                   ULARGE_INTEGER* new_position) override {
        std::lock_guard lock(mutex_);
        uint64_t base = 0;
        if (origin == STREAM_SEEK_CUR) base = position_;
        else if (origin == STREAM_SEEK_END) base = source_.size_bytes();
        else if (origin != STREAM_SEEK_SET) return STG_E_INVALIDFUNCTION;

        uint64_t target = 0;
        if (move.QuadPart < 0) {
            const uint64_t magnitude = static_cast<uint64_t>(-(move.QuadPart + 1)) + 1;
            if (magnitude > base) return STG_E_INVALIDFUNCTION;
            target = base - magnitude;
        } else {
            const auto amount = static_cast<uint64_t>(move.QuadPart);
            if (amount > std::numeric_limits<uint64_t>::max() - base)
                return STG_E_INVALIDFUNCTION;
            target = base + amount;
        }
        if (target > static_cast<uint64_t>(std::numeric_limits<LONGLONG>::max()))
            return STG_E_INVALIDFUNCTION;
        position_ = target;
        if (new_position) new_position->QuadPart = target;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override {
        return STG_E_ACCESSDENIED;
    }

    HRESULT STDMETHODCALLTYPE CopyTo(IStream* destination, ULARGE_INTEGER count,
                                     ULARGE_INTEGER* read_bytes,
                                     ULARGE_INTEGER* written_bytes) override {
        if (read_bytes) read_bytes->QuadPart = 0;
        if (written_bytes) written_bytes->QuadPart = 0;
        if (!destination) return STG_E_INVALIDPOINTER;
        std::array<uint8_t, 64u * 1024u> buffer{};
        uint64_t total_read = 0;
        uint64_t total_written = 0;
        while (total_read < count.QuadPart) {
            const ULONG requested = static_cast<ULONG>((std::min)(
                static_cast<uint64_t>(buffer.size()), count.QuadPart - total_read));
            ULONG actual_read = 0;
            const HRESULT read_result = Read(buffer.data(), requested, &actual_read);
            if (FAILED(read_result)) return read_result;
            if (actual_read == 0) break;
            ULONG actual_written = 0;
            const HRESULT write_result = destination->Write(
                buffer.data(), actual_read, &actual_written);
            total_read += actual_read;
            total_written += actual_written;
            if (read_bytes) read_bytes->QuadPart = total_read;
            if (written_bytes) written_bytes->QuadPart = total_written;
            if (FAILED(write_result)) return write_result;
            if (actual_written != actual_read) return STG_E_MEDIUMFULL;
            if (actual_read != requested) break;
        }
        return total_read == count.QuadPart ? S_OK : S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE Revert() override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override {
        return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override {
        return STG_E_INVALIDFUNCTION;
    }

    HRESULT STDMETHODCALLTYPE Stat(STATSTG* stat, DWORD flags) override {
        if (!stat) return STG_E_INVALIDPOINTER;
        std::memset(stat, 0, sizeof(*stat));
        stat->type = STGTY_STREAM;
        stat->cbSize.QuadPart = source_.size_bytes();
        if ((flags & STATFLAG_NONAME) == 0) stat->pwcsName = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Clone(IStream** clone) override {
        if (!clone) return STG_E_INVALIDPOINTER;
        *clone = nullptr;
        std::lock_guard lock(mutex_);
        auto* copy = new (std::nothrow) RasterByteSourceStream(source_, position_);
        if (!copy) return E_OUTOFMEMORY;
        *clone = copy;
        return S_OK;
    }

private:
    ~RasterByteSourceStream() = default;
    std::atomic<ULONG> references_{1};
    const RasterByteSource& source_;
    std::mutex mutex_;
    uint64_t position_{0};
};

GUID decoder_class(RasterImageFormat format) noexcept {
    switch (format) {
    case RasterImageFormat::Jpeg: return CLSID_WICJpegDecoder;
    case RasterImageFormat::Png: return CLSID_WICPngDecoder;
    case RasterImageFormat::Tiff: return CLSID_WICTiffDecoder;
    case RasterImageFormat::Bmp: return CLSID_WICBmpDecoder;
    case RasterImageFormat::Unknown: break;
    }
    return GUID_NULL;
}

GUID container_format(RasterImageFormat format) noexcept {
    switch (format) {
    case RasterImageFormat::Jpeg: return GUID_ContainerFormatJpeg;
    case RasterImageFormat::Png: return GUID_ContainerFormatPng;
    case RasterImageFormat::Tiff: return GUID_ContainerFormatTiff;
    case RasterImageFormat::Bmp: return GUID_ContainerFormatBmp;
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

RasterImageDecodeResult decode_bigtiff_uncompressed_rows(
    const RasterByteSource& source, const RasterImageDecodeOptions& options,
    const RasterImageRowCallback& row_callback, const volatile bool* cancelled,
    const std::optional<RasterImageRegion>& requested_region) {
    std::array<uint8_t, 16> header{};
    if (!RasterImageDecoder::read_exact(source, 0, header, cancelled))
        return failure("RASTER_SOURCE_READ", "The BigTIFF header could not be read.");
    const bool little = header[0] == 'I' && header[1] == 'I';
    const bool big = header[0] == 'M' && header[1] == 'M';
    if ((!little && !big) ||
        (little ? (header[2] != 43 || header[3] != 0 || header[4] != 8 ||
                   header[5] != 0 || header[6] != 0 || header[7] != 0)
                : (header[2] != 0 || header[3] != 43 || header[4] != 0 ||
                   header[5] != 8 || header[6] != 0 || header[7] != 0)))
        return failure("RASTER_BIGTIFF_HEADER", "The BigTIFF header is malformed.");
    if (options.frame_index != 0)
        return failure("RASTER_FRAME_NOT_FOUND", "The requested BigTIFF frame does not exist.");

    const auto u16 = [little](const uint8_t* p) {
        return little ? static_cast<uint16_t>(p[0] | (uint16_t(p[1]) << 8))
                      : static_cast<uint16_t>((uint16_t(p[0]) << 8) | p[1]);
    };
    const auto u64 = [little](const uint8_t* p) {
        uint64_t value = 0;
        for (unsigned i = 0; i < 8; ++i) {
            const unsigned shift_index = little ? i : 7 - i;
            value |= static_cast<uint64_t>(p[i]) << (shift_index * 8);
        }
        return value;
    };
    const uint64_t ifd_offset = u64(header.data() + 8);
    std::array<uint8_t, 8> count_bytes{};
    if (ifd_offset > source.size_bytes() ||
        source.size_bytes() - ifd_offset < 16 ||
        !RasterImageDecoder::read_exact(source, ifd_offset, count_bytes, cancelled))
        return failure("RASTER_BIGTIFF_IFD", "The BigTIFF directory offset is invalid.");
    const uint64_t entry_count = u64(count_bytes.data());
    const uint64_t remaining = source.size_bytes() - ifd_offset - 16;
    if (entry_count > 4096 || entry_count > remaining / 20)
        return failure("RASTER_BIGTIFF_IFD", "The BigTIFF directory exceeds the source bounds.");

    uint64_t width = 0, height = 0, compression = 1, photometric = 0;
    uint64_t samples = 0, rows_per_strip = 0, strip_offset = 0, strip_bytes = 0;
    uint64_t planar = 1, orientation = 1, extra_sample = 0, bits_value = 0;
    uint64_t bits_count = 0;
    uint16_t bits_type = 0;
    bool has_icc_profile = false;
    const auto scalar = [&](uint16_t type, uint64_t count, uint64_t raw,
                            uint64_t& value) {
        const uint64_t type_bytes = type == 3 ? 2 : type == 4 ? 4 : type == 16 ? 8 : 0;
        if (count != 1 || type_bytes == 0) return false;
        if (type_bytes == 2) value = little ? (raw & 0xffffu) : (raw >> 48);
        else if (type_bytes == 4) value = little ? (raw & 0xffffffffu) : (raw >> 32);
        else value = raw;
        return true;
    };
    std::array<uint8_t, 20> entry{};
    for (uint64_t i = 0; i < entry_count; ++i) {
        const uint64_t offset = ifd_offset + 8 + i * 20;
        if (!RasterImageDecoder::read_exact(source, offset, entry, cancelled))
            return failure("RASTER_BIGTIFF_IFD", "A BigTIFF directory entry could not be read.");
        const uint16_t tag = u16(entry.data());
        const uint16_t type = u16(entry.data() + 2);
        const uint64_t count = u64(entry.data() + 4);
        const uint64_t raw = u64(entry.data() + 12);
        switch (tag) {
        case 256: if (!scalar(type, count, raw, width)) return failure("RASTER_BIGTIFF_TAG", "BigTIFF width is malformed."); break;
        case 257: if (!scalar(type, count, raw, height)) return failure("RASTER_BIGTIFF_TAG", "BigTIFF height is malformed."); break;
        case 258: bits_type = type; bits_value = raw; bits_count = count; break;
        case 259: if (!scalar(type, count, raw, compression)) return failure("RASTER_BIGTIFF_TAG", "BigTIFF compression is malformed."); break;
        case 262: if (!scalar(type, count, raw, photometric)) return failure("RASTER_BIGTIFF_TAG", "BigTIFF photometric mode is malformed."); break;
        case 273: if (!scalar(type, count, raw, strip_offset)) return failure("RASTER_BIGTIFF_TAG", "BigTIFF strip offset is malformed."); break;
        case 274: if (!scalar(type, count, raw, orientation)) return failure("RASTER_BIGTIFF_TAG", "BigTIFF orientation is malformed."); break;
        case 277: if (!scalar(type, count, raw, samples)) return failure("RASTER_BIGTIFF_TAG", "BigTIFF sample count is malformed."); break;
        case 278: if (!scalar(type, count, raw, rows_per_strip)) return failure("RASTER_BIGTIFF_TAG", "BigTIFF strip height is malformed."); break;
        case 279: if (!scalar(type, count, raw, strip_bytes)) return failure("RASTER_BIGTIFF_TAG", "BigTIFF strip size is malformed."); break;
        case 284: if (!scalar(type, count, raw, planar)) return failure("RASTER_BIGTIFF_TAG", "BigTIFF planar mode is malformed."); break;
        case 338: if (!scalar(type, count, raw, extra_sample)) return failure("RASTER_BIGTIFF_TAG", "BigTIFF alpha mode is malformed."); break;
        case 34675: has_icc_profile = true; break;
        default: break;
        }
    }
    std::array<uint8_t, 8> next_ifd_bytes{};
    const uint64_t next_ifd_pointer = ifd_offset + 8 + entry_count * 20;
    if (!RasterImageDecoder::read_exact(source, next_ifd_pointer,
                                        next_ifd_bytes, cancelled))
        return failure("RASTER_BIGTIFF_IFD", "The BigTIFF next-directory pointer could not be read.");
    if (u64(next_ifd_bytes.data()) != 0)
        return failure("RASTER_MULTIFRAME_UNSUPPORTED",
            "This BigTIFF decoder supports only a single image directory.");
    const bool supported_samples = samples == 3 || samples == 4;
    bool supported_bits = supported_samples && bits_type == 3 &&
        bits_count == samples && bits_count <= 4;
    for (uint64_t i = 0; supported_bits && i < bits_count; ++i) {
        const unsigned shift = static_cast<unsigned>((little ? i : 3 - i) * 16);
        supported_bits = ((bits_value >> shift) & 0xffffu) == 8;
    }
    if (width == 0 || height == 0 || !supported_bits || compression != 1 ||
        photometric != 2 || rows_per_strip < height || planar != 1 || orientation != 1 ||
        (samples == 4 && extra_sample != 2) || (samples == 3 && extra_sample != 0) ||
        has_icc_profile)
        return failure("RASTER_BIGTIFF_UNSUPPORTED",
            "BigTIFF profile mismatch: width=" + std::to_string(width) +
            ", height=" + std::to_string(height) + ", samples=" +
            std::to_string(samples) + ", bits-count=" + std::to_string(bits_count) +
            ", bits-type=" + std::to_string(bits_type) +
            ", compression=" + std::to_string(compression) +
            ", photometric=" + std::to_string(photometric) +
            ", rows-per-strip=" + std::to_string(rows_per_strip) +
            ", planar=" + std::to_string(planar) +
            ", orientation=" + std::to_string(orientation) +
            ", extra-sample=" + std::to_string(extra_sample) +
            (has_icc_profile ? ", ICC profile is outside the supported path." : ""));
    const RasterImageRegion region = requested_region.value_or(
        RasterImageRegion{0, 0, static_cast<uint32_t>(width),
                          static_cast<uint32_t>(height)});
    uint64_t pixel_count = 0, decoded_bytes = 0, row_bytes = 0, strip_required = 0;
    uint64_t encoded_region_bytes = 0, rgba_region_bytes = 0, row_scratch_bytes = 0;
    if (width > static_cast<uint64_t>(INT32_MAX) || height > static_cast<uint64_t>(INT32_MAX) ||
        region.width_px == 0 || region.height_px == 0 ||
        region.x_px > width || region.y_px > height ||
        region.width_px > width - region.x_px ||
        region.height_px > height - region.y_px ||
        !checked_mul(region.width_px, region.height_px, pixel_count) ||
        !checked_mul(pixel_count, 4, decoded_bytes) ||
        !checked_mul(width, samples, row_bytes) ||
        !checked_mul(row_bytes, height, strip_required) ||
        !checked_mul(region.width_px, samples, encoded_region_bytes) ||
        !checked_mul(region.width_px, 4, rgba_region_bytes) ||
        encoded_region_bytes > std::numeric_limits<size_t>::max() ||
        rgba_region_bytes > std::numeric_limits<size_t>::max() ||
        encoded_region_bytes > std::numeric_limits<uint64_t>::max() - rgba_region_bytes ||
        pixel_count > options.max_decoded_pixels || decoded_bytes > options.max_decoded_bytes ||
        strip_bytes < strip_required ||
        strip_offset > source.size_bytes() || strip_bytes > source.size_bytes() - strip_offset)
        return failure("RASTER_DECODED_BUDGET", "BigTIFF dimensions or strip bounds exceed the decode budget.");
    row_scratch_bytes = encoded_region_bytes + rgba_region_bytes;
    bool reservation_rejected = false;
    auto reservation = reserve_decode_memory(options, row_scratch_bytes, reservation_rejected);
    if (reservation_rejected)
        return failure("RASTER_MEMORY_BUDGET", "BigTIFF row buffers exceed the shared raster memory budget.");

    RasterImageData image;
    image.format = RasterImageFormat::Tiff;
    image.width_px = static_cast<uint32_t>(width);
    image.height_px = static_cast<uint32_t>(height);
    image.frame_count = 1;
    image.frame_index = options.frame_index;
    image.source_bits_per_pixel = static_cast<uint32_t>(samples * 8);
    image.exif_orientation = 1;
    image.orientation_applied = false;
    image.icc_profile_status = RasterIccProfileStatus::NotPresent;
    image.pixel_color_basis = RasterPixelColorBasis::AssumedSRgb;
    std::vector<uint8_t> encoded_row;
    std::vector<uint8_t> rgba;
    try {
        encoded_row.resize(static_cast<size_t>(encoded_region_bytes));
        rgba.resize(static_cast<size_t>(rgba_region_bytes));
    } catch (const std::bad_alloc&) {
        return failure("RASTER_ALLOCATION_FAILED", "BigTIFF row buffers could not be allocated.");
    }
    for (uint32_t row = 0; row < region.height_px; ++row) {
        const uint32_t y = region.y_px + row;
        const uint64_t source_offset = strip_offset + row_bytes * y +
            static_cast<uint64_t>(region.x_px) * samples;
        if (!RasterImageDecoder::read_exact(source, source_offset,
                                           encoded_row, cancelled))
            return failure(cancelled && *cancelled ? "RASTER_CANCELLED" : "RASTER_SOURCE_READ",
                "A BigTIFF pixel row could not be read.");
        for (uint32_t x = 0; x < region.width_px; ++x) {
            const size_t source_index = static_cast<size_t>(x) * samples;
            const size_t target_index = static_cast<size_t>(x) * 4;
            rgba[target_index] = encoded_row[source_index];
            rgba[target_index + 1] = encoded_row[source_index + 1];
            rgba[target_index + 2] = encoded_row[source_index + 2];
            rgba[target_index + 3] = samples == 4 ? encoded_row[source_index + 3] : 255;
        }
        if (!row_callback(image, y, region.width_px, rgba))
            return failure("RASTER_CANCELLED", "The BigTIFF row consumer stopped decoding.");
    }
    RasterImageDecodeResult result;
    result.success = true;
    result.image = std::move(image);
    return result;
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
                                        RasterImageData& image,
                                        ComOwner<IWICColorContext>& source_context) {
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
    source_context = std::move(contexts[0]);
    for (UINT index = 0; index < actual_count; ++index) {
        UINT byte_count = 0;
        const HRESULT size_result = source_context->GetProfileBytes(0, nullptr, &byte_count);
        if (FAILED(size_result) || byte_count == 0)
            return RasterIccProfileStatus::Unavailable;
        if (byte_count > max_bytes) return RasterIccProfileStatus::ExceedsBudget;
        std::vector<uint8_t> profile(byte_count);
        if (FAILED(source_context->GetProfileBytes(byte_count, profile.data(), &byte_count)) ||
            byte_count != profile.size())
            return RasterIccProfileStatus::Unavailable;
        image.embedded_icc_profile = std::move(profile);
        return RasterIccProfileStatus::Preserved;
    }
    return RasterIccProfileStatus::Unavailable;
}

bool transform_to_srgb(IWICImagingFactory* factory,
                       IWICBitmapSource* source,
                       UINT width,
                       UINT height,
                       IWICColorContext* source_context,
                       UINT stride,
                       RasterImageData& image) {
    if (!factory || !source || !source_context) return false;
    ComOwner<IWICColorContext> destination_context;
    if (FAILED(factory->CreateColorContext(destination_context.put())) ||
        !destination_context ||
        FAILED(destination_context->InitializeFromExifColorSpace(1)))
        return false;

    ComOwner<IWICColorTransform> transform;
    if (FAILED(factory->CreateColorTransformer(transform.put())) || !transform ||
        FAILED(transform->Initialize(source, source_context, destination_context.get(),
                                     GUID_WICPixelFormat32bppRGBA)))
        return false;
    if (!copy_wic_pixels_in_bands(transform.get(), width, height,
                                  stride, image.pixels_rgba))
        return false;
    image.pixel_color_basis = RasterPixelColorBasis::ConvertedToSRgb;
    return true;
}

uint32_t png_source_bits_per_pixel(std::span<const uint8_t> source) noexcept {
    if (source.size() < 29 || read_be32(source.data() + 8) != 13 ||
        std::memcmp(source.data() + 12, "IHDR", 4) != 0)
        return 0;
    const uint32_t bit_depth = source[24];
    uint32_t channels = 0;
    switch (source[25]) {
    case 0: channels = 1; break;
    case 2: channels = 3; break;
    case 3: channels = 1; break;
    case 4: channels = 2; break;
    case 6: channels = 4; break;
    default: return 0;
    }
    return bit_depth == 0 || bit_depth > 16 ? 0 : channels * bit_depth;
}

uint8_t png_source_bit_depth(std::span<const uint8_t> source) noexcept {
    constexpr uint8_t signature[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (source.size() < 25 || !std::equal(std::begin(signature), std::end(signature), source.begin()) ||
        std::memcmp(source.data() + 12, "IHDR", 4) != 0)
        return 0;
    return source[24];
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
         (source[0] == 'M' && source[1] == 'M' && source[2] == 0x00 && source[3] == 0x2A) ||
         (source[0] == 'I' && source[1] == 'I' && source[2] == 0x2B && source[3] == 0x00) ||
         (source[0] == 'M' && source[1] == 'M' && source[2] == 0x00 && source[3] == 0x2B)))
        return RasterImageFormat::Tiff;
    if (source.size() >= 14 && source[0] == 'B' && source[1] == 'M')
        return RasterImageFormat::Bmp;
    return RasterImageFormat::Unknown;
}

bool RasterImageDecoder::read_exact(const RasterByteSource& source,
                                    uint64_t offset,
                                    std::span<uint8_t> destination,
                                    const volatile bool* cancelled) noexcept {
    const uint64_t size = source.size_bytes();
    if (offset > size || destination.size() > size - offset) return false;
    if (cancelled && *cancelled) return false;
    size_t bytes_read = 0;
    if (!source.read_at(offset, destination, bytes_read) ||
        bytes_read != destination.size()) return false;
    return !(cancelled && *cancelled);
}

RasterImageDecodeResult RasterImageDecoder::decode_rows(
    const RasterByteSource& source,
    const RasterImageDecodeOptions& options,
    const RasterRowCallback& row_callback,
    const volatile bool* cancelled) {
    if (!row_callback)
        return failure("RASTER_ROW_CALLBACK_REQUIRED", "A row callback is required.");
    auto rgba8_options = options;
    rgba8_options.preserve_16bit = false;
    return decode_rows(source, rgba8_options,
        RasterImageRowCallback([&row_callback](const RasterImageData&, uint32_t y,
                                               uint32_t width,
                                               std::span<const uint8_t> row) {
            return row_callback(y, width, row);
        }), cancelled);
}

static RasterImageDecodeResult decode_rows_impl(
    const RasterByteSource& source,
    const RasterImageDecodeOptions& options,
    const std::optional<RasterImageRegion>& requested_region,
    const RasterImageRowCallback& row_callback,
    const volatile bool* cancelled) {
    if (!row_callback)
        return failure("RASTER_ROW_CALLBACK_REQUIRED", "A row callback is required.");
    if (cancelled && *cancelled)
        return failure("RASTER_CANCELLED", "Raster decoding was cancelled.");
    const uint64_t source_bytes = source.size_bytes();
    if (source_bytes == 0)
        return failure("RASTER_EMPTY_SOURCE", "The raster source is empty.");
    if (options.max_source_bytes == 0 || source_bytes > options.max_source_bytes)
        return failure("RASTER_SOURCE_LIMIT", "The raster source exceeds the configured byte limit.");
    if (options.max_decoded_pixels == 0 || options.max_decoded_bytes == 0 ||
        options.max_decoded_bytes > std::numeric_limits<uint32_t>::max() ||
        options.max_frames == 0 ||
        options.max_icc_profile_bytes > std::numeric_limits<uint32_t>::max())
        return failure("RASTER_INVALID_BUDGET", "The raster decode budget is invalid.");

    std::array<uint8_t, 29> source_prefix{};
    const size_t prefix_size = static_cast<size_t>((std::min)(
        source_bytes, static_cast<uint64_t>(source_prefix.size())));
    if (!RasterImageDecoder::read_exact(source, 0, std::span<uint8_t>(source_prefix).first(prefix_size), cancelled))
        return failure(cancelled && *cancelled ? "RASTER_CANCELLED" : "RASTER_SOURCE_READ",
            "The raster source header could not be read.");
    const auto prefix = std::span<const uint8_t>(source_prefix).first(prefix_size);
    const auto format = RasterImageDecoder::sniff_format(prefix);
    if (format == RasterImageFormat::Unknown)
        return failure("RASTER_SIGNATURE_UNSUPPORTED",
            "The file signature is not PNG, JPEG, TIFF or BMP.");
    if (format == RasterImageFormat::Tiff && prefix.size() >= 4 &&
        ((prefix[0] == 'I' && prefix[1] == 'I' && prefix[2] == 0x2b && prefix[3] == 0x00) ||
         (prefix[0] == 'M' && prefix[1] == 'M' && prefix[2] == 0x00 && prefix[3] == 0x2b)))
        return decode_bigtiff_uncompressed_rows(source, options, row_callback,
                                                cancelled, requested_region);

#ifndef _WIN32
    if (requested_region)
        return failure("RASTER_REGION_UNSUPPORTED",
            "Region decoding requires Windows Imaging Component for this raster format.");
#endif

#ifdef _WIN32
    auto& ole = Ole32Dll::get();
    auto& wic = WicDll::get();
    if (!ole.is_available() && !wic.create_factory)
        return failure("RASTER_WIC_UNAVAILABLE", "Windows Imaging Component is unavailable.");
    ComApartment apartment;
    if (!apartment.usable() && !wic.create_factory)
        return failure("RASTER_COM_INITIALIZATION",
            "Windows Imaging Component could not initialize COM.");

    ComOwner<IWICImagingFactory> factory;
    if (wic.create_factory) {
        (void)wic.create_factory(0x0237 /* WINCODEC_SDK_VERSION2 */, factory.put());
        if (!factory) (void)wic.create_factory(0x0236 /* WINCODEC_SDK_VERSION1 */, factory.put());
    }
    if (!factory) {
        if (FAILED(ole.co_create(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(factory.put()))) || !factory) {
            constexpr GUID kClsidWicImagingFactory2 = {
                0x317d06e8, 0x5f24, 0x433d, {0xbd, 0xf7, 0x79, 0xce, 0x68, 0xd8, 0xab, 0xc2}};
            if (FAILED(ole.co_create(kClsidWicImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_PPV_ARGS(factory.put()))) || !factory)
                return failure("RASTER_WIC_UNAVAILABLE",
                    "Windows Imaging Component is unavailable.");
        }
    }

    ComOwner<IStream> stream;
    stream.reset(new (std::nothrow) RasterByteSourceStream(source));
    if (!stream)
        return failure("RASTER_STREAM_INVALID", "A read-only raster stream could not be created.");
    ComOwner<IWICBitmapDecoder> decoder;
    HRESULT decoder_result = factory->CreateDecoderFromStream(
        stream.get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.put());
    if (FAILED(decoder_result) || !decoder) {
        const GUID clsid = decoder_class(format);
        if (!IsEqualGUID(clsid, GUID_NULL)) {
            (void)ole.co_create(clsid, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(decoder.put()));
            if (decoder)
                (void)decoder->Initialize(stream.get(), WICDecodeMetadataCacheOnDemand);
        }
    }
    if (!decoder)
        return failure("RASTER_DECODE_FAILED",
            "The selected Windows raster decoder rejected this stream.");

    GUID actual_container{};
    if (FAILED(decoder->GetContainerFormat(&actual_container)) ||
        !IsEqualGUID(actual_container, container_format(format)))
        return failure("RASTER_CONTAINER_MISMATCH",
            "The decoded container does not match its signature.");
    UINT frame_count = 0;
    if (FAILED(decoder->GetFrameCount(&frame_count)) || frame_count == 0 ||
        frame_count > options.max_frames)
        return failure("RASTER_FRAME_LIMIT",
            "The raster frame count is empty or exceeds the configured limit.");
    if (options.frame_index >= frame_count)
        return failure("RASTER_FRAME_NOT_FOUND", "The requested raster frame does not exist.");

    ComOwner<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(options.frame_index, frame.put())) || !frame)
        return failure("RASTER_FRAME_DECODE_FAILED",
            "The requested raster frame could not be decoded.");
    UINT width = 0, height = 0;
    GUID source_pixel_format{};
    double dpi_x = 96.0, dpi_y = 96.0;
    if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0 ||
        FAILED(frame->GetPixelFormat(&source_pixel_format)) ||
        FAILED(frame->GetResolution(&dpi_x, &dpi_y)) || !std::isfinite(dpi_x) ||
        !std::isfinite(dpi_y) || dpi_x <= 0 || dpi_y <= 0)
        return failure("RASTER_FRAME_METADATA",
            "The selected frame has invalid dimensions or resolution.");

    uint32_t bits_per_pixel = format == RasterImageFormat::Png
        ? png_source_bits_per_pixel(prefix) : 0;
    if (bits_per_pixel == 0 &&
        !source_bits_per_pixel(factory.get(), source_pixel_format, bits_per_pixel))
        bits_per_pixel = 32;
    const uint16_t orientation = read_orientation(frame.get(), format);
    if (orientation != 1 && !options.apply_orientation)
        return failure("RASTER_ORIENTATION_REQUIRES_CHOICE",
            "The source has a non-default EXIF orientation; choose whether to apply it before import.");

    const bool request_16bit = options.preserve_16bit &&
        ((format == RasterImageFormat::Png && png_source_bit_depth(prefix) == 16) ||
         bits_per_pixel == 16 || bits_per_pixel >= 48);
    uint64_t full_pixel_count = 0;
    if (!checked_mul(width, height, full_pixel_count))
        return failure("RASTER_DECODED_BUDGET",
            "Decoded raster dimensions exceed the configured pixel or memory budget.");

    ComOwner<IWICBitmapSource> oriented_source;
    IWICBitmapSource* selected_source = frame.get();
    ComOwner<IWICBitmapFlipRotator> rotator;
    const bool orientation_applied = orientation != 1;
    if (orientation_applied) {
        if (FAILED(factory->CreateBitmapFlipRotator(rotator.put())) || !rotator ||
            FAILED(rotator->Initialize(frame.get(), orientation_transform(orientation))) ||
            FAILED(rotator->QueryInterface(IID_PPV_ARGS(oriented_source.put()))) || !oriented_source)
            return failure("RASTER_ORIENTATION_FAILED",
                "The EXIF orientation transform could not be applied.");
        selected_source = oriented_source.get();
    }
    UINT output_width = 0, output_height = 0;
    if (FAILED(selected_source->GetSize(&output_width, &output_height)) ||
        output_width == 0 || output_height == 0 ||
        output_width > static_cast<UINT>(std::numeric_limits<INT>::max()) ||
        output_height > static_cast<UINT>(std::numeric_limits<INT>::max()) ||
        static_cast<uint64_t>(output_width) * output_height != full_pixel_count)
        return failure("RASTER_OUTPUT_DIMENSIONS",
            "The oriented raster dimensions are invalid for row decoding.");
    const RasterImageRegion region = requested_region.value_or(
        RasterImageRegion{0, 0, output_width, output_height});
    uint64_t decoded_pixel_count = 0, output_bytes = 0;
    if (region.width_px == 0 || region.height_px == 0 ||
        region.x_px > output_width || region.y_px > output_height ||
        region.width_px > output_width - region.x_px ||
        region.height_px > output_height - region.y_px ||
        !checked_mul(region.width_px, region.height_px, decoded_pixel_count) ||
        decoded_pixel_count > options.max_decoded_pixels ||
        !checked_mul(decoded_pixel_count, request_16bit ? 8 : 4, output_bytes) ||
        output_bytes > options.max_decoded_bytes)
        return failure("RASTER_DECODED_BUDGET",
            "Decoded raster region exceeds the configured pixel or memory budget.");
    uint64_t stride64 = 0, stride16_64 = 0;
    if (!checked_mul(region.width_px, 4, stride64) ||
        !checked_mul(region.width_px, 8, stride16_64) ||
        stride64 > std::numeric_limits<UINT>::max() ||
        stride16_64 > std::numeric_limits<UINT>::max())
        return failure("RASTER_DECODED_BUDGET",
            "The raster row exceeds the WIC addressable stride.");
    const UINT stride = static_cast<UINT>(stride64);
    const UINT stride16 = static_cast<UINT>(stride16_64);

    RasterImageData image;
    image.format = format;
    image.width_px = output_width;
    image.height_px = output_height;
    image.frame_count = frame_count;
    image.frame_index = options.frame_index;
    image.source_bits_per_pixel = bits_per_pixel;
    image.decoded_bits_per_channel = 8;
    image.exif_orientation = orientation;
    image.orientation_applied = orientation_applied;
    image.dpi_x = dpi_x;
    image.dpi_y = dpi_y;
    ComOwner<IWICColorContext> embedded_profile_context;
    image.icc_profile_status = read_icc_profile(
        factory.get(), frame.get(), options.max_icc_profile_bytes,
        image, embedded_profile_context);
    if (format == RasterImageFormat::Png && !image.has_embedded_icc_profile) {
        uint64_t offset = 8;
        while (offset <= source_bytes && source_bytes - offset >= 12) {
            std::array<uint8_t, 8> chunk{};
            if (!RasterImageDecoder::read_exact(source, offset, chunk, cancelled))
                return failure(cancelled && *cancelled ? "RASTER_CANCELLED" : "RASTER_SOURCE_READ",
                    "PNG color metadata could not be read from the source stream.");
            const uint32_t payload_bytes = read_be32(chunk.data());
            if (payload_bytes > source_bytes - offset - 12) break;
            if (std::memcmp(chunk.data() + 4, "iCCP", 4) == 0) {
                image.has_embedded_icc_profile = true;
                if (image.icc_profile_status == RasterIccProfileStatus::NotPresent)
                    image.icc_profile_status = RasterIccProfileStatus::Unavailable;
                break;
            }
            if (std::memcmp(chunk.data() + 4, "IEND", 4) == 0) break;
            offset += static_cast<uint64_t>(payload_bytes) + 12;
        }
    }
    if (image.has_embedded_icc_profile)
        image.pixel_color_basis = RasterPixelColorBasis::EmbeddedProfileUnapplied;

    bool consumer_stopped = false;
    bool scratch_budget_rejected = false;
    const RasterRowCallback guarded_callback = [&](uint32_t y, uint32_t row_width,
                                                   std::span<const uint8_t> row) {
        if (cancelled && *cancelled) return false;
        if (!row_callback(image, y, row_width, row)) {
            consumer_stopped = true;
            return false;
        }
        return true;
    };
    if (embedded_profile_context) {
        if (request_16bit) {
            ComOwner<IWICColorContext> destination16;
            ComOwner<IWICColorTransform> transform16;
            if (SUCCEEDED(factory->CreateColorContext(destination16.put())) && destination16 &&
                SUCCEEDED(destination16->InitializeFromExifColorSpace(1)) &&
                SUCCEEDED(factory->CreateColorTransformer(transform16.put())) && transform16 &&
                SUCCEEDED(transform16->Initialize(selected_source, embedded_profile_context.get(),
                    destination16.get(), GUID_WICPixelFormat64bppRGBA))) {
                image.decoded_bits_per_channel = 16;
                image.pixel_color_basis = RasterPixelColorBasis::ConvertedToSRgb;
                if (!copy_wic_rows_in_bands(transform16.get(), region.width_px, region.height_px,
                        stride16, guarded_callback, cancelled, false, false,
                        options.memory_budget, scratch_budget_rejected,
                        region.x_px, region.y_px)) {
                    return failure(scratch_budget_rejected ? "RASTER_SCRATCH_BUDGET" :
                        (consumer_stopped || (cancelled && *cancelled))
                            ? "RASTER_CANCELLED" : "RASTER_PIXEL_COPY",
                        scratch_budget_rejected
                            ? "WIC row scratch exceeds the shared raster memory budget."
                            : (consumer_stopped || (cancelled && *cancelled))
                            ? "Raster row decoding was cancelled."
                            : "WIC could not stream 16-bit ICC-converted rows from the source.");
                }
                RasterImageDecodeResult result;
                result.success = true;
                result.image = std::move(image);
                return result;
            }
        }
        ComOwner<IWICColorContext> destination_context;
        ComOwner<IWICColorTransform> transform;
        if (SUCCEEDED(factory->CreateColorContext(destination_context.put())) &&
            destination_context &&
            SUCCEEDED(destination_context->InitializeFromExifColorSpace(1)) &&
            SUCCEEDED(factory->CreateColorTransformer(transform.put())) && transform &&
            SUCCEEDED(transform->Initialize(selected_source, embedded_profile_context.get(),
                                            destination_context.get(),
                                            GUID_WICPixelFormat32bppRGBA))) {
            // Row consumers receive this metadata while CopyPixels is still
            // producing the transformed rows, so publish the output basis first.
            image.pixel_color_basis = RasterPixelColorBasis::ConvertedToSRgb;
            if (!copy_wic_rows_in_bands(transform.get(), region.width_px, region.height_px,
                    stride, guarded_callback, cancelled, false, false,
                    options.memory_budget, scratch_budget_rejected,
                    region.x_px, region.y_px)) {
                return failure(scratch_budget_rejected ? "RASTER_SCRATCH_BUDGET" :
                    (consumer_stopped || (cancelled && *cancelled))
                        ? "RASTER_CANCELLED" : "RASTER_PIXEL_COPY",
                    scratch_budget_rejected
                        ? "WIC row scratch exceeds the shared raster memory budget."
                        : (consumer_stopped || (cancelled && *cancelled))
                        ? "Raster row decoding was cancelled."
                        : "WIC could not stream ICC-converted rows from the source.");
            }
            RasterImageDecodeResult result;
            result.success = true;
            result.image = std::move(image);
            return result;
        }
    }

    ComOwner<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(converter.put())) || !converter)
        return failure("RASTER_WIC_UNAVAILABLE",
            "Windows Imaging Component format converter unavailable.");
    if (request_16bit) {
        ComOwner<IWICFormatConverter> converter16;
        BOOL can_convert16 = FALSE;
        if (SUCCEEDED(factory->CreateFormatConverter(converter16.put())) && converter16 &&
            SUCCEEDED(converter16->CanConvert(source_pixel_format,
                GUID_WICPixelFormat64bppRGBA, &can_convert16)) && can_convert16 &&
            SUCCEEDED(converter16->Initialize(selected_source, GUID_WICPixelFormat64bppRGBA,
                WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
            image.decoded_bits_per_channel = 16;
            if (!copy_wic_rows_in_bands(converter16.get(), region.width_px, region.height_px,
                    stride16, guarded_callback, cancelled, false, false,
                    options.memory_budget, scratch_budget_rejected,
                    region.x_px, region.y_px)) {
                return failure(scratch_budget_rejected ? "RASTER_SCRATCH_BUDGET" :
                    (consumer_stopped || (cancelled && *cancelled))
                        ? "RASTER_CANCELLED" : "RASTER_PIXEL_COPY",
                    scratch_budget_rejected
                        ? "WIC row scratch exceeds the shared raster memory budget."
                        : (consumer_stopped || (cancelled && *cancelled))
                        ? "Raster row decoding was cancelled."
                        : "WIC could not stream 16-bit RGBA rows from the source.");
            }
            RasterImageDecodeResult result;
            result.success = true;
            result.image = std::move(image);
            return result;
        }
    }
    bool need_bgr_swap = false;
    bool need_unpremultiply = false;
    BOOL can_convert = FALSE;
    HRESULT convert_result = converter->CanConvert(
        source_pixel_format, GUID_WICPixelFormat32bppRGBA, &can_convert);
    if (SUCCEEDED(convert_result) && can_convert) {
        convert_result = converter->Initialize(selected_source, GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    } else {
        can_convert = FALSE;
    }
    if (!can_convert || FAILED(convert_result)) {
        BOOL can_convert_bgra = FALSE;
        if (SUCCEEDED(converter->CanConvert(source_pixel_format,
                GUID_WICPixelFormat32bppBGRA, &can_convert_bgra)) && can_convert_bgra) {
            convert_result = converter->Initialize(selected_source, GUID_WICPixelFormat32bppBGRA,
                WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
            if (SUCCEEDED(convert_result)) {
                need_bgr_swap = true;
                can_convert = TRUE;
            }
        }
    }
    if (!can_convert || FAILED(convert_result)) {
        BOOL can_convert_pbgra = FALSE;
        if (SUCCEEDED(converter->CanConvert(source_pixel_format,
                GUID_WICPixelFormat32bppPBGRA, &can_convert_pbgra)) && can_convert_pbgra) {
            convert_result = converter->Initialize(selected_source, GUID_WICPixelFormat32bppPBGRA,
                WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
            if (SUCCEEDED(convert_result)) {
                need_bgr_swap = true;
                need_unpremultiply = true;
                can_convert = TRUE;
            }
        }
    }
    ComOwner<IWICFormatConverter> intermediate_converter;
    if (!can_convert || FAILED(convert_result)) {
        const GUID intermediate_formats[]{GUID_WICPixelFormat64bppRGBA,
            GUID_WICPixelFormat24bppBGR, GUID_WICPixelFormat32bppBGR};
        for (const auto& intermediate_format : intermediate_formats) {
            BOOL can_intermediate = FALSE;
            if (SUCCEEDED(converter->CanConvert(source_pixel_format,
                    intermediate_format, &can_intermediate)) && can_intermediate &&
                SUCCEEDED(converter->Initialize(selected_source, intermediate_format,
                    WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)) &&
                SUCCEEDED(factory->CreateFormatConverter(intermediate_converter.put())) &&
                intermediate_converter) {
                BOOL can_final = FALSE;
                if (SUCCEEDED(intermediate_converter->CanConvert(
                        intermediate_format, GUID_WICPixelFormat32bppRGBA, &can_final)) &&
                    can_final && SUCCEEDED(intermediate_converter->Initialize(converter.get(),
                        GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                        nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
                    can_convert = TRUE;
                    need_bgr_swap = false;
                    need_unpremultiply = false;
                    convert_result = S_OK;
                    break;
                }
            }
        }
    }
    IWICBitmapSource* output_source = intermediate_converter
        ? static_cast<IWICBitmapSource*>(intermediate_converter.get())
        : static_cast<IWICBitmapSource*>(converter.get());
    if (!can_convert || FAILED(convert_result) || !output_source)
        return failure("RASTER_PIXEL_CONVERSION",
            "The source pixel format cannot be converted to bounded RGBA8 rows.");
    if (!copy_wic_rows_in_bands(output_source, region.width_px, region.height_px, stride,
            guarded_callback, cancelled, need_bgr_swap, need_unpremultiply,
            options.memory_budget, scratch_budget_rejected,
            region.x_px, region.y_px))
        return failure(scratch_budget_rejected ? "RASTER_SCRATCH_BUDGET" :
            (consumer_stopped || (cancelled && *cancelled))
                ? "RASTER_CANCELLED" : "RASTER_PIXEL_COPY",
            scratch_budget_rejected
                ? "WIC row scratch exceeds the shared raster memory budget."
                : (consumer_stopped || (cancelled && *cancelled))
                ? "Raster row decoding was cancelled."
                : "WIC could not stream converted rows from the source.");

    RasterImageDecodeResult result;
    result.success = true;
    result.image = std::move(image);
    return result;
#else
    if (source_bytes > std::numeric_limits<size_t>::max() ||
        source_bytes > std::numeric_limits<uint32_t>::max())
        return failure("RASTER_STREAM_CODEC_UNAVAILABLE",
            "This platform cannot decode a raster source larger than its addressable span.");
    bool source_budget_rejected = false;
    [[maybe_unused]] auto source_reservation = reserve_decode_memory(
        options, source_bytes, source_budget_rejected);
    if (source_budget_rejected)
        return failure("RASTER_MEMORY_BUDGET", "Raster source staging exceeds the shared memory budget.");
    std::vector<uint8_t> bytes(static_cast<size_t>(source_bytes));
    if (!read_exact(source, 0, bytes, cancelled))
        return failure(cancelled && *cancelled ? "RASTER_CANCELLED" : "RASTER_SOURCE_READ",
            "The raster source could not be read completely.");
    auto decoded = decode(bytes, options);
    if (!decoded.success) return decoded;
    const size_t bytes_per_pixel = decoded.image.decoded_bits_per_channel == 16 ? 8 : 4;
    const size_t stride = static_cast<size_t>(decoded.image.width_px) * bytes_per_pixel;
    const auto& decoded_pixels = decoded.image.decoded_bits_per_channel == 16
        ? decoded.image.pixels_rgba16le : decoded.image.pixels_rgba;
    for (uint32_t y = 0; y < decoded.image.height_px; ++y) {
        if (cancelled && *cancelled)
            return failure("RASTER_CANCELLED", "Raster decoding was cancelled.");
        const auto row = std::span<const uint8_t>(decoded_pixels)
            .subspan(static_cast<size_t>(y) * stride, stride);
        if (!row_callback(decoded.image, y, decoded.image.width_px, row))
            return failure("RASTER_CANCELLED", "Raster row consumer stopped decoding.");
    }
    decoded.image.pixels_rgba.clear();
    decoded.image.pixels_rgba.shrink_to_fit();
    decoded.image.pixels_rgba16le.clear();
    decoded.image.pixels_rgba16le.shrink_to_fit();
    return decoded;
#endif
}

RasterImageDecodeResult RasterImageDecoder::decode_rows(
    const RasterByteSource& source, const RasterImageDecodeOptions& options,
    const RasterImageRowCallback& row_callback,
    const volatile bool* cancelled) {
    return decode_rows_impl(source, options, std::nullopt, row_callback, cancelled);
}

RasterImageDecodeResult RasterImageDecoder::decode_region_rows(
    const RasterByteSource& source, const RasterImageDecodeOptions& options,
    RasterImageRegion region, const RasterImageRowCallback& row_callback,
    const volatile bool* cancelled) {
    return decode_rows_impl(source, options, region, row_callback, cancelled);
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
        return failure("RASTER_SIGNATURE_UNSUPPORTED", "The file signature is not PNG, JPEG, TIFF or BMP.");

#ifdef _WIN32
    if (options.preserve_16bit) {
        class SpanSource final : public RasterByteSource {
        public:
            explicit SpanSource(std::span<const uint8_t> bytes) : bytes_(bytes) {}
            uint64_t size_bytes() const noexcept override { return bytes_.size(); }
            bool read_at(uint64_t offset, std::span<uint8_t> destination,
                         size_t& bytes_read) const noexcept override {
                bytes_read = 0;
                if (offset > bytes_.size()) return false;
                const size_t count = static_cast<size_t>((std::min)(
                    static_cast<uint64_t>(destination.size()),
                    static_cast<uint64_t>(bytes_.size()) - offset));
                std::copy_n(bytes_.data() + static_cast<size_t>(offset), count,
                            destination.data());
                bytes_read = count;
                return true;
            }
        private:
            std::span<const uint8_t> bytes_;
        } byte_source(source);

        RasterImageData collected;
        std::string collection_error;
        uint32_t next_row = 0;
        bool initialized = false;
        const auto streamed = decode_rows(byte_source, options,
            RasterImageRowCallback([&](const RasterImageData& metadata, uint32_t y,
                                       uint32_t width, std::span<const uint8_t> row) {
                try {
                    if (y != next_row || width != metadata.width_px ||
                        (metadata.decoded_bits_per_channel != 8 &&
                         metadata.decoded_bits_per_channel != 16)) {
                        collection_error = "WIC returned an invalid row for full-frame collection.";
                        return false;
                    }
                    const uint64_t pixels = static_cast<uint64_t>(metadata.width_px) *
                        metadata.height_px;
                    const uint64_t bytes_per_pixel =
                        metadata.decoded_bits_per_channel == 16 ? 12u : 4u;
                    if (pixels == 0 || pixels > UINT64_MAX / bytes_per_pixel ||
                        pixels > SIZE_MAX / bytes_per_pixel) {
                        collection_error = "Collected raster dimensions exceed addressable memory.";
                        return false;
                    }
                    const uint64_t output_bytes = pixels * bytes_per_pixel;
                    if (output_bytes > options.max_decoded_bytes) {
                        collection_error = "Collected raster pixels exceed the decoded-byte budget.";
                        return false;
                    }
                    if (!initialized) {
                        bool rejected = false;
                        collected = metadata;
                        collected.pixel_memory_reservation = reserve_decode_memory(
                            options, output_bytes, rejected);
                        if (rejected) {
                            collection_error = "Collected raster pixels exceed the shared memory budget.";
                            return false;
                        }
                        collected.pixels_rgba.resize(static_cast<size_t>(pixels) * 4);
                        if (metadata.decoded_bits_per_channel == 16)
                            collected.pixels_rgba16le.resize(static_cast<size_t>(pixels) * 8);
                        initialized = true;
                    }
                    if (metadata.decoded_bits_per_channel == 16) {
                        const size_t stride = static_cast<size_t>(width) * 8;
                        if (row.size() != stride) {
                            collection_error = "WIC returned an invalid RGBA16LE row size.";
                            return false;
                        }
                        auto* target = collected.pixels_rgba16le.data() +
                            static_cast<size_t>(y) * stride;
                        std::copy(row.begin(), row.end(), target);
                        auto* preview = collected.pixels_rgba.data() +
                            static_cast<size_t>(y) * width * 4;
                        for (size_t sample = 0; sample < static_cast<size_t>(width) * 4; ++sample)
                            preview[sample] = row[sample * 2 + 1];
                    } else {
                        const size_t stride = static_cast<size_t>(width) * 4;
                        if (row.size() != stride) {
                            collection_error = "WIC returned an invalid RGBA8 row size.";
                            return false;
                        }
                        std::copy(row.begin(), row.end(), collected.pixels_rgba.begin() +
                            static_cast<size_t>(y) * stride);
                    }
                    ++next_row;
                    return true;
                } catch (...) {
                    collection_error = "Full-frame raster collection allocation failed.";
                    return false;
                }
            }));
        if (!streamed.success) {
            if (!collection_error.empty())
                return failure("RASTER_DECODED_BUDGET", std::move(collection_error));
            return streamed;
        }
        if (!initialized || next_row != collected.height_px)
            return failure("RASTER_PIXEL_COPY", "WIC did not return the complete raster frame.");
        RasterImageDecodeResult result;
        result.success = true;
        result.image = std::move(collected);
        return result;
    }
    bool source_has_png_iccp = false;
#endif
    if (format == RasterImageFormat::Png) {
#ifndef _WIN32
        return decode_png_pure(source, options);
#else
        size_t chunk_offset = 8;
        while (chunk_offset + 12 <= source.size()) {
            const uint32_t chunk_bytes = read_be32(source.data() + chunk_offset);
            if (chunk_bytes > source.size() - chunk_offset - 12) break;
            if (std::memcmp(source.data() + chunk_offset + 4, "iCCP", 4) == 0) {
                source_has_png_iccp = true;
                break;
            }
            if (std::memcmp(source.data() + chunk_offset + 4, "IEND", 4) == 0) break;
            chunk_offset += 12 + chunk_bytes;
        }
#endif
    }

#ifdef _WIN32
    auto& ole = Ole32Dll::get();
    auto& wic = WicDll::get();
    if (!ole.is_available() && !wic.create_factory)
        return failure("RASTER_WIC_UNAVAILABLE", "Windows Imaging Component is unavailable.");

    ComApartment apartment;
    if (!apartment.usable() && !wic.create_factory)
        return failure("RASTER_COM_INITIALIZATION", "Windows Imaging Component could not initialize COM.");

    ComOwner<IWICImagingFactory> factory;
    if (wic.create_factory) {
        (void)wic.create_factory(0x0237 /* WINCODEC_SDK_VERSION2 */, factory.put());
        if (!factory) {
            (void)wic.create_factory(0x0236 /* WINCODEC_SDK_VERSION1 */, factory.put());
        }
    }
    if (!factory) {
        if (FAILED(ole.co_create(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(factory.put()))) || !factory) {
            constexpr GUID kClsidWicImagingFactory2 = {
                0x317d06e8, 0x5f24, 0x433d, {0xbd, 0xf7, 0x79, 0xce, 0x68, 0xd8, 0xab, 0xc2}};
            if (FAILED(ole.co_create(kClsidWicImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_PPV_ARGS(factory.put()))) || !factory) {
                return failure("RASTER_WIC_UNAVAILABLE", "Windows Imaging Component is unavailable.");
            }
        }
    }

    // WIC's memory stream accepts mutable memory, so use a private bounded copy of the source.
    bool source_budget_rejected = false;
    [[maybe_unused]] auto source_copy_reservation = reserve_decode_memory(
        options, source.size(), source_budget_rejected);
    if (source_budget_rejected)
        return failure("RASTER_MEMORY_BUDGET", "Raster source staging exceeds the shared memory budget.");
    std::vector<uint8_t> source_copy(source.begin(), source.end());
    ComOwner<IWICStream> stream;
    if (FAILED(factory->CreateStream(stream.put())) || !stream ||
        FAILED(stream->InitializeFromMemory(source_copy.data(),
            static_cast<DWORD>(source_copy.size()))))
        return failure("RASTER_STREAM_INVALID", "The source bytes could not be staged for WIC.");

    ComOwner<IWICBitmapDecoder> decoder;
    HRESULT hr_dec = factory->CreateDecoderFromStream(
        stream.get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.put());
    if (FAILED(hr_dec) || !decoder) {
        const GUID clsid = decoder_class(format);
        if (!IsEqualGUID(clsid, GUID_NULL)) {
            (void)ole.co_create(clsid, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(decoder.put()));
            if (decoder) {
                (void)decoder->Initialize(stream.get(), WICDecodeMetadataCacheOnDemand);
            }
        }
    }
    if (!decoder)
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

    uint32_t bits_per_pixel = format == RasterImageFormat::Png
        ? png_source_bits_per_pixel(source) : 0;
    if (bits_per_pixel == 0 &&
        !source_bits_per_pixel(factory.get(), source_pixel_format, bits_per_pixel)) {
        bits_per_pixel = 32;
    }

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
    bool output_budget_rejected = false;
    image.pixel_memory_reservation = reserve_decode_memory(
        options, output_bytes, output_budget_rejected);
    if (output_budget_rejected)
        return failure("RASTER_MEMORY_BUDGET", "Decoded pixels exceed the shared raster memory budget.");
    image.pixels_rgba.resize(static_cast<size_t>(output_bytes));
    ComOwner<IWICColorContext> embedded_profile_context;
    image.icc_profile_status = read_icc_profile(
        factory.get(), frame.get(), options.max_icc_profile_bytes, image,
        embedded_profile_context);
    if (source_has_png_iccp && !image.has_embedded_icc_profile) {
        image.has_embedded_icc_profile = true;
        if (image.icc_profile_status == RasterIccProfileStatus::NotPresent)
            image.icc_profile_status = RasterIccProfileStatus::Unavailable;
    }
    if (image.has_embedded_icc_profile)
        image.pixel_color_basis = RasterPixelColorBasis::EmbeddedProfileUnapplied;
    const bool icc_transformed = embedded_profile_context && transform_to_srgb(
        factory.get(), selected_source, output_width, output_height,
        embedded_profile_context.get(), static_cast<UINT>(stride), image);

    if (!icc_transformed) {
        ComOwner<IWICFormatConverter> converter;
        if (FAILED(factory->CreateFormatConverter(converter.put())) || !converter)
            return failure("RASTER_WIC_UNAVAILABLE", "Windows Imaging Component format converter unavailable.");

        bool need_bgr_swap = false;
        bool need_unpremultiply = false;
        BOOL can_convert = FALSE;
        HRESULT hr_convert = converter->CanConvert(source_pixel_format, GUID_WICPixelFormat32bppRGBA, &can_convert);
        if (SUCCEEDED(hr_convert) && can_convert) {
            hr_convert = converter->Initialize(selected_source, GUID_WICPixelFormat32bppRGBA,
                WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
        } else {
            can_convert = FALSE;
        }

        if (!can_convert || FAILED(hr_convert)) {
            BOOL can_convert_bgra = FALSE;
            if (SUCCEEDED(converter->CanConvert(source_pixel_format, GUID_WICPixelFormat32bppBGRA, &can_convert_bgra)) && can_convert_bgra) {
                hr_convert = converter->Initialize(selected_source, GUID_WICPixelFormat32bppBGRA,
                    WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
                if (SUCCEEDED(hr_convert)) {
                    need_bgr_swap = true;
                    can_convert = TRUE;
                }
            }
        }

        if (!can_convert || FAILED(hr_convert)) {
            BOOL can_convert_pbgra = FALSE;
            if (SUCCEEDED(converter->CanConvert(source_pixel_format, GUID_WICPixelFormat32bppPBGRA, &can_convert_pbgra)) && can_convert_pbgra) {
                hr_convert = converter->Initialize(selected_source, GUID_WICPixelFormat32bppPBGRA,
                    WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
                if (SUCCEEDED(hr_convert)) {
                    need_bgr_swap = true;
                    need_unpremultiply = true;
                    can_convert = TRUE;
                }
            }
        }

        ComOwner<IWICFormatConverter> intermediate_converter;
        if (!can_convert || FAILED(hr_convert)) {
            const GUID intermediate_formats[]{
                GUID_WICPixelFormat64bppRGBA,
                GUID_WICPixelFormat24bppBGR,
                GUID_WICPixelFormat32bppBGR
            };
            for (const auto& inter_fmt : intermediate_formats) {
                BOOL can_inter = FALSE;
                if (SUCCEEDED(converter->CanConvert(source_pixel_format, inter_fmt, &can_inter)) && can_inter) {
                    if (SUCCEEDED(converter->Initialize(selected_source, inter_fmt,
                            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
                        if (SUCCEEDED(factory->CreateFormatConverter(intermediate_converter.put())) && intermediate_converter) {
                            BOOL can_final = FALSE;
                            if (SUCCEEDED(intermediate_converter->CanConvert(inter_fmt, GUID_WICPixelFormat32bppRGBA, &can_final)) && can_final &&
                                SUCCEEDED(intermediate_converter->Initialize(converter.get(), GUID_WICPixelFormat32bppRGBA,
                                    WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
                                can_convert = TRUE;
                                need_bgr_swap = false;
                                need_unpremultiply = false;
                                hr_convert = S_OK;
                                break;
                            }
                        }
                    }
                }
            }
        }

        IWICBitmapSource* output_source = intermediate_converter ? intermediate_converter.get() : converter.get();
        if (!can_convert || FAILED(hr_convert) || !output_source)
            return failure("RASTER_PIXEL_CONVERSION", "The source pixel format cannot be converted to bounded RGBA8 pixels.");
        if (!copy_wic_pixels_in_bands(output_source, output_width, output_height,
                static_cast<UINT>(stride), image.pixels_rgba))
            return failure("RASTER_PIXEL_COPY", "WIC could not copy the converted pixels within the configured budget.");

        if (need_bgr_swap || need_unpremultiply) {
            for (size_t i = 0; i < image.pixels_rgba.size(); i += 4) {
                if (need_bgr_swap)
                    std::swap(image.pixels_rgba[i], image.pixels_rgba[i + 2]);
                // PBGRA already contains alpha-multiplied channels. The host
                // stores straight RGBA; leaving them premultiplied darkens
                // translucent colors when the renderer multiplies again.
                if (need_unpremultiply) {
                    const uint32_t alpha = image.pixels_rgba[i + 3];
                    for (size_t channel = 0; channel < 3; ++channel) {
                        const uint32_t value = image.pixels_rgba[i + channel];
                        image.pixels_rgba[i + channel] = alpha == 0 ? 0 :
                            static_cast<uint8_t>(std::min(255u, (value * 255u + alpha / 2) / alpha));
                    }
                }
            }
        }
    }

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
