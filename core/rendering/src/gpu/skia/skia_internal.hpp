#pragma once

#include "pikdraw/color/values/color_value.hpp"
#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"
#include "pikdraw/geometry/primitives/point.hpp"
#include "pikdraw/geometry/curves/path.hpp"
#include "pikdraw/rendering/backend.hpp"
#include "pikdraw/rendering/camera.hpp"
#include "pikdraw/rendering/frame_target.hpp"
#include "pikdraw/rendering/d2d_backend.hpp"
#include "pikdraw/rendering/software_rasterizer.hpp"

#include <memory>
#include <string>
#include <vector>

namespace pikdraw::rendering::skia {

struct SkiaPoint2F {
  float x{0.0f};
  float y{0.0f};
};

inline SkiaPoint2F camera_relative_point(double world_x, double world_y,
                                        double camera_center_x,
                                        double camera_center_y) noexcept {
  return SkiaPoint2F{static_cast<float>(world_x - camera_center_x),
                     static_cast<float>(world_y - camera_center_y)};
}

class SkiaContextD3D12 {
public:
  SkiaContextD3D12();
  ~SkiaContextD3D12();

  bool initialize();
  void shutdown();
  bool recover();

  [[nodiscard]] bool is_valid() const noexcept { return is_initialized_; }
  [[nodiscard]] DeviceLossStatus status() const noexcept { return status_; }

private:
  bool is_initialized_{false};
  DeviceLossStatus status_{DeviceLossStatus::Healthy};
};

class SkiaPathConverter {
public:
  SkiaPathConverter() = default;

  struct PathSegment {
    geometry::PathVerb verb;
    SkiaPoint2F pt;
    SkiaPoint2F ctrl1;
    SkiaPoint2F ctrl2;
  };

  static std::vector<PathSegment> convert(
      const geometry::Path2D &path,
      double camera_center_x,
      double camera_center_y);
};

class SkiaEffectsCompositor {
public:
  SkiaEffectsCompositor();
  ~SkiaEffectsCompositor();

  /// SkSL Shader source for realtime GPU CMYK/Spot color transformation
  [[nodiscard]] static std::string_view cmyk_icc_sksl_shader() noexcept;

  bool evaluate_appearance(
      const document::Appearance &appearance,
      double camera_zoom,
      std::vector<RenderDiagnostic> &diagnostics);
};

} // namespace pikdraw::rendering::skia
