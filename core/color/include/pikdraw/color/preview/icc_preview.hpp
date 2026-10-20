#pragma once
#include "pikdraw/color/preview/color_transform.hpp"
#include <filesystem>
#include <memory>
#include <string>

namespace pikdraw::color {
struct PreviewOptions {
    bool proof_colors{false};       // Soft-proofing: simulate target print profile on display
    bool gamut_warning{false};      // Gamut warning: highlight out-of-gamut colors with warning color
    bool overprint_preview{false};  // Overprint preview simulation
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
    static std::filesystem::path system_srgb_profile();
    ~IccPreview();
    IccPreview(const IccPreview &) = delete;
    IccPreview &operator=(const IccPreview &) = delete;

    void set_preview_options(const PreviewOptions& options) const noexcept;
    [[nodiscard]] PreviewOptions preview_options() const noexcept;
    [[nodiscard]] bool is_out_of_gamut(const ColorValue &source) const noexcept;

    [[nodiscard]] PreviewRGBA convert(const ColorValue &source) const noexcept;
    [[nodiscard]] PreviewRGBA convert(const ColorValue &source, const PreviewOptions &options) const noexcept;

    // Explicit document conversion, distinct from the screen preview cache.
    // Returns false if the matching transform is unavailable or fails.
    [[nodiscard]] bool convert_process(const ColorValue& source, DocumentColorMode target,
                                       ColorValue& result) const noexcept;
    [[nodiscard]] bool has_cmyk_transform() const noexcept;
    [[nodiscard]] uint64_t conversion_failure_count() const noexcept;

  private:
    struct Impl;
    explicit IccPreview(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
} // namespace pikdraw::color
