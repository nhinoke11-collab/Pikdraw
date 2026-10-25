#include "pikdraw/color/preview/preview_cache.hpp"

#include <bit>
#include <type_traits>

namespace pikdraw::color {

namespace {

uint64_t compute_color_hash(const ColorValue& val) noexcept {
    // Simple 64-bit FNV-1a hash over color values
    uint64_t h = 14695981039346656037ULL;
    auto mix = [&h](const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i) {
            h ^= bytes[i];
            h *= 1099511628211ULL;
        }
    };

    const auto space = static_cast<uint32_t>(val.color_space());
    mix(&space, sizeof(space));
    const uint8_t is_profile_bound = val.profile_resource() ? 1 : 0;
    mix(&is_profile_bound, sizeof(is_profile_bound));
    if (val.profile_resource()) {
        const auto profile_space = static_cast<uint32_t>(
            val.profile_resource()->identity().color_space);
        mix(&profile_space, sizeof(profile_space));
        const auto& digest = val.profile_resource()->identity().checksum_sha256;
        mix(digest.data(), digest.size());
    }

    std::visit([&mix](const auto& c) {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, RGBColor>) {
            mix(&c.r, sizeof(c.r));
            mix(&c.g, sizeof(c.g));
            mix(&c.b, sizeof(c.b));
            mix(&c.a, sizeof(c.a));
        } else if constexpr (std::is_same_v<T, CMYKColor>) {
            mix(&c.c, sizeof(c.c));
            mix(&c.m, sizeof(c.m));
            mix(&c.y, sizeof(c.y));
            mix(&c.k, sizeof(c.k));
            mix(&c.a, sizeof(c.a));
        } else if constexpr (std::is_same_v<T, SpotColor>) {
            const auto name_size = c.spot_name.size();
            mix(&name_size, sizeof(name_size));
            mix(c.spot_name.data(), c.spot_name.size());
            mix(&c.tint, sizeof(c.tint));
            mix(&c.alternate_rgb.r, sizeof(c.alternate_rgb.r));
            mix(&c.alternate_rgb.g, sizeof(c.alternate_rgb.g));
            mix(&c.alternate_rgb.b, sizeof(c.alternate_rgb.b));
            mix(&c.alternate_rgb.a, sizeof(c.alternate_rgb.a));
        } else if constexpr (std::is_same_v<T, GrayscaleColor>) {
            mix(&c.gray, sizeof(c.gray));
            mix(&c.a, sizeof(c.a));
        }
    }, val.payload());

    return h;
}

bool same_double(double lhs, double rhs) noexcept {
    return std::bit_cast<uint64_t>(lhs) == std::bit_cast<uint64_t>(rhs);
}

bool same_rgb(const RGBColor& lhs, const RGBColor& rhs) noexcept {
    return same_double(lhs.r, rhs.r) && same_double(lhs.g, rhs.g) &&
        same_double(lhs.b, rhs.b) && same_double(lhs.a, rhs.a);
}

bool same_preview_color(const ColorValue& lhs, const ColorValue& rhs) noexcept {
    if (lhs.color_space() != rhs.color_space() ||
        lhs.overprint().fill != rhs.overprint().fill ||
        lhs.overprint().stroke != rhs.overprint().stroke)
        return false;

    const auto& lhs_profile = lhs.profile_resource();
    const auto& rhs_profile = rhs.profile_resource();
    if (static_cast<bool>(lhs_profile) != static_cast<bool>(rhs_profile)) return false;
    if (lhs_profile &&
        (lhs_profile->identity().color_space != rhs_profile->identity().color_space ||
         lhs_profile->identity().checksum_sha256 != rhs_profile->identity().checksum_sha256))
        return false;

    if (lhs.payload().index() != rhs.payload().index()) return false;
    if (const auto* a = std::get_if<RGBColor>(&lhs.payload()))
        return same_rgb(*a, std::get<RGBColor>(rhs.payload()));
    if (const auto* a = std::get_if<CMYKColor>(&lhs.payload())) {
        const auto& b = std::get<CMYKColor>(rhs.payload());
        return same_double(a->c, b.c) && same_double(a->m, b.m) &&
            same_double(a->y, b.y) && same_double(a->k, b.k) && same_double(a->a, b.a);
    }
    if (const auto* a = std::get_if<SpotColor>(&lhs.payload())) {
        const auto& b = std::get<SpotColor>(rhs.payload());
        return a->spot_name == b.spot_name && same_double(a->tint, b.tint) &&
            same_rgb(a->alternate_rgb, b.alternate_rgb) && a->overprint == b.overprint;
    }
    const auto& a = std::get<GrayscaleColor>(lhs.payload());
    const auto& b = std::get<GrayscaleColor>(rhs.payload());
    return same_double(a.gray, b.gray) && same_double(a.a, b.a);
}

} // namespace

bool ColorPreviewCache::CacheKey::operator==(const CacheKey& o) const noexcept {
    return generation == o.generation && same_preview_color(color, o.color);
}

DeviceRGBA ColorPreviewCache::get_or_compute(const ColorValue& val, uint64_t profile_generation) {
    const uint64_t h = compute_color_hash(val);
    CacheKey key{val, h, profile_generation};

    if (const auto cached = entries_.get(key)) return *cached;

    const PreviewRGBA preview = to_preview_rgba(val);
    const DeviceRGBA dev = preview.to_device();
    entries_.insert(key, dev);
    return dev;
}

void ColorPreviewCache::clear() noexcept {
    entries_.clear();
}

} // namespace pikdraw::color
