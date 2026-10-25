#include "pikdraw/color/preview/profile_color_transform.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <icm.h>
#endif

namespace pikdraw::color {
struct ProfileColorTransform::Impl {
    std::shared_ptr<const ProfileResource> source;
    std::shared_ptr<const ProfileResource> destination;
    IccTransformOptions options;
    mutable std::atomic<uint64_t> failures{0};
#ifdef _WIN32
    HTRANSFORM transform{nullptr};
    HTRANSFORM gamut_transform{nullptr};
    mutable std::mutex mutex;
    ~Impl() {
        if (transform) DeleteColorTransform(transform);
        if (gamut_transform) DeleteColorTransform(gamut_transform);
    }
#endif
};

ProfileColorTransform::ProfileColorTransform(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}
ProfileColorTransform::~ProfileColorTransform() = default;

const ProfileResource& ProfileColorTransform::source_profile() const noexcept {
    return *impl_->source;
}
const ProfileResource& ProfileColorTransform::destination_profile() const noexcept {
    return *impl_->destination;
}
std::shared_ptr<const ProfileResource> ProfileColorTransform::source_resource() const noexcept {
    return impl_->source;
}
std::shared_ptr<const ProfileResource> ProfileColorTransform::destination_resource() const noexcept {
    return impl_->destination;
}
IccTransformOptions ProfileColorTransform::options() const noexcept { return impl_->options; }
uint64_t ProfileColorTransform::failure_count() const noexcept {
    return impl_->failures.load(std::memory_order_relaxed);
}

#ifdef _WIN32
namespace {
struct ProfileHandle {
    HPROFILE value{nullptr};
    ProfileHandle(const ProfileResource& resource, DWORD expected_space) {
        const auto& bytes = resource.bytes();
        if (bytes.empty() || bytes.size() > std::numeric_limits<DWORD>::max())
            throw std::invalid_argument("ICC resource size is invalid");
        PROFILE profile{};
        profile.dwType = PROFILE_MEMBUFFER;
        profile.pProfileData = const_cast<uint8_t*>(bytes.data());
        profile.cbDataSize = static_cast<DWORD>(bytes.size());
        value = OpenColorProfileW(&profile, PROFILE_READ, FILE_SHARE_READ, OPEN_EXISTING);
        PROFILEHEADER header{};
        BOOL valid = FALSE;
        if (!value || !IsColorProfileValid(value, &valid) || !valid ||
            !GetColorProfileHeader(value, &header) || header.phDataColorSpace != expected_space) {
            if (value) CloseColorProfile(value);
            value = nullptr;
            throw std::invalid_argument("ICC transform profile is invalid for its declared color space");
        }
    }
    ~ProfileHandle() { if (value) CloseColorProfile(value); }
};

DWORD profile_space(ColorSpace space) {
    switch (space) {
    case ColorSpace::Rgb: return SPACE_RGB;
    case ColorSpace::Cmyk: return SPACE_CMYK;
    case ColorSpace::Spot:
    case ColorSpace::Grayscale: break;
    }
    throw std::invalid_argument("Only RGB and CMYK ICC profiles are supported by this transform");
}

DWORD rendering_intent(IccRenderingIntent intent) {
    switch (intent) {
    case IccRenderingIntent::Perceptual: return INTENT_PERCEPTUAL;
    case IccRenderingIntent::RelativeColorimetric: return INTENT_RELATIVE_COLORIMETRIC;
    case IccRenderingIntent::Saturation: return INTENT_SATURATION;
    case IccRenderingIntent::AbsoluteColorimetric: return INTENT_ABSOLUTE_COLORIMETRIC;
    }
    throw std::invalid_argument("Invalid ICC rendering intent");
}

WORD channel(double value) noexcept {
    return static_cast<WORD>(std::lround(std::clamp(value, 0.0, 1.0) * 65535.0));
}
} // namespace
#endif

ProfileColorTransformResult ProfileColorTransform::create(
    std::shared_ptr<const ProfileResource> source,
    std::shared_ptr<const ProfileResource> destination,
    IccTransformOptions options) {
    if (!source || !destination)
        return {nullptr, "Source and destination ICC profiles are required"};
    const auto source_space = source->identity().color_space;
    const auto destination_space = destination->identity().color_space;
    if (source_space != ColorSpace::Rgb && source_space != ColorSpace::Cmyk)
        return {nullptr, "Source transform profile must be RGB or CMYK"};
    if (destination_space != ColorSpace::Rgb && destination_space != ColorSpace::Cmyk)
        return {nullptr, "Destination transform profile must be RGB or CMYK"};
    if (source->identity() == destination->identity())
        return {nullptr, "Identical profile resources require no conversion"};
    if (options.black_point_compensation)
        return {nullptr, "The Windows ICC backend does not expose black point compensation"};
#ifdef _WIN32
    try {
        const DWORD intent = rendering_intent(options.intent);
        ProfileHandle input(*source, profile_space(source_space));
        ProfileHandle output(*destination, profile_space(destination_space));
        HPROFILE profiles[] = {input.value, output.value};
        DWORD intents[] = {intent, intent};
        auto impl = std::make_unique<Impl>();
        impl->source = std::move(source);
        impl->destination = std::move(destination);
        impl->options = options;
        const auto transform = CreateMultiProfileTransform(
            profiles, 2, intents, 2, BEST_MODE, INDEX_DONT_CARE);
        if (!transform)
            return {nullptr, "Windows CMM could not create the requested ICC transform"};
        impl->transform = transform;
        const auto gamut_transform = CreateMultiProfileTransform(
            profiles, 2, intents, 2, ENABLE_GAMUT_CHECKING | BEST_MODE,
            INDEX_DONT_CARE);
        impl->gamut_transform = gamut_transform;
        return {std::shared_ptr<const ProfileColorTransform>(
            new ProfileColorTransform(std::move(impl))), {}};
    } catch (const std::exception& error) {
        return {nullptr, error.what()};
    }
#else
    (void)options;
    return {nullptr, "ICC profile conversion is currently available on Windows only"};
#endif
}

bool ProfileColorTransform::convert(const ColorValue& source, ColorValue& destination) const noexcept {
#ifdef _WIN32
    if (!source.is_valid() || source.color_space() != impl_->source->identity().color_space ||
        (source.profile_resource() && source.profile_resource()->identity() !=
                                          impl_->source->identity()))
        return false;
    COLOR input{}, output{};
    COLORTYPE input_type = COLOR_RGB;
    COLORTYPE output_type = COLOR_RGB;
    double alpha = 1.0;
    if (const auto* rgb = std::get_if<RGBColor>(&source.payload())) {
        input.rgb = {channel(rgb->r), channel(rgb->g), channel(rgb->b)};
        alpha = rgb->a;
        input_type = COLOR_RGB;
    } else if (const auto* cmyk = std::get_if<CMYKColor>(&source.payload())) {
        input.cmyk = {channel(cmyk->c), channel(cmyk->m), channel(cmyk->y), channel(cmyk->k)};
        alpha = cmyk->a;
        input_type = COLOR_CMYK;
    } else {
        return false;
    }
    if (impl_->destination->identity().color_space == ColorSpace::Rgb)
        output_type = COLOR_RGB;
    else if (impl_->destination->identity().color_space == ColorSpace::Cmyk)
        output_type = COLOR_CMYK;
    else
        return false;
    std::lock_guard lock(impl_->mutex);
    if (!TranslateColors(impl_->transform, &input, 1, input_type, &output, output_type)) {
        impl_->failures.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (output_type == COLOR_RGB) {
        destination = ColorValue{RGBColor{output.rgb.red / 65535.0,
            output.rgb.green / 65535.0, output.rgb.blue / 65535.0, alpha}};
    } else {
        destination = ColorValue{CMYKColor{output.cmyk.cyan / 65535.0,
            output.cmyk.magenta / 65535.0, output.cmyk.yellow / 65535.0,
            output.cmyk.black / 65535.0, alpha}};
    }
    destination.set_overprint(source.overprint());
    if (source.profile_resource() &&
        !destination.set_profile_resource(impl_->destination))
        return false;
    return true;
#else
    (void)source;
    (void)destination;
    return false;
#endif
}

std::optional<bool> ProfileColorTransform::is_in_gamut(
    const ColorValue& source) const noexcept {
#ifdef _WIN32
    if (!source.is_valid() ||
        source.color_space() != impl_->source->identity().color_space ||
        (source.profile_resource() &&
         source.profile_resource()->identity() != impl_->source->identity()))
        return std::nullopt;
    COLOR input{};
    COLORTYPE input_type = COLOR_RGB;
    if (const auto* rgb = std::get_if<RGBColor>(&source.payload())) {
        input.rgb = {channel(rgb->r), channel(rgb->g), channel(rgb->b)};
        input_type = COLOR_RGB;
    } else if (const auto* cmyk = std::get_if<CMYKColor>(&source.payload())) {
        input.cmyk = {channel(cmyk->c), channel(cmyk->m), channel(cmyk->y),
                      channel(cmyk->k)};
        input_type = COLOR_CMYK;
    } else {
        return std::nullopt;
    }
    BYTE result = 0;
    std::lock_guard lock(impl_->mutex);
    if (!impl_->gamut_transform ||
        !CheckColors(impl_->gamut_transform, &input, 1, input_type, &result))
        return std::nullopt;
    return result == 0;
#else
    (void)source;
    return std::nullopt;
#endif
}

bool ProfileColorTransform::convert_rgb8(
    std::span<const uint8_t> source_rgba,
    std::span<uint8_t> destination_rgba) const noexcept {
    if (source_rgba.size() != destination_rgba.size() ||
        (source_rgba.size() % 4u) != 0u ||
        impl_->source->identity().color_space != ColorSpace::Rgb ||
        impl_->destination->identity().color_space != ColorSpace::Rgb)
        return false;
    if (!source_rgba.empty() && source_rgba.data() != destination_rgba.data()) {
        const auto source_address = reinterpret_cast<uintptr_t>(source_rgba.data());
        const auto destination_address = reinterpret_cast<uintptr_t>(destination_rgba.data());
        const auto distance = source_address < destination_address
            ? destination_address - source_address : source_address - destination_address;
        if (distance < source_rgba.size()) return false;
    }
#ifdef _WIN32
    constexpr size_t kPixelsPerBatch = 8192;
    try {
        std::array<COLOR, kPixelsPerBatch> input{};
        std::array<COLOR, kPixelsPerBatch> output{};
        std::lock_guard lock(impl_->mutex);
        const size_t pixel_count = source_rgba.size() / 4u;
        for (size_t offset = 0; offset < pixel_count;) {
            const size_t count = (std::min)(kPixelsPerBatch, pixel_count - offset);
            for (size_t index = 0; index < count; ++index) {
                const size_t source = (offset + index) * 4u;
                input[index].rgb.red = static_cast<WORD>(source_rgba[source] * 257u);
                input[index].rgb.green = static_cast<WORD>(source_rgba[source + 1] * 257u);
                input[index].rgb.blue = static_cast<WORD>(source_rgba[source + 2] * 257u);
            }
            if (!TranslateColors(impl_->transform, input.data(),
                    static_cast<DWORD>(count), COLOR_RGB, output.data(), COLOR_RGB)) {
                impl_->failures.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            for (size_t index = 0; index < count; ++index) {
                const size_t destination = (offset + index) * 4u;
                const auto to_byte = [](WORD value) noexcept {
                    return static_cast<uint8_t>((static_cast<uint32_t>(value) + 128u) / 257u);
                };
                destination_rgba[destination] = to_byte(output[index].rgb.red);
                destination_rgba[destination + 1] = to_byte(output[index].rgb.green);
                destination_rgba[destination + 2] = to_byte(output[index].rgb.blue);
                destination_rgba[destination + 3] = source_rgba[destination + 3];
            }
            offset += count;
        }
        return true;
    } catch (...) {
        impl_->failures.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
#else
    (void)source_rgba;
    (void)destination_rgba;
    return false;
#endif
}

bool ProfileColorTransform::convert_process16le(
    std::span<const uint8_t> source_samples,
    std::span<uint8_t> destination_samples,
    bool source_has_alpha, bool destination_has_alpha,
    const IccTransformExecutionOptions& execution) const noexcept {
    const auto source_space = impl_->source->identity().color_space;
    const auto destination_space = impl_->destination->identity().color_space;
    const size_t source_channels = source_space == ColorSpace::Rgb ? 3u :
        source_space == ColorSpace::Cmyk ? 4u : 0u;
    const size_t destination_channels = destination_space == ColorSpace::Rgb ? 3u :
        destination_space == ColorSpace::Cmyk ? 4u : 0u;
    if (source_channels == 0 || destination_channels == 0 ||
        (source_has_alpha && !destination_has_alpha))
        return false;

    const size_t source_bytes_per_pixel =
        (source_channels + (source_has_alpha ? 1u : 0u)) * 2u;
    const size_t destination_bytes_per_pixel =
        (destination_channels + (destination_has_alpha ? 1u : 0u)) * 2u;
    if (source_samples.size() % source_bytes_per_pixel != 0)
        return false;
    const size_t pixel_count = source_samples.size() / source_bytes_per_pixel;
    if (pixel_count > std::numeric_limits<size_t>::max() /
            destination_bytes_per_pixel ||
        destination_samples.size() != pixel_count * destination_bytes_per_pixel)
        return false;
    if (!source_samples.empty()) {
        if (source_samples.data() == destination_samples.data()) {
            if (source_samples.size() != destination_samples.size()) return false;
        } else {
            const auto source_address =
                reinterpret_cast<uintptr_t>(source_samples.data());
            const auto destination_address =
                reinterpret_cast<uintptr_t>(destination_samples.data());
            if (source_address < destination_address) {
                if (destination_address - source_address < source_samples.size())
                    return false;
            } else if (source_address - destination_address <
                       destination_samples.size()) {
                return false;
            }
        }
    }
    if (pixel_count == 0) return true;
#ifdef _WIN32
    constexpr size_t kPixelsPerBatch = 8192;
    const auto read_sample = [](std::span<const uint8_t> bytes,
                                size_t offset) noexcept {
        return static_cast<WORD>(static_cast<uint16_t>(bytes[offset]) |
            (static_cast<uint16_t>(bytes[offset + 1]) << 8));
    };
    const auto write_sample = [](std::span<uint8_t> bytes, size_t offset,
                                 WORD value) noexcept {
        bytes[offset] = static_cast<uint8_t>(value & 0xffu);
        bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
    };
    try {
        std::array<COLOR, kPixelsPerBatch> input{};
        std::array<COLOR, kPixelsPerBatch> output{};
        for (size_t offset = 0; offset < pixel_count;) {
            if (execution.cancelled && execution.cancelled()) return false;
            const size_t count = (std::min)(kPixelsPerBatch,
                                             pixel_count - offset);
            for (size_t index = 0; index < count; ++index) {
                const size_t source = (offset + index) * source_bytes_per_pixel;
                if (source_space == ColorSpace::Rgb) {
                    input[index].rgb.red = read_sample(source_samples, source);
                    input[index].rgb.green = read_sample(source_samples, source + 2);
                    input[index].rgb.blue = read_sample(source_samples, source + 4);
                } else {
                    input[index].cmyk.cyan = read_sample(source_samples, source);
                    input[index].cmyk.magenta = read_sample(source_samples, source + 2);
                    input[index].cmyk.yellow = read_sample(source_samples, source + 4);
                    input[index].cmyk.black = read_sample(source_samples, source + 6);
                }
            }
            const COLORTYPE input_type = source_space == ColorSpace::Rgb
                ? COLOR_RGB : COLOR_CMYK;
            const COLORTYPE output_type = destination_space == ColorSpace::Rgb
                ? COLOR_RGB : COLOR_CMYK;
            {
                std::lock_guard lock(impl_->mutex);
                if (!TranslateColors(impl_->transform, input.data(),
                        static_cast<DWORD>(count), input_type, output.data(),
                        output_type)) {
                    impl_->failures.fetch_add(1, std::memory_order_relaxed);
                    return false;
                }
            }
            for (size_t index = 0; index < count; ++index) {
                const size_t source = (offset + index) * source_bytes_per_pixel;
                const size_t destination =
                    (offset + index) * destination_bytes_per_pixel;
                if (destination_space == ColorSpace::Rgb) {
                    write_sample(destination_samples, destination,
                                 output[index].rgb.red);
                    write_sample(destination_samples, destination + 2,
                                 output[index].rgb.green);
                    write_sample(destination_samples, destination + 4,
                                 output[index].rgb.blue);
                } else {
                    write_sample(destination_samples, destination,
                                 output[index].cmyk.cyan);
                    write_sample(destination_samples, destination + 2,
                                 output[index].cmyk.magenta);
                    write_sample(destination_samples, destination + 4,
                                 output[index].cmyk.yellow);
                    write_sample(destination_samples, destination + 6,
                                 output[index].cmyk.black);
                }
                if (destination_has_alpha) {
                    const WORD alpha = source_has_alpha
                        ? read_sample(source_samples,
                            source + source_channels * 2u)
                        : static_cast<WORD>(65535u);
                    write_sample(destination_samples,
                        destination + destination_channels * 2u, alpha);
                }
            }
            offset += count;
            if (execution.progress)
                execution.progress(static_cast<uint64_t>(offset),
                                   static_cast<uint64_t>(pixel_count));
        }
        return true;
    } catch (...) {
        impl_->failures.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
#else
    (void)source_samples;
    (void)destination_samples;
    (void)source_has_alpha;
    (void)destination_has_alpha;
    (void)execution;
    return false;
#endif
}

} // namespace pikdraw::color
