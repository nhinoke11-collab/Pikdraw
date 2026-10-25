#include "d2d_internal.hpp"
#include "pikdraw/rendering/gradient_sampler.hpp"
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
            const float max_sigma = std::max(sigma_x_px, sigma_y_px);

            if (max_sigma * 4.0f > kMaxTextureDimensionPx) {
              diagnostics.push_back({object_id, true,
                                     "Gaussian blur support exceeds texture budget (16384 px)"});
              return false;
            }

            if (std::abs(sigma_x_px - sigma_y_px) > 1e-4f) {
              if (sigma_x_px <= 1e-4f || sigma_y_px <= 1e-4f) {
                diagnostics.push_back({object_id, true,
                                       "Direct2D does not support zero-axis anisotropic Gaussian blur; requires software rasterizer"});
                return false;
              }
              ComPtr<ID2D1Effect> scale_pre;
              HRESULT hr = context->CreateEffect(CLSID_D2D12DAffineTransform, scale_pre.ReleaseAndGetAddressOf());
              if (FAILED(hr)) return false;
              const float sy = sigma_x_px / sigma_y_px;
              scale_pre->SetInput(0, current_image.Get());
              scale_pre->SetValue(D2D1_2DAFFINETRANSFORM_PROP_TRANSFORM_MATRIX, D2D1::Matrix3x2F::Scale(1.0f, sy));
              ComPtr<ID2D1Image> scaled_img;
              scale_pre->GetOutput(scaled_img.ReleaseAndGetAddressOf());

              ComPtr<ID2D1Effect> blur;
              hr = context->CreateEffect(CLSID_D2D1GaussianBlur, blur.ReleaseAndGetAddressOf());
              if (FAILED(hr)) return false;
              blur->SetInput(0, scaled_img.Get());
              blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, sigma_x_px);
              blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);
              ComPtr<ID2D1Image> blurred_img;
              blur->GetOutput(blurred_img.ReleaseAndGetAddressOf());

              ComPtr<ID2D1Effect> scale_post;
              hr = context->CreateEffect(CLSID_D2D12DAffineTransform, scale_post.ReleaseAndGetAddressOf());
              if (FAILED(hr)) return false;
              scale_post->SetInput(0, blurred_img.Get());
              scale_post->SetValue(D2D1_2DAFFINETRANSFORM_PROP_TRANSFORM_MATRIX, D2D1::Matrix3x2F::Scale(1.0f, 1.0f / sy));
              scale_post->GetOutput(current_image.ReleaseAndGetAddressOf());
              return true;
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
            blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, sigma_x_px);
            blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE,
                           D2D1_BORDER_MODE_SOFT);

            blur->GetOutput(current_image.ReleaseAndGetAddressOf());
            return true;
          } else if constexpr (std::is_same_v<T, document::DropShadowEffect>) {
            const float sigma_x_px = static_cast<float>(effect.sigma_x_mm * camera_zoom);
            const float sigma_y_px = static_cast<float>(effect.sigma_y_mm * camera_zoom);
            const float max_sigma = std::max(sigma_x_px, sigma_y_px);
            const bool anisotropic = std::abs(sigma_x_px - sigma_y_px) > 1e-4f;
            const float dx_px = static_cast<float>(effect.dx_mm * camera_zoom);
            const float dy_px = static_cast<float>(-effect.dy_mm * camera_zoom);

            if (max_sigma * 4.0f > kMaxTextureDimensionPx) {
              diagnostics.push_back({object_id, true,
                                     "Drop shadow support exceeds texture budget (16384 px)"});
              return false;
            }

            if (anisotropic && (sigma_x_px <= 1e-4f || sigma_y_px <= 1e-4f)) {
              diagnostics.push_back({object_id, true,
                                     "Direct2D cannot represent a zero-axis anisotropic drop shadow; no CPU fallback is available"});
              return false;
            }

            ComPtr<ID2D1Image> shadow_source = current_image;
            const float y_scale = anisotropic ? sigma_x_px / sigma_y_px : 1.0f;
            if (anisotropic) {
              D2D1_RECT_F source_bounds{};
              if (!std::isfinite(y_scale) || y_scale <= 0.0f ||
                  FAILED(context->GetImageLocalBounds(current_image.Get(),
                                                       &source_bounds)))
                return false;
              const double scaled_width =
                  static_cast<double>(source_bounds.right - source_bounds.left) +
                  4.0 * sigma_x_px;
              const double scaled_height =
                  static_cast<double>(source_bounds.bottom - source_bounds.top) *
                      y_scale +
                  4.0 * sigma_x_px;
              if (!std::isfinite(scaled_width) || !std::isfinite(scaled_height) ||
                  scaled_width <= 0.0 || scaled_height <= 0.0 ||
                  scaled_width > kMaxTextureDimensionPx ||
                  scaled_height > kMaxTextureDimensionPx ||
                  scaled_width * scaled_height > 16'777'216.0) {
                diagnostics.push_back({object_id, true,
                                       "Anisotropic drop shadow transform exceeds the Direct2D texture budget"});
                return false;
              }
              ComPtr<ID2D1Effect> scale_pre;
              HRESULT hr = context->CreateEffect(CLSID_D2D12DAffineTransform,
                                         scale_pre.ReleaseAndGetAddressOf());
              if (FAILED(hr)) return false;
              scale_pre->SetInput(0, current_image.Get());
              if (FAILED(scale_pre->SetValue(
                      D2D1_2DAFFINETRANSFORM_PROP_TRANSFORM_MATRIX,
                      D2D1::Matrix3x2F::Scale(1.0f, y_scale))))
                return false;
              scale_pre->GetOutput(shadow_source.ReleaseAndGetAddressOf());
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
            shadow->SetInput(0, shadow_source.Get());
            if (FAILED(shadow->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION,
                                        sigma_x_px)) ||
                FAILED(shadow->SetValue(D2D1_SHADOW_PROP_COLOR, shadow_color)))
              return false;

            ComPtr<ID2D1Image> shadow_img;
            shadow->GetOutput(shadow_img.ReleaseAndGetAddressOf());

            if (anisotropic) {
              ComPtr<ID2D1Effect> scale_post;
              hr = context->CreateEffect(CLSID_D2D12DAffineTransform,
                                         scale_post.ReleaseAndGetAddressOf());
              if (FAILED(hr)) return false;
              scale_post->SetInput(0, shadow_img.Get());
              if (FAILED(scale_post->SetValue(
                      D2D1_2DAFFINETRANSFORM_PROP_TRANSFORM_MATRIX,
                      D2D1::Matrix3x2F::Scale(1.0f, 1.0f / y_scale))))
                return false;
              scale_post->GetOutput(shadow_img.ReleaseAndGetAddressOf());
            }

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
          } else if constexpr (std::is_same_v<T, document::FeatherEffect>) {
            diagnostics.push_back({object_id, true,
                                   "Direct2D does not support Feather; CPU fallback is unavailable"});
            return false;
          } else if constexpr (std::is_same_v<T, document::OuterGlowEffect>) {
            diagnostics.push_back({object_id, true,
                                   "Direct2D does not support Outer Glow; CPU fallback is unavailable"});
            return false;
          } else if constexpr (std::is_same_v<T, document::InnerGlowEffect>) {
            diagnostics.push_back({object_id, true,
                                   "Direct2D does not support Inner Glow; CPU fallback is unavailable"});
            return false;
          } else if constexpr (std::is_same_v<T, document::InnerShadowEffect>) {
            diagnostics.push_back({object_id, true,
                                   "Direct2D does not support Inner Shadow; CPU fallback is unavailable"});
            return false;
          } else if constexpr (std::is_same_v<T, document::ColorOverlayEffect>) {
            diagnostics.push_back({object_id, true,
                                   "Direct2D does not support Color Overlay; CPU fallback is unavailable"});
            return false;
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

  const auto sampler = GradientSampler::prepare(
      gradient, geometry::Transform2D::identity(), Camera2D{}, color_preview, opacity);
  if (!sampler)
    return nullptr;

  std::vector<D2D1_GRADIENT_STOP> d2d_stops;
  d2d_stops.reserve(gradient.stops.size() * 2 - 1);
  for (size_t i = 0; i < gradient.stops.size(); ++i) {
    const auto &stop = gradient.stops[i];
    const auto stop_color = extract_rgb(
        stop.color, static_cast<float>(stop.opacity * opacity), color_preview);
    d2d_stops.push_back(D2D1::GradientStop(static_cast<float>(stop.offset), stop_color));

    if (i + 1 >= gradient.stops.size())
      continue;
    const auto &next = gradient.stops[i + 1];
    if (next.offset <= stop.offset + 1e-12 ||
        std::abs(stop.midpoint_to_next - 0.5) <= 1e-12)
      continue;

    // Direct2D has no midpoint field. Add the exact half-color knot so its
    // two linear segments reproduce PikDraw's piecewise midpoint remapping.
    const double offset = stop.offset +
        (next.offset - stop.offset) * stop.midpoint_to_next;
    const float d2d_offset = static_cast<float>(offset);
    const float next_offset = static_cast<float>(next.offset);
    if (d2d_offset <= static_cast<float>(stop.offset) || d2d_offset >= next_offset)
      continue;
    const auto midpoint = sampler->sample_at_t(offset);
    d2d_stops.push_back(D2D1::GradientStop(
        d2d_offset, D2D1::ColorF(midpoint.r, midpoint.g, midpoint.b, midpoint.a)));
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
  ComPtr<ID2D1GradientStopCollection1> stop_collection1;
  HRESULT hr = context->CreateGradientStopCollection(
      d2d_stops.data(), static_cast<UINT32>(d2d_stops.size()),
      D2D1_COLOR_SPACE_SRGB, D2D1_COLOR_SPACE_SRGB,
      D2D1_BUFFER_PRECISION_8BPC_UNORM, extend_mode,
      D2D1_COLOR_INTERPOLATION_MODE_STRAIGHT,
      stop_collection1.ReleaseAndGetAddressOf());
  if (FAILED(hr))
    return nullptr;
  stop_collection1.As(&stop_collection);

  if (gradient.kind == document::GradientKind::Linear) {
    geometry::Point2D p0{0.0, 0.0};
    geometry::Point2D p1{1.0, 0.0};
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
    const auto &matrix = gradient.gradient_to_object;
    // A radial gradient depends on the ellipse's quadratic form A*A^T.
    // Decompose that form into principal radii and a rotation so Direct2D's
    // native radial brush samples the same normalized radius as GradientSampler.
    const double xx = matrix.a * matrix.a + matrix.c * matrix.c;
    const double xy = matrix.a * matrix.b + matrix.c * matrix.d;
    const double yy = matrix.b * matrix.b + matrix.d * matrix.d;
    const double discriminant = std::hypot(xx - yy, 2.0 * xy);
    const double radius_x = std::sqrt(std::max(0.0, (xx + yy + discriminant) * 0.5));
    const double determinant = std::abs(matrix.a * matrix.d - matrix.b * matrix.c);
    const double radius_y = radius_x > 0.0 ? determinant / radius_x : 0.0;
    const double angle = 0.5 * std::atan2(2.0 * xy, xx - yy);

    D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES props{};
    props.center = D2D1::Point2F(static_cast<float>(matrix.tx),
                                 static_cast<float>(matrix.ty));
    props.gradientOriginOffset = D2D1::Point2F(0, 0);
    props.radiusX = static_cast<float>(radius_x);
    props.radiusY = static_cast<float>(radius_y);

    ComPtr<ID2D1RadialGradientBrush> brush;
    hr = context->CreateRadialGradientBrush(
        props, stop_collection.Get(), brush.ReleaseAndGetAddressOf());
    if (FAILED(hr))
      return nullptr;
    if (std::abs(angle) > 1e-12) {
      brush->SetTransform(D2D1::Matrix3x2F::Rotation(
          static_cast<float>(angle * 180.0 / 3.14159265358979323846),
          props.center));
    }
    ComPtr<ID2D1Brush> base;
    brush.As(&base);
    return base;
  }
}

} // namespace pikdraw::rendering::d2d
