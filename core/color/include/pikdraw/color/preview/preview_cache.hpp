#pragma once

#include "pikdraw/color/preview/color_transform.hpp"
#include <unordered_map>
#include <cstdint>

namespace pikdraw::color {

class ColorPreviewCache {
public:
    ColorPreviewCache() = default;

    [[nodiscard]] DeviceRGBA get_or_compute(const ColorValue& val, uint64_t profile_generation);
    void clear() noexcept;

    [[nodiscard]] size_t size() const noexcept { return entries_.size(); }

private:
    struct CacheKey {
        uint64_t hash{0};
        uint64_t generation{0};

        bool operator==(const CacheKey& o) const noexcept {
            return hash == o.hash && generation == o.generation;
        }
    };

    struct KeyHasher {
        size_t operator()(const CacheKey& k) const noexcept {
            return k.hash ^ (k.generation << 1);
        }
    };

    std::unordered_map<CacheKey, DeviceRGBA, KeyHasher> entries_;
};

} // namespace pikdraw::color
