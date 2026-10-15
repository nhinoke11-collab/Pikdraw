#pragma once
#include "pikdraw/color/preview/color_transform.hpp"
#include <filesystem>
#include <memory>
#include <string>

namespace pikdraw::color {
// Session preview only: explicit source ICC profiles -> system sRGB output.
// This never assigns profiles to document objects or changes source channels.
class IccPreview {
  public:
    static std::shared_ptr<const IccPreview> create(const std::filesystem::path &rgb_source,
                                                    const std::filesystem::path &cmyk_source,
                                                    std::string *error = nullptr);
    static std::filesystem::path system_srgb_profile();
    ~IccPreview();
    IccPreview(const IccPreview &) = delete;
    IccPreview &operator=(const IccPreview &) = delete;
    [[nodiscard]] PreviewRGBA convert(const ColorValue &source) const noexcept;
    [[nodiscard]] bool has_cmyk_transform() const noexcept;
    [[nodiscard]] uint64_t conversion_failure_count() const noexcept;

  private:
    struct Impl;
    explicit IccPreview(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
} // namespace pikdraw::color
