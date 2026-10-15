#include "pikdraw/color/preview/icc_preview.hpp"
#include <array>
#include <atomic>
#include <map>
#include <mutex>
#include <stdexcept>
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
    HTRANSFORM rgb{nullptr}, cmyk{nullptr};
    mutable std::mutex mutex;
    mutable std::map<std::array<WORD, 5>, PreviewRGBA> cache;
    ~Impl() {
        if (rgb)
            DeleteColorTransform(rgb);
        if (cmyk)
            DeleteColorTransform(cmyk);
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
    ProfileHandle(const std::filesystem::path &path, DWORD expected_space) {
        PROFILE source{};
        source.dwType = PROFILE_FILENAME;
        source.pProfileData = const_cast<wchar_t *>(path.c_str());
        source.cbDataSize = static_cast<DWORD>((path.native().size() + 1) * sizeof(wchar_t));
        value = OpenColorProfileW(&source, PROFILE_READ, FILE_SHARE_READ, OPEN_EXISTING);
        PROFILEHEADER header{};
        if (!value || !GetColorProfileHeader(value, &header) || header.phDataColorSpace != expected_space) {
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
HTRANSFORM make_transform(const std::filesystem::path &source, DWORD expected,
                          const std::filesystem::path &destination) {
    ProfileHandle input(source, expected), output(destination, SPACE_RGB);
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
    try {
        const auto output = system_srgb_profile();
        if (output.empty())
            throw std::runtime_error("System sRGB ICC profile is unavailable");
        auto impl = std::make_unique<Impl>();
        impl->rgb = make_transform(rgb_source.empty() ? output : rgb_source, SPACE_RGB, output);
        if (!cmyk_source.empty())
            impl->cmyk = make_transform(cmyk_source, SPACE_CMYK, output);
        return std::shared_ptr<const IccPreview>(new IccPreview(std::move(impl)));
    } catch (const std::exception &failure) {
        if (error)
            *error = failure.what();
    }
#else
    (void)rgb_source;
    (void)cmyk_source;
    if (error)
        *error = "ICC preview is currently available on Windows only";
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
uint64_t IccPreview::conversion_failure_count() const noexcept {
    return impl_->failures.load(std::memory_order_relaxed);
}
PreviewRGBA IccPreview::convert(const ColorValue &source) const noexcept {
    auto fallback = to_preview_rgba(source);
#ifdef _WIN32
    if (!source.is_valid())
        return fallback;
    try {
        COLOR input{}, output{};
        COLORTYPE type = COLOR_RGB;
        HTRANSFORM transform = impl_->rgb;
        std::array<WORD, 5> key{};
        if (const auto *c = std::get_if<CMYKColor>(&source.payload())) {
            if (!impl_->cmyk)
                return fallback;
            key = {channel(c->c), channel(c->m), channel(c->y), channel(c->k), 1};
            input.cmyk = {key[0], key[1], key[2], key[3]};
            type = COLOR_CMYK;
            transform = impl_->cmyk;
        } else {
            if (const auto *rgb = std::get_if<RGBColor>(&source.payload()))
                key = {channel(rgb->r), channel(rgb->g), channel(rgb->b), 0, 0};
            else
                key = {channel(fallback.r), channel(fallback.g), channel(fallback.b), 0, 0};
            input.rgb = {key[0], key[1], key[2]};
        }
        std::lock_guard lock(impl_->mutex);
        if (const auto found = impl_->cache.find(key); found != impl_->cache.end()) {
            auto value = found->second;
            value.a = fallback.a;
            return value;
        }
        if (!TranslateColors(transform, &input, 1, type, &output, COLOR_RGB)) {
            impl_->failures.fetch_add(1, std::memory_order_relaxed);
            return fallback;
        }
        PreviewRGBA value{output.rgb.red / 65535.0f, output.rgb.green / 65535.0f, output.rgb.blue / 65535.0f,
                          1};
        if (impl_->cache.size() >= 4096)
            impl_->cache.clear();
        impl_->cache.emplace(key, value);
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
