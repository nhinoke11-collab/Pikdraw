#pragma once

#include "pikdraw/color/preview/color_transform.hpp"
#include "pikdraw/color/preview/bounded_lru_cache.hpp"
#include <cstdint>

namespace pikdraw::color {

class ColorPreviewCache {
public:
    static constexpr size_t max_entries = 4096;

    ColorPreviewCache() = default;

    [[nodiscard]] DeviceRGBA get_or_compute(const ColorValue& val, uint64_t profile_generation);
    void clear() noexcept;

    [[nodiscard]] size_t size() const noexcept { return entries_.size(); }

private:
    struct CacheKey {
        ColorValue color;
        uint64_t hash{0};
        uint64_t generation{0};

        bool operator==(const CacheKey& o) const noexcept;
    };

    struct KeyHasher {
        size_t operator()(const CacheKey& k) const noexcept {
            return k.hash ^ (k.generation << 1);
        }
    };

    BoundedLruCache<CacheKey, DeviceRGBA, KeyHasher> entries_{max_entries};
};

} // namespace pikdraw::color
