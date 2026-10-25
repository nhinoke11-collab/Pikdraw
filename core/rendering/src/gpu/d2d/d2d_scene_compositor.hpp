#pragma once

#include "d2d_internal.hpp"

namespace pikdraw::rendering::d2d {

struct SceneDrawState {
  ID2D1Bitmap1 *target_bitmap{nullptr};
  D2D1_SIZE_U size{};
  float dpi_x{96.0f};
  float dpi_y{96.0f};
  D2D1_MATRIX_3X2_F view_matrix{D2D1::Matrix3x2F::Identity()};
  bool drawing_active{false};
  float viewport_origin_x{0.0f};
  float viewport_origin_y{0.0f};
};

bool draw_page(ID2D1DeviceContext *context, const document::Page &page,
               const Camera2D &camera, color::DeviceRGBA background,
               float viewport_origin_x = 0.0f,
               float viewport_origin_y = 0.0f);

bool blend_with_scene(ID2D1DeviceContext *context, ID2D1Image *foreground,
                      document::BlendMode blend_mode,
                      document::ObjectId object_id, SceneDrawState &scene,
                      std::vector<RenderDiagnostic> &diagnostics);

} // namespace pikdraw::rendering::d2d
