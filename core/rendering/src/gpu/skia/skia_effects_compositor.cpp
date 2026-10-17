#include "skia_internal.hpp"
#include <algorithm>
#include <cmath>

namespace pikdraw::rendering::skia {

namespace {

constexpr float kMaxSkiaFilterRadiusPx = 16384.0f;

constexpr std::string_view kCmykIccSkSL = R"(
    uniform shader input_shader;
    uniform float4x4 cmyk_to_rgb_matrix;
    uniform float4 tint_and_overprint;

    half4 main(float2 coord) {
        half4 color = input_shader.eval(coord);
        // Transform CMYK to linear sRGB via 4x4 ICC color matrix
        half4 converted = cmyk_to_rgb_matrix * color;
        converted.a = color.a;
        return converted;
    }
)";

} // namespace

SkiaEffectsCompositor::SkiaEffectsCompositor() = default;

SkiaEffectsCompositor::~SkiaEffectsCompositor() = default;

std::string_view SkiaEffectsCompositor::cmyk_icc_sksl_shader() noexcept {
  return kCmykIccSkSL;
}

bool SkiaEffectsCompositor::evaluate_appearance(
    const document::Appearance &appearance,
    double camera_zoom,
    std::vector<RenderDiagnostic> &diagnostics) {
  for (const auto &node : appearance.effects) {
    if (!node.enabled)
      continue;

    const bool valid = std::visit(
        [&](const auto &effect) -> bool {
          using T = std::decay_t<decltype(effect)>;
          if constexpr (std::is_same_v<T, document::GaussianBlurEffect>) {
            const float sigma_x = static_cast<float>(effect.sigma_x_mm * camera_zoom);
            const float sigma_y = static_cast<float>(effect.sigma_y_mm * camera_zoom);
            if (std::max(sigma_x, sigma_y) * 4.0f > kMaxSkiaFilterRadiusPx) {
              diagnostics.push_back({document::kInvalidObjectId, true,
                                     "Skia Gaussian blur exceeds 16384 px budget"});
              return false;
            }
            return true;
          } else if constexpr (std::is_same_v<T, document::DropShadowEffect>) {
            const float sigma = static_cast<float>(
                std::max(effect.sigma_x_mm, effect.sigma_y_mm) * camera_zoom);
            if (sigma * 4.0f > kMaxSkiaFilterRadiusPx) {
              diagnostics.push_back({document::kInvalidObjectId, true,
                                     "Skia Drop shadow exceeds 16384 px budget"});
              return false;
            }
            return true;
          }
          return true;
        },
        node.parameters);

    if (!valid)
      return false;
  }
  return true;
}

} // namespace pikdraw::rendering::skia
