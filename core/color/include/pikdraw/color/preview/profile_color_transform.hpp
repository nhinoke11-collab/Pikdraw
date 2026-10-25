#pragma once

#include "pikdraw/color/profiles/profile_resource.hpp"
#include "pikdraw/color/values/color_value.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace pikdraw::color {

enum class IccRenderingIntent : uint8_t {
    Perceptual,
    RelativeColorimetric,
    Saturation,
    AbsoluteColorimetric
};

struct IccTransformOptions {
    IccRenderingIntent intent{IccRenderingIntent::RelativeColorimetric};
    bool black_point_compensation{false};
};

struct IccTransformExecutionOptions {
    std::function<bool()> cancelled;
    std::function<void(uint64_t completed_pixels, uint64_t total_pixels)> progress;
};

class ProfileColorTransform;

struct ProfileColorTransformResult {
    std::shared_ptr<const ProfileColorTransform> transform;
    std::string diagnostic;

    [[nodiscard]] explicit operator bool() const noexcept {
        return transform != nullptr && diagnostic.empty();
    }
};

/// Reusable, profile-bound process-color transform. It changes channel values
/// only when explicitly invoked and preserves alpha and overprint metadata.
class ProfileColorTransform final {
public:
    [[nodiscard]] static ProfileColorTransformResult create(
        std::shared_ptr<const ProfileResource> source,
        std::shared_ptr<const ProfileResource> destination,
        IccTransformOptions options = {});

    ~ProfileColorTransform();
    ProfileColorTransform(const ProfileColorTransform&) = delete;
    ProfileColorTransform& operator=(const ProfileColorTransform&) = delete;

    [[nodiscard]] const ProfileResource& source_profile() const noexcept;
    [[nodiscard]] const ProfileResource& destination_profile() const noexcept;
    [[nodiscard]] std::shared_ptr<const ProfileResource> source_resource() const noexcept;
    [[nodiscard]] std::shared_ptr<const ProfileResource> destination_resource() const noexcept;
    [[nodiscard]] IccTransformOptions options() const noexcept;
    [[nodiscard]] uint64_t failure_count() const noexcept;
    [[nodiscard]] bool convert(const ColorValue& source, ColorValue& destination) const noexcept;
    /// Returns whether a source color is inside this transform's output gamut.
    /// nullopt means the active CMM does not provide a gamut result.
    [[nodiscard]] std::optional<bool> is_in_gamut(
        const ColorValue& source) const noexcept;
    /// Converts straight-alpha interleaved RGBA8 pixels with this transform's
    /// bound RGB profiles. Alpha bytes are copied unchanged. Spans must have
    /// equal lengths divisible by four; partial overlap is rejected.
    [[nodiscard]] bool convert_rgb8(std::span<const uint8_t> source_rgba,
                                    std::span<uint8_t> destination_rgba) const noexcept;
    /// Converts tightly packed little-endian 16-bit process pixels. RGB
    /// profiles use RGB or RGBA samples; CMYK profiles use CMYK or CMYKA.
    /// Alpha, when present in the destination, is copied unchanged or set to
    /// full coverage when the source has no alpha. Dropping source alpha and
    /// partial span overlap are rejected. Cancellation returns false and may
    /// leave completed batches in the destination; publish only on success.
    [[nodiscard]] bool convert_process16le(
        std::span<const uint8_t> source_samples,
        std::span<uint8_t> destination_samples,
        bool source_has_alpha, bool destination_has_alpha,
        const IccTransformExecutionOptions& execution = {}) const noexcept;

private:
    struct Impl;
    explicit ProfileColorTransform(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace pikdraw::color
