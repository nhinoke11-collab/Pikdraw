#include "d2d_scene_compositor.hpp"

namespace pikdraw::rendering::d2d {

bool draw_page(ID2D1DeviceContext *context, const document::Page &page,
               const Camera2D &camera, color::DeviceRGBA background,
               float viewport_origin_x, float viewport_origin_y) {
  if (background.a == 0 || page.width_mm <= 0 || page.height_mm <= 0)
    return true;
  ComPtr<ID2D1SolidColorBrush> paper;
  if (FAILED(context->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1),
                                           paper.ReleaseAndGetAddressOf())))
    return false;
  // Project page corners in double before converting to D2D coordinates.
  const auto lower = camera.world_to_screen({0, 0});
  const auto upper = camera.world_to_screen({page.width_mm, page.height_mm});
  context->SetTransform(D2D1::Matrix3x2F::Identity());
  context->FillRectangle(D2D1::RectF(
                             static_cast<float>(lower.x) + viewport_origin_x,
                             static_cast<float>(upper.y) + viewport_origin_y,
                             static_cast<float>(upper.x) + viewport_origin_x,
                             static_cast<float>(lower.y) + viewport_origin_y),
                          paper.Get());
  return true;
}

bool blend_with_scene(ID2D1DeviceContext *context, ID2D1Image *foreground,
                      document::BlendMode blend_mode,
                      document::ObjectId object_id, SceneDrawState &scene,
                      std::vector<RenderDiagnostic> &diagnostics) {
  if (!context || !foreground || !scene.target_bitmap || !scene.drawing_active) {
    diagnostics.push_back({object_id, true,
                           "Direct2D blend requires an active scene render target"});
    return false;
  }

  HRESULT hr = context->EndDraw();
  scene.drawing_active = false;
  if (FAILED(hr)) {
    diagnostics.push_back({object_id, true,
                           "Direct2D could not flush the scene before blending"});
    return false;
  }

  context->SetTarget(nullptr);
  D2D1_BITMAP_PROPERTIES1 copy_props{};
  copy_props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
  copy_props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
  copy_props.dpiX = scene.dpi_x;
  copy_props.dpiY = scene.dpi_y;
  copy_props.bitmapOptions = D2D1_BITMAP_OPTIONS_NONE;

  ComPtr<ID2D1Bitmap1> backdrop;
  hr = context->CreateBitmap(scene.size, nullptr, 0, &copy_props,
                             backdrop.ReleaseAndGetAddressOf());
  if (FAILED(hr)) {
    diagnostics.push_back({object_id, true,
                           "Direct2D could not allocate the blend backdrop snapshot"});
    return false;
  }
  hr = backdrop->CopyFromBitmap(nullptr, scene.target_bitmap, nullptr);
  if (FAILED(hr)) {
    diagnostics.push_back({object_id, true,
                           "Direct2D could not snapshot the scene for blending"});
    return false;
  }

  D2D1_BLEND_MODE d2d_mode{};
  switch (blend_mode) {
  case document::BlendMode::Multiply:
    d2d_mode = D2D1_BLEND_MODE_MULTIPLY;
    break;
  case document::BlendMode::Screen:
    d2d_mode = D2D1_BLEND_MODE_SCREEN;
    break;
  case document::BlendMode::Overlay:
    d2d_mode = D2D1_BLEND_MODE_OVERLAY;
    break;
  case document::BlendMode::Darken:
    d2d_mode = D2D1_BLEND_MODE_DARKEN;
    break;
  case document::BlendMode::Lighten:
    d2d_mode = D2D1_BLEND_MODE_LIGHTEN;
    break;
  case document::BlendMode::Normal:
  default:
    diagnostics.push_back({object_id, true,
                           "Normal compositing must bypass the Direct2D blend effect"});
    return false;
  }

  ComPtr<ID2D1Effect> blend;
  hr = context->CreateEffect(CLSID_D2D1Blend, blend.ReleaseAndGetAddressOf());
  if (FAILED(hr)) {
    diagnostics.push_back({object_id, true, "Direct2D blend effect creation failed"});
    return false;
  }
  blend->SetInput(0, foreground);
  blend->SetInput(1, backdrop.Get());
  hr = blend->SetValue(D2D1_BLEND_PROP_MODE, d2d_mode);
  if (FAILED(hr)) {
    diagnostics.push_back({object_id, true, "Direct2D blend mode configuration failed"});
    return false;
  }
  ComPtr<ID2D1Image> blended_scene;
  blend->GetOutput(blended_scene.ReleaseAndGetAddressOf());
  if (!blended_scene) {
    diagnostics.push_back({object_id, true,
                           "Direct2D blend effect did not produce an output image"});
    return false;
  }

  context->SetTarget(scene.target_bitmap);
  context->BeginDraw();
  context->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
  context->SetTransform(D2D1::Matrix3x2F::Identity());
  context->DrawImage(blended_scene.Get(), nullptr, nullptr,
                     D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR,
                     D2D1_COMPOSITE_MODE_SOURCE_COPY);
  hr = context->EndDraw();
  if (FAILED(hr)) {
    diagnostics.push_back({object_id, true,
                           "Direct2D failed while writing the blended scene"});
    return false;
  }

  context->SetTarget(scene.target_bitmap);
  context->BeginDraw();
  context->SetTransform(scene.view_matrix);
  scene.drawing_active = true;
  return true;
}

} // namespace pikdraw::rendering::d2d
