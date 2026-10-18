#include "d2d_internal.hpp"
#include <algorithm>
#include <cmath>

namespace pikdraw::rendering::d2d {

namespace {

constexpr float kMaxTextureDimensionPx = 16384.0f;

D2D1_COLOR_F extract_rgb(const color::ColorValue &source, float opacity,
                         const color::IccPreview *color_preview) {
  const auto preview = color_preview ? color_preview->convert(source)
                                     : color::to_preview_rgba(source);
  return D2D1::ColorF(preview.r, preview.g, preview.b,
                      std::clamp(preview.a * opacity, 0.0f, 1.0f));
}

} // namespace

D2DEffectsPipeline::D2DEffectsPipeline() = default;

D2DEffectsPipeline::~D2DEffectsPipeline() = default;

ComPtr<ID2D1Brush> D2DEffectsPipeline::create_brush(
    ID2D1DeviceContext *context,
    const color::ColorValue &color,
    float opacity,
    const color::IccPreview *color_preview) {
  if (!context)
    return nullptr;

  D2D1_COLOR_F d2d_color = extract_rgb(color, opacity, color_preview);
  ComPtr<ID2D1SolidColorBrush> brush;
  HRESULT hr = context->CreateSolidColorBrush(d2d_color, brush.ReleaseAndGetAddressOf());
  if (FAILED(hr))
    return nullptr;

  ComPtr<ID2D1Brush> base_brush;
  brush.As(&base_brush);
  return base_brush;
}

bool D2DEffectsPipeline::apply_effects(
    ID2D1DeviceContext *context,
    ID2D1Image *source_image,
    const document::Appearance &appearance,
    document::ObjectId object_id,
    double camera_zoom,
    ComPtr<ID2D1Image> &out_image,
    std::vector<RenderDiagnostic> &diagnostics,
    const color::IccPreview *color_preview) {
  if (!context || !source_image)
    return false;

  ComPtr<ID2D1Image> current_image = source_image;

  for (const auto &node : appearance.effects) {
    if (!node.enabled)
      continue;

    const bool ok = std::visit(
        [&](const auto &effect) -> bool {
          using T = std::decay_t<decltype(effect)>;
          if constexpr (std::is_same_v<T, document::GaussianBlurEffect>) {
            const float sigma_x_px = static_cast<float>(effect.sigma_x_mm * camera_zoom);
            const float sigma_y_px = static_cast<float>(effect.sigma_y_mm * camera_zoom);
            const float std_dev = std::max(sigma_x_px, sigma_y_px);

            if (std_dev * 4.0f > kMaxTextureDimensionPx) {
              diagnostics.push_back({object_id, true,
                                     "Gaussian blur support exceeds texture budget (16384 px)"});
              return false;
            }

            ComPtr<ID2D1Effect> blur;
            HRESULT hr = context->CreateEffect(CLSID_D2D1GaussianBlur,
                                               blur.ReleaseAndGetAddressOf());
            if (FAILED(hr)) {
              diagnostics.push_back({object_id, true,
                                     "Direct2D Gaussian blur effect creation failed"});
              return false;
            }

            blur->SetInput(0, current_image.Get());
            blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, std_dev);
            blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE,
                           D2D1_BORDER_MODE_SOFT);

            blur->GetOutput(current_image.ReleaseAndGetAddressOf());
            return true;
          } else if constexpr (std::is_same_v<T, document::DropShadowEffect>) {
            const float sigma_px = static_cast<float>(
                std::max(effect.sigma_x_mm, effect.sigma_y_mm) * camera_zoom);
            const float dx_px = static_cast<float>(effect.dx_mm * camera_zoom);
            const float dy_px = static_cast<float>(effect.dy_mm * camera_zoom);

            if (sigma_px * 4.0f > kMaxTextureDimensionPx) {
              diagnostics.push_back({object_id, true,
                                     "Drop shadow support exceeds texture budget (16384 px)"});
              return false;
            }

            ComPtr<ID2D1Effect> shadow;
            HRESULT hr = context->CreateEffect(CLSID_D2D1Shadow,
                                               shadow.ReleaseAndGetAddressOf());
            if (FAILED(hr)) {
              diagnostics.push_back({object_id, true,
                                     "Direct2D drop shadow effect creation failed"});
              return false;
            }

            D2D1_COLOR_F shadow_color = extract_rgb(
                effect.color, static_cast<float>(effect.opacity), color_preview);
            shadow->SetInput(0, current_image.Get());
            shadow->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, sigma_px);
            shadow->SetValue(D2D1_SHADOW_PROP_COLOR, shadow_color);

            ComPtr<ID2D1Image> shadow_img;
            shadow->GetOutput(shadow_img.ReleaseAndGetAddressOf());

            // Composite shadow behind current graphic using D2D1 2D Affine Transform
            // to offset the shadow
            ComPtr<ID2D1Effect> transform_effect;
            hr = context->CreateEffect(CLSID_D2D12DAffineTransform,
                                       transform_effect.ReleaseAndGetAddressOf());
            if (FAILED(hr)) {
              diagnostics.push_back({object_id, true,
                                     "Direct2D shadow transform effect creation failed"});
              return false;
            }

            transform_effect->SetInput(0, shadow_img.Get());
            D2D1_MATRIX_3X2_F translation = D2D1::Matrix3x2F::Translation(dx_px, dy_px);
            transform_effect->SetValue(D2D1_2DAFFINETRANSFORM_PROP_TRANSFORM_MATRIX,
                                       translation);

            ComPtr<ID2D1Image> translated_shadow;
            transform_effect->GetOutput(translated_shadow.ReleaseAndGetAddressOf());

            // Composite translated shadow behind graphic
            ComPtr<ID2D1Effect> composite;
            hr = context->CreateEffect(CLSID_D2D1Composite,
                                       composite.ReleaseAndGetAddressOf());
            if (FAILED(hr)) {
              diagnostics.push_back({object_id, true,
                                     "Direct2D shadow composite effect creation failed"});
              return false;
            }

            if (effect.blend_mode != document::BlendMode::Normal) {
              diagnostics.push_back({object_id, true,
                                     "GPU drop-shadow blend mode requires a scene backdrop input"});
              return false;
            }

            composite->SetInput(0, translated_shadow.Get());
            composite->SetInput(1, current_image.Get());
            composite->SetValue(D2D1_COMPOSITE_PROP_MODE,
                                D2D1_COMPOSITE_MODE_SOURCE_OVER);

            composite->GetOutput(current_image.ReleaseAndGetAddressOf());
            return true;
          }
          return true;
        },
        node.parameters);

    if (!ok)
      return false;
  }

  out_image = current_image;
  return true;
}

ComPtr<ID2D1Brush> D2DEffectsPipeline::create_gradient_brush(
    ID2D1DeviceContext *context,
    const document::GradientPaint &gradient,
    float opacity,
    const color::IccPreview *color_preview) {
  if (!context || gradient.stops.empty())
    return nullptr;

  std::vector<D2D1_GRADIENT_STOP> d2d_stops;
  d2d_stops.reserve(gradient.stops.size());
  for (const auto &stop : gradient.stops) {
    D2D1_COLOR_F color = extract_rgb(stop.color, static_cast<float>(stop.opacity * opacity), color_preview);
    d2d_stops.push_back(D2D1::GradientStop(static_cast<float>(stop.offset), color));
  }

  D2D1_EXTEND_MODE extend_mode = D2D1_EXTEND_MODE_CLAMP;
  switch (gradient.spread) {
  case document::GradientSpread::Repeat:
    extend_mode = D2D1_EXTEND_MODE_WRAP;
    break;
  case document::GradientSpread::Reflect:
    extend_mode = D2D1_EXTEND_MODE_MIRROR;
    break;
  case document::GradientSpread::Pad:
  default:
    extend_mode = D2D1_EXTEND_MODE_CLAMP;
    break;
  }

  ComPtr<ID2D1GradientStopCollection> stop_collection;
  HRESULT hr = context->CreateGradientStopCollection(
      d2d_stops.data(), static_cast<UINT32>(d2d_stops.size()),
      D2D1_GAMMA_2_2, extend_mode, stop_collection.ReleaseAndGetAddressOf());
  if (FAILED(hr))
    return nullptr;

  if (gradient.kind == document::GradientKind::Linear) {
    geometry::Point2D p0{0.0, 0.5};
    geometry::Point2D p1{1.0, 0.5};
    p0 = gradient.gradient_to_object.map_point(p0);
    p1 = gradient.gradient_to_object.map_point(p1);

    D2D1_POINT_2F start = D2D1::Point2F(static_cast<float>(p0.x), static_cast<float>(p0.y));
    D2D1_POINT_2F end = D2D1::Point2F(static_cast<float>(p1.x), static_cast<float>(p1.y));

    D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props{};
    props.startPoint = start;
    props.endPoint = end;

    ComPtr<ID2D1LinearGradientBrush> brush;
    hr = context->CreateLinearGradientBrush(
        props, stop_collection.Get(), brush.ReleaseAndGetAddressOf());
    if (FAILED(hr))
      return nullptr;
    ComPtr<ID2D1Brush> base;
    brush.As(&base);
    return base;
  } else {
    geometry::Point2D center{0.5, 0.5};
    center = gradient.gradient_to_object.map_point(center);
    D2D1_POINT_2F d2d_center = D2D1::Point2F(static_cast<float>(center.x), static_cast<float>(center.y));

    geometry::Point2D edge{1.0, 0.5};
    edge = gradient.gradient_to_object.map_point(edge);
    float radius = static_cast<float>(std::hypot(edge.x - center.x, edge.y - center.y));
    if (radius <= 0.0f) radius = 1.0f;

    D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES props{};
    props.center = d2d_center;
    props.gradientOriginOffset = D2D1::Point2F(0, 0);
    props.radiusX = radius;
    props.radiusY = radius;

    ComPtr<ID2D1RadialGradientBrush> brush;
    hr = context->CreateRadialGradientBrush(
        props, stop_collection.Get(), brush.ReleaseAndGetAddressOf());
    if (FAILED(hr))
      return nullptr;
    ComPtr<ID2D1Brush> base;
    brush.As(&base);
    return base;
  }
}

} // namespace pikdraw::rendering::d2d
