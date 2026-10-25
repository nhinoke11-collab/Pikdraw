#pragma once
#include "pikdraw/color/preview/color_transform.hpp"
#include "pikdraw/color/preview/profile_color_transform.hpp"
#include "pikdraw/color/profiles/profile_resource.hpp"
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace pikdraw::color {
struct PreviewOptions {
    bool proof_colors{false};       // Soft-proofing: simulate target print profile on display
    bool gamut_warning{false};      // Highlights only colors rejected by the active CMM gamut check.
    bool overprint_preview{false};  // Reserved; current renderer does not composite ink separations.
    IccRenderingIntent proof_intent{IccRenderingIntent::RelativeColorimetric};
    PreviewRGBA gamut_warning_color{0.5f, 0.5f, 0.5f, 1.0f}; // Default neutral warning gray
};

// Session preview only: explicit source ICC profiles -> system sRGB output.
// convert() never changes source channels. convert_process() explicitly returns
// destination channels for document editing without modifying its input.
class IccPreview {
  public:
    static std::shared_ptr<const IccPreview> create(const std::filesystem::path &rgb_source,
                                                    const std::filesystem::path &cmyk_source,
                                                    std::string *error = nullptr);
    static std::shared_ptr<const IccPreview> create_from_resources(
        std::shared_ptr<const ProfileResource> rgb_source,
        std::shared_ptr<const ProfileResource> cmyk_source,
        std::string *error = nullptr);
    static std::shared_ptr<const IccPreview> create_from_resources(
        std::shared_ptr<const ProfileResource> rgb_source,
        std::shared_ptr<const ProfileResource> cmyk_source,
        std::shared_ptr<const ProfileResource> display_destination,
        std::string *error = nullptr);
    static std::filesystem::path system_srgb_profile();
    ~IccPreview();
    IccPreview(const IccPreview &) = delete;
    IccPreview &operator=(const IccPreview &) = delete;

    void set_preview_options(const PreviewOptions& options) const noexcept;
    [[nodiscard]] PreviewOptions preview_options() const noexcept;
    // Approximate RGB round-trip diagnostic. This is not Delta E or a
    // colorimetric out-of-gamut predicate; kept under a new accurate name.
    [[nodiscard]] bool has_large_rgb_roundtrip_difference(const ColorValue& source) const noexcept;
    /// True only when the configured CMM returns an explicit gamut result.
    [[nodiscard]] std::optional<bool> is_out_of_gamut(
        const ColorValue& source) const noexcept;
    [[nodiscard]] bool supports_gamut_check() const noexcept;

    [[nodiscard]] PreviewRGBA convert(const ColorValue &source) const noexcept;
    [[nodiscard]] PreviewRGBA convert(const ColorValue &source, const PreviewOptions &options) const noexcept;

    // Explicit document conversion, distinct from the screen preview cache.
    // Returns false if the matching transform is unavailable or fails.
    [[nodiscard]] bool convert_process(const ColorValue& source, DocumentColorMode target,
                                       ColorValue& result) const noexcept;
    [[nodiscard]] bool has_cmyk_transform() const noexcept;
    [[nodiscard]] bool supports_proofing(IccRenderingIntent intent) const noexcept;
    [[nodiscard]] uint64_t conversion_failure_count() const noexcept;

  private:
    struct Impl;
    explicit IccPreview(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
} // namespace pikdraw::color
