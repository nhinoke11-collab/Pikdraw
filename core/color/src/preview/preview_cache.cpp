#include "pikdraw/color/preview/preview_cache.hpp"

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
            mix(c.spot_name.data(), c.spot_name.size());
            mix(&c.tint, sizeof(c.tint));
        } else if constexpr (std::is_same_v<T, GrayscaleColor>) {
            mix(&c.gray, sizeof(c.gray));
            mix(&c.a, sizeof(c.a));
        }
    }, val.payload());

    return h;
}

} // namespace

DeviceRGBA ColorPreviewCache::get_or_compute(const ColorValue& val, uint64_t profile_generation) {
    const uint64_t h = compute_color_hash(val);
    CacheKey key{h, profile_generation};

    auto it = entries_.find(key);
    if (it != entries_.end()) {
        return it->second;
    }

    const PreviewRGBA preview = to_preview_rgba(val);
    const DeviceRGBA dev = preview.to_device();
    entries_[key] = dev;
    return dev;
}

void ColorPreviewCache::clear() noexcept {
    entries_.clear();
}

} // namespace pikdraw::color
