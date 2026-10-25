#include "pikdraw/color/preview/icc_preview.hpp"
#include "pikdraw/color/preview/bounded_lru_cache.hpp"
#include "pikdraw/color/preview/profile_color_transform.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// The Windows SDK requires Win32 types before icm.h.
#include <icm.h>
#endif

namespace pikdraw::color {
struct IccPreview::Impl {
    mutable std::atomic<uint64_t> failures{0};
#ifdef _WIN32
    HTRANSFORM rgb{nullptr}, cmyk{nullptr}, to_cmyk{nullptr}, to_rgb{nullptr};
    HTRANSFORM gamut_to_cmyk{nullptr};
    std::shared_ptr<const ProfileResource> rgb_source;
    std::shared_ptr<const ProfileResource> cmyk_source;
    std::shared_ptr<const ProfileResource> display_profile;
    mutable std::mutex mutex;
    mutable PreviewOptions options;
    struct PreviewKeyHasher {
        size_t operator()(const std::array<WORD, 5>& key) const noexcept {
            size_t hash = 0;
            for (const auto component : key)
                hash = (hash * 131u) ^ static_cast<size_t>(component);
            return hash;
        }
    };
    mutable BoundedLruCache<std::array<WORD, 5>, PreviewRGBA, PreviewKeyHasher> cache{4096};
    struct CachedProfileTransform {
        std::shared_ptr<const ProfileColorTransform> transform;
        size_t profile_bytes{0};
    };
    mutable std::unordered_map<std::string, CachedProfileTransform> profile_transforms;
    mutable std::deque<std::string> profile_transform_lru;
    mutable size_t profile_transform_bytes{0};

    std::shared_ptr<const ProfileColorTransform> profile_transform(
        const std::shared_ptr<const ProfileResource>& source,
        const std::shared_ptr<const ProfileResource>& destination,
        IccTransformOptions options = {}) const {
        if (!source || !destination || source->identity() == destination->identity())
            return {};
        const auto key = std::to_string(static_cast<unsigned>(source->identity().color_space)) + ":" +
            source->identity().checksum_sha256 + ">" +
            std::to_string(static_cast<unsigned>(destination->identity().color_space)) + ":" +
            destination->identity().checksum_sha256 + ":" +
            std::to_string(static_cast<unsigned>(options.intent)) + ":" +
            (options.black_point_compensation ? "1" : "0");
        const auto touch = [&](const std::string& value) {
            const auto found = std::find(profile_transform_lru.begin(), profile_transform_lru.end(), value);
            if (found != profile_transform_lru.end()) profile_transform_lru.erase(found);
            profile_transform_lru.push_back(value);
        };
        {
            std::lock_guard lock(mutex);
            const auto found = profile_transforms.find(key);
            if (found != profile_transforms.end()) {
                touch(key);
                return found->second.transform;
            }
        }

        auto created = ProfileColorTransform::create(source, destination, options);
        if (!created) return {};
        const size_t bytes = source->bytes().size() + destination->bytes().size();
        constexpr size_t max_entries = 16;
        constexpr size_t max_profile_bytes = 64 * 1024 * 1024;
        if (bytes > max_profile_bytes) return created.transform;
        std::lock_guard lock(mutex);
        if (const auto found = profile_transforms.find(key); found != profile_transforms.end()) {
            touch(key);
            return found->second.transform;
        }
        while (!profile_transform_lru.empty() &&
               (profile_transforms.size() >= max_entries ||
                profile_transform_bytes + bytes > max_profile_bytes)) {
            const auto oldest = std::move(profile_transform_lru.front());
            profile_transform_lru.pop_front();
            const auto found = profile_transforms.find(oldest);
            if (found == profile_transforms.end()) continue;
            profile_transform_bytes -= found->second.profile_bytes;
            profile_transforms.erase(found);
        }
        if (profile_transforms.size() < max_entries &&
            profile_transform_bytes + bytes <= max_profile_bytes) {
            profile_transforms.emplace(key, CachedProfileTransform{created.transform, bytes});
            profile_transform_lru.push_back(key);
        }
        return created.transform;
    }

    ~Impl() {
        if (rgb)
            DeleteColorTransform(rgb);
        if (cmyk)
            DeleteColorTransform(cmyk);
        if (to_cmyk) DeleteColorTransform(to_cmyk);
        if (to_rgb) DeleteColorTransform(to_rgb);
        if (gamut_to_cmyk) DeleteColorTransform(gamut_to_cmyk);
    }
#endif
};
IccPreview::IccPreview(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
IccPreview::~IccPreview() = default;

std::filesystem::path IccPreview::system_srgb_profile() {
#ifdef _WIN32
    std::array<wchar_t, 32768> buffer{};
    DWORD bytes = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
    if (GetStandardColorSpaceProfileW(nullptr, LCS_sRGB, buffer.data(), &bytes))
        return buffer.data();
#endif
    return {};
}
#ifdef _WIN32
namespace {
struct ProfileHandle {
    HPROFILE value{nullptr};
    ProfileHandle(const ProfileResource& resource, DWORD expected_space) {
        const auto& bytes = resource.bytes();
        if (bytes.empty() || bytes.size() > std::numeric_limits<DWORD>::max())
            throw std::invalid_argument("ICC resource size is invalid");
        PROFILE source{};
        source.dwType = PROFILE_MEMBUFFER;
        source.pProfileData = const_cast<std::uint8_t *>(bytes.data());
        source.cbDataSize = static_cast<DWORD>(bytes.size());
        value = OpenColorProfileW(&source, PROFILE_READ, FILE_SHARE_READ, OPEN_EXISTING);
        PROFILEHEADER header{};
        BOOL valid = FALSE;
        if (!value || !IsColorProfileValid(value, &valid) || !valid ||
            !GetColorProfileHeader(value, &header) || header.phDataColorSpace != expected_space) {
            if (value)
                CloseColorProfile(value);
            value = nullptr;
            throw std::invalid_argument(
                "Cannot read ICC profile, or its RGB/CMYK channels do not match this slot");
        }
    }
    ~ProfileHandle() {
        if (value)
            CloseColorProfile(value);
    }
};
HTRANSFORM make_transform(const ProfileResource& source, DWORD expected,
                          const ProfileResource& destination, DWORD output_space = SPACE_RGB) {
    ProfileHandle input(source, expected), output(destination, output_space);
    HPROFILE profiles[] = {input.value, output.value};
    DWORD intents[] = {INTENT_RELATIVE_COLORIMETRIC, INTENT_RELATIVE_COLORIMETRIC};
    const auto transform = CreateMultiProfileTransform(profiles, 2, intents, 2, BEST_MODE, INDEX_DONT_CARE);
    if (!transform)
        throw std::runtime_error("Windows color engine could not create this ICC transform");
    return transform;
}
WORD channel(double value) {
    return static_cast<WORD>(std::lround(std::clamp(value, 0.0, 1.0) * 65535));
}
} // namespace
#endif
std::shared_ptr<const IccPreview> IccPreview::create(const std::filesystem::path &rgb_source,
                                                     const std::filesystem::path &cmyk_source,
                                                     std::string *error) {
    if (error)
        error->clear();
#ifdef _WIN32
    std::shared_ptr<const ProfileResource> rgb_resource;
    std::shared_ptr<const ProfileResource> cmyk_resource;
    if (!rgb_source.empty()) {
        auto loaded = ProfileResource::load_file(rgb_source, ColorSpace::Rgb);
        if (!loaded) {
            if (error) *error = "RGB ICC file failed resource validation";
            return nullptr;
        }
        rgb_resource = std::move(loaded.resource);
    }
    if (!cmyk_source.empty()) {
        auto loaded = ProfileResource::load_file(cmyk_source, ColorSpace::Cmyk);
        if (!loaded) {
            if (error) *error = "CMYK ICC file failed resource validation";
            return nullptr;
        }
        cmyk_resource = std::move(loaded.resource);
    }
    return create_from_resources(std::move(rgb_resource), std::move(cmyk_resource), error);
#else
    (void)rgb_source;
    (void)cmyk_source;
    if (error)
        *error = "ICC preview is currently available on Windows only";
#endif
    return nullptr;
}

std::shared_ptr<const IccPreview> IccPreview::create_from_resources(
    std::shared_ptr<const ProfileResource> rgb_source,
    std::shared_ptr<const ProfileResource> cmyk_source, std::string *error) {
    return create_from_resources(std::move(rgb_source), std::move(cmyk_source),
                                 {}, error);
}

std::shared_ptr<const IccPreview> IccPreview::create_from_resources(
    std::shared_ptr<const ProfileResource> rgb_source,
    std::shared_ptr<const ProfileResource> cmyk_source,
    std::shared_ptr<const ProfileResource> display_destination,
    std::string *error) {
    if (error) error->clear();
#ifdef _WIN32
    try {
        if (!display_destination) {
            const auto display_path = system_srgb_profile();
            if (display_path.empty())
                throw std::runtime_error("System sRGB ICC profile is unavailable");
            auto display_result = ProfileResource::load_file(
                display_path, ColorSpace::Rgb);
            if (!display_result)
                throw std::runtime_error("System sRGB ICC resource failed validation");
            display_destination = std::move(display_result.resource);
        }
        if (!rgb_source) rgb_source = display_destination;
        if (rgb_source->identity().color_space != ColorSpace::Rgb ||
            display_destination->identity().color_space != ColorSpace::Rgb ||
            (cmyk_source && cmyk_source->identity().color_space != ColorSpace::Cmyk))
            throw std::invalid_argument("ICC resource color space does not match its preview slot");

        auto impl = std::make_unique<Impl>();
        impl->rgb_source = rgb_source;
        impl->cmyk_source = cmyk_source;
        impl->display_profile = display_destination;
        impl->rgb = make_transform(*rgb_source, SPACE_RGB, *display_destination);
        if (cmyk_source) {
            impl->cmyk = make_transform(*cmyk_source, SPACE_CMYK,
                                        *display_destination);
            impl->to_cmyk = make_transform(*rgb_source, SPACE_RGB, *cmyk_source, SPACE_CMYK);
            impl->to_rgb = make_transform(*cmyk_source, SPACE_CMYK, *rgb_source);
            ProfileHandle gamut_rgb(*rgb_source, SPACE_RGB);
            ProfileHandle gamut_cmyk(*cmyk_source, SPACE_CMYK);
            HPROFILE gamut_profiles[] = {gamut_rgb.value, gamut_cmyk.value};
            DWORD gamut_intents[] = {INTENT_RELATIVE_COLORIMETRIC,
                                     INTENT_RELATIVE_COLORIMETRIC};
            impl->gamut_to_cmyk = CreateMultiProfileTransform(
                gamut_profiles, 2, gamut_intents, 2,
                ENABLE_GAMUT_CHECKING | BEST_MODE, INDEX_DONT_CARE);
        }
        return std::shared_ptr<const IccPreview>(new IccPreview(std::move(impl)));
    } catch (const std::exception& failure) {
        if (error) *error = failure.what();
    }
#else
    (void)rgb_source;
    (void)cmyk_source;
    (void)display_destination;
    if (error) *error = "ICC preview is currently available on Windows only";
#endif
    return nullptr;
}

bool IccPreview::has_cmyk_transform() const noexcept {
#ifdef _WIN32
    return impl_->cmyk != nullptr;
#else
    return false;
#endif
}
bool IccPreview::supports_proofing(IccRenderingIntent intent) const noexcept {
#ifdef _WIN32
    try {
        if (!impl_->rgb_source || !impl_->cmyk_source || !impl_->display_profile)
            return false;
        const IccTransformOptions options{intent, false};
        return impl_->profile_transform(impl_->rgb_source,
                                        impl_->cmyk_source, options) &&
               impl_->profile_transform(impl_->cmyk_source,
                                        impl_->display_profile, options);
    } catch (...) {
        return false;
    }
#else
    (void)intent;
    return false;
#endif
}
uint64_t IccPreview::conversion_failure_count() const noexcept {
    return impl_->failures.load(std::memory_order_relaxed);
}
bool IccPreview::convert_process(const ColorValue& source, DocumentColorMode target,
                                 ColorValue& result) const noexcept {
#ifdef _WIN32
    if (!source.is_valid()) return false;
    const auto& configured_source = source.color_space() == ColorSpace::Rgb
        ? impl_->rgb_source : impl_->cmyk_source;
    if (source.profile_resource() &&
        (!configured_source || source.profile_resource()->identity() !=
                                   configured_source->identity()))
        return false;
    COLOR input{}, output{};
    HTRANSFORM transform = nullptr;
    COLORTYPE input_type = COLOR_RGB, output_type = COLOR_CMYK;
    double alpha = 1;
    if (target == DocumentColorMode::Cmyk) {
        const auto* rgb = std::get_if<RGBColor>(&source.payload());
        if (!rgb || !impl_->to_cmyk) return false;
        input.rgb = {channel(rgb->r), channel(rgb->g), channel(rgb->b)};
        alpha = rgb->a;
        transform = impl_->to_cmyk;
    } else {
        const auto* cmyk = std::get_if<CMYKColor>(&source.payload());
        if (!cmyk || !impl_->to_rgb) return false;
        input.cmyk = {channel(cmyk->c), channel(cmyk->m), channel(cmyk->y), channel(cmyk->k)};
        alpha = cmyk->a;
        input_type = COLOR_CMYK;
        output_type = COLOR_RGB;
        transform = impl_->to_rgb;
    }
    std::lock_guard lock(impl_->mutex);
    if (!TranslateColors(transform, &input, 1, input_type, &output, output_type)) {
        impl_->failures.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    result = target == DocumentColorMode::Cmyk
        ? ColorValue{CMYKColor{output.cmyk.cyan / 65535.0, output.cmyk.magenta / 65535.0,
                               output.cmyk.yellow / 65535.0, output.cmyk.black / 65535.0, alpha}}
        : ColorValue{RGBColor{output.rgb.red / 65535.0, output.rgb.green / 65535.0,
                              output.rgb.blue / 65535.0, alpha}};
    result.set_overprint(source.overprint());
    if (source.profile_resource()) {
        const auto& destination_profile = target == DocumentColorMode::Cmyk
            ? impl_->cmyk_source : impl_->rgb_source;
        if (!destination_profile || !result.set_profile_resource(destination_profile))
            return false;
    }
    return true;
#else
    (void)source; (void)target; (void)result;
    return false;
#endif
}

void IccPreview::set_preview_options(const PreviewOptions& options) const noexcept {
#ifdef _WIN32
    std::lock_guard lock(impl_->mutex);
    impl_->options = options;
    impl_->cache.clear();
#else
    (void)options;
#endif
}

PreviewOptions IccPreview::preview_options() const noexcept {
#ifdef _WIN32
    std::lock_guard lock(impl_->mutex);
    return impl_->options;
#else
    return {};
#endif
}

bool IccPreview::has_large_rgb_roundtrip_difference(const ColorValue &source) const noexcept {
#ifdef _WIN32
    if (!source.is_valid() || !impl_->cmyk_source) return false;
    const auto* rgb = std::get_if<RGBColor>(&source.payload());
    if (!rgb) return false;

    const auto source_profile = source.profile_resource()
        ? source.profile_resource() : impl_->rgb_source;
    if (!source_profile) return false;
    ColorValue roundtrip;
    if (source_profile->identity() != impl_->rgb_source->identity()) {
        try {
            const auto to_cmyk = impl_->profile_transform(source_profile, impl_->cmyk_source);
            const auto to_rgb = impl_->profile_transform(impl_->cmyk_source, source_profile);
            ColorValue cmyk;
            if (!to_cmyk || !to_cmyk->convert(source, cmyk) ||
                !to_rgb || !to_rgb->convert(cmyk, roundtrip))
                return false;
        } catch (...) {
            impl_->failures.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        const auto* returned = std::get_if<RGBColor>(&roundtrip.payload());
        if (!returned) return false;
        const double dr = rgb->r - returned->r;
        const double dg = rgb->g - returned->g;
        const double db = rgb->b - returned->b;
        return std::sqrt(dr * dr + dg * dg + db * db) > 0.12;
    }
    if (!impl_->to_cmyk || !impl_->to_rgb) return false;

    COLOR input{}, cmyk_mid{}, roundtrip_rgb{};
    input.rgb = {channel(rgb->r), channel(rgb->g), channel(rgb->b)};

    std::lock_guard lock(impl_->mutex);
    if (!TranslateColors(impl_->to_cmyk, &input, 1, COLOR_RGB, &cmyk_mid, COLOR_CMYK)) {
        return false;
    }
    if (!TranslateColors(impl_->to_rgb, &cmyk_mid, 1, COLOR_CMYK, &roundtrip_rgb, COLOR_RGB)) {
        return false;
    }
    const double r_diff = (input.rgb.red - roundtrip_rgb.rgb.red) / 65535.0;
    const double g_diff = (input.rgb.green - roundtrip_rgb.rgb.green) / 65535.0;
    const double b_diff = (input.rgb.blue - roundtrip_rgb.rgb.blue) / 65535.0;
    const double distance = std::sqrt(r_diff * r_diff + g_diff * g_diff + b_diff * b_diff);
    // This is a UI diagnostic threshold in encoded RGB space, not Delta E.
    return distance > 0.12;
#else
    (void)source;
    return false;
#endif
}

bool IccPreview::supports_gamut_check() const noexcept {
#ifdef _WIN32
    if (!impl_->gamut_to_cmyk) return false;
    COLOR sample{};
    sample.rgb = {32768, 32768, 32768};
    BYTE result = 0;
    std::lock_guard lock(impl_->mutex);
    return CheckColors(impl_->gamut_to_cmyk, &sample, 1, COLOR_RGB, &result);
#else
    return false;
#endif
}

std::optional<bool> IccPreview::is_out_of_gamut(
    const ColorValue& source) const noexcept {
#ifdef _WIN32
    if (!source.is_valid() || !std::holds_alternative<RGBColor>(source.payload()) ||
        !impl_->cmyk_source)
        return std::nullopt;
    const auto source_profile = source.profile_resource()
        ? source.profile_resource() : impl_->rgb_source;
    if (!source_profile || source_profile->identity().color_space != ColorSpace::Rgb)
        return std::nullopt;
    if (source_profile->identity() != impl_->rgb_source->identity()) {
        const auto transform = impl_->profile_transform(source_profile, impl_->cmyk_source);
        if (!transform) return std::nullopt;
        const auto in_gamut = transform->is_in_gamut(source);
        if (!in_gamut) return std::nullopt;
        return !*in_gamut;
    }

    const auto& rgb = std::get<RGBColor>(source.payload());
    COLOR input{};
    input.rgb = {channel(rgb.r), channel(rgb.g), channel(rgb.b)};
    BYTE result = 0;
    std::lock_guard lock(impl_->mutex);
    if (!impl_->gamut_to_cmyk ||
        !CheckColors(impl_->gamut_to_cmyk, &input, 1, COLOR_RGB, &result))
        return std::nullopt;
    return result != 0;
#else
    (void)source;
    return std::nullopt;
#endif
}

PreviewRGBA IccPreview::convert(const ColorValue &source) const noexcept {
    return convert(source, preview_options());
}

PreviewRGBA IccPreview::convert(const ColorValue &source, const PreviewOptions &options) const noexcept {
    auto fallback = to_preview_rgba(source);
#ifdef _WIN32
    if (!source.is_valid())
        return fallback;

    const auto& configured_source = source.color_space() == ColorSpace::Rgb
        ? impl_->rgb_source : impl_->cmyk_source;
    const bool custom_bound_source = source.profile_resource() &&
        (!configured_source || source.profile_resource()->identity() != configured_source->identity());
    if (custom_bound_source) {
        try {
            const auto source_profile = source.profile_resource();
            if (source_profile->identity().color_space != ColorSpace::Rgb &&
                source_profile->identity().color_space != ColorSpace::Cmyk) {
                impl_->failures.fetch_add(1, std::memory_order_relaxed);
                return fallback;
            }
            if (source_profile->identity() == impl_->display_profile->identity() &&
                !options.proof_colors)
                return fallback;
            if (options.gamut_warning && is_out_of_gamut(source).value_or(false)) {
                auto warning = options.gamut_warning_color;
                warning.a = fallback.a;
                return warning;
            }
            if (options.proof_colors) {
                if (!impl_->cmyk_source) {
                    impl_->failures.fetch_add(1, std::memory_order_relaxed);
                    return fallback;
                }
                const IccTransformOptions proof_options{options.proof_intent, false};
                const auto to_proof = source_profile->identity() ==
                        impl_->cmyk_source->identity()
                    ? std::shared_ptr<const ProfileColorTransform>{}
                    : impl_->profile_transform(
                        source_profile, impl_->cmyk_source, proof_options);
                const auto to_display = impl_->profile_transform(
                        impl_->cmyk_source, impl_->display_profile, proof_options);
                ColorValue proof_color, display_color;
                bool proof_input_ready = false;
                if (to_proof)
                    proof_input_ready = to_proof->convert(source, proof_color);
                else if (source_profile->identity() ==
                         impl_->cmyk_source->identity()) {
                    proof_color = source;
                    proof_input_ready = true;
                }
                if (proof_input_ready && to_display &&
                    to_display->convert(proof_color, display_color))
                    return to_preview_rgba(display_color);
                impl_->failures.fetch_add(1, std::memory_order_relaxed);
                return fallback;
            }
            const auto display_transform = impl_->profile_transform(
                source_profile, impl_->display_profile,
                IccTransformOptions{});
            ColorValue display_color;
            if (display_transform && display_transform->convert(source, display_color))
                return to_preview_rgba(display_color);
            impl_->failures.fetch_add(1, std::memory_order_relaxed);
            return fallback;
        } catch (...) {
            impl_->failures.fetch_add(1, std::memory_order_relaxed);
            return fallback;
        }
    }

    if (source.profile_resource()) {
        if (!configured_source || source.profile_resource()->identity() != configured_source->identity())
            return fallback;
    }

    if (options.gamut_warning && is_out_of_gamut(source).value_or(false)) {
        auto warning = options.gamut_warning_color;
        warning.a = fallback.a;
        return warning;
    }

    try {
        COLOR input{}, output{};
        COLORTYPE type = COLOR_RGB;
        HTRANSFORM transform = impl_->rgb;
        std::array<WORD, 5> key{};
        if (const auto *c = std::get_if<CMYKColor>(&source.payload())) {
            if (!impl_->cmyk)
                return fallback;
            if (options.proof_colors) {
                const auto to_display = impl_->profile_transform(
                    impl_->cmyk_source, impl_->display_profile,
                    IccTransformOptions{options.proof_intent, false});
                ColorValue display_color;
                if (to_display && to_display->convert(source, display_color))
                    return to_preview_rgba(display_color);
                impl_->failures.fetch_add(1, std::memory_order_relaxed);
                return fallback;
            }
            key = {channel(c->c), channel(c->m), channel(c->y), channel(c->k), 1};
            input.cmyk = {key[0], key[1], key[2], key[3]};
            type = COLOR_CMYK;
            transform = impl_->cmyk;
        } else {
            const auto *rgb = std::get_if<RGBColor>(&source.payload());
            const WORD r = channel(rgb ? rgb->r : fallback.r);
            const WORD g = channel(rgb ? rgb->g : fallback.g);
            const WORD b = channel(rgb ? rgb->b : fallback.b);
            if (options.proof_colors) {
                if (!impl_->to_cmyk || !impl_->cmyk || !impl_->cmyk_source) {
                    impl_->failures.fetch_add(1, std::memory_order_relaxed);
                    return fallback;
                }
                key = {r, g, b, 2, static_cast<WORD>(options.proof_intent)};
                {
                    std::lock_guard lock(impl_->mutex);
                    if (const auto cached = impl_->cache.get(key)) {
                        auto value = *cached;
                        value.a = fallback.a;
                        return value;
                    }
                }
                const IccTransformOptions proof_options{options.proof_intent, false};
                const auto to_proof = impl_->profile_transform(
                    impl_->rgb_source, impl_->cmyk_source, proof_options);
                const auto to_display = impl_->profile_transform(
                    impl_->cmyk_source, impl_->display_profile, proof_options);
                ColorValue proof_color, display_color;
                if (to_proof && to_display &&
                    to_proof->convert(source, proof_color) &&
                    to_display->convert(proof_color, display_color)) {
                    auto value = to_preview_rgba(display_color);
                    std::lock_guard lock(impl_->mutex);
                    impl_->cache.insert(key, value);
                    value.a = fallback.a;
                    return value;
                }
                impl_->failures.fetch_add(1, std::memory_order_relaxed);
                return fallback;
            }
            key = {r, g, b, 0, 0};
            input.rgb = {r, g, b};
        }
        std::lock_guard lock(impl_->mutex);
        if (const auto cached = impl_->cache.get(key)) {
            auto value = *cached;
            value.a = fallback.a;
            return value;
        }
        if (!TranslateColors(transform, &input, 1, type, &output, COLOR_RGB)) {
            impl_->failures.fetch_add(1, std::memory_order_relaxed);
            return fallback;
        }
        PreviewRGBA value{output.rgb.red / 65535.0f, output.rgb.green / 65535.0f, output.rgb.blue / 65535.0f,
                          1};
        impl_->cache.insert(key, value);
        value.a = fallback.a;
        return value;
    } catch (...) {
        impl_->failures.fetch_add(1, std::memory_order_relaxed);
        return fallback;
    }
#endif
    return fallback;
}
} // namespace pikdraw::color

