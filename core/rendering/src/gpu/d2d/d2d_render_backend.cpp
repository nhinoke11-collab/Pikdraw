#include "pikdraw/rendering/d2d_backend.hpp"
#include "d2d_internal.hpp"
#include "pikdraw/rendering/spatial_index.hpp"
#include "pikdraw/typography/layout/glyph_outline.hpp"
#include "pikdraw/typography/layout/text_layout.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>

namespace pikdraw::rendering {

namespace {

bool identity_transform(const geometry::Transform2D &transform) {
  const auto identity = geometry::Transform2D::identity();
  return std::abs(transform.a - identity.a) < 1e-6 &&
         std::abs(transform.b - identity.b) < 1e-6 &&
         std::abs(transform.c - identity.c) < 1e-6 &&
         std::abs(transform.d - identity.d) < 1e-6 &&
         std::abs(transform.tx - identity.tx) < 1e-6 &&
         std::abs(transform.ty - identity.ty) < 1e-6;
}

} // namespace

struct D2DRenderBackend::Impl {
  d2d::D2DDeviceManager device_mgr;
  d2d::D2DGeometryCache geometry_cache;
  d2d::D2DEffectsPipeline effects_pipeline;
  SpatialIndex spatial_index;
  SoftwareRasterizer fallback_rasterizer;
  color::DeviceRGBA background{255, 255, 255, 255};
  std::shared_ptr<const color::IccPreview> color_preview;
  bool d2d_initialized{false};

  Impl() {
    d2d_initialized = device_mgr.initialize();
  }

  bool should_use_software(const PixelBufferTarget & /*target*/) const {
    const char *env = std::getenv("PIKDRAW_RENDER_BACKEND");
    if (env && std::string_view(env) == "cpu") {
      return true;
    }
    return !d2d_initialized || !device_mgr.is_valid();
  }

  bool render_object(
      const document::DocumentSnapshot &snapshot,
      const document::ObjectVariant &variant,
      const geometry::Transform2D &parent_transform,
      const Camera2D &camera,
      ID2D1DeviceContext *context,
      ID2D1Factory1 *factory,
      size_t &rendered_count,
      std::vector<RenderDiagnostic> &diagnostics,
      size_t depth = 0) {
    if (depth > 256)
      return false;

    return std::visit(
        [&](const auto &obj) -> bool {
          using T = std::decay_t<decltype(obj)>;
          if constexpr (std::is_same_v<T, document::PathObject>) {
            const auto effective_transform = parent_transform * obj.transform;
            geometry::Path2D path = obj.path;
            if (!identity_transform(effective_transform)) {
              path = path.transformed(effective_transform);
            }

            auto geo = geometry_cache.get_or_create_path(
                factory, obj.id, snapshot.revision(), path,
                camera.center().x, camera.center().y);
            if (!geo) {
              return false;
            }

            const bool has_effects = !obj.appearance.effects.empty() ||
                                     obj.appearance.blend_mode != document::BlendMode::Normal;

            auto draw_geometry_contents = [&](ID2D1DeviceContext *ctx) {
              if (obj.has_fill) {
                d2d::ComPtr<ID2D1Brush> fill_brush;
                const float fill_op = static_cast<float>(
                    obj.appearance.fill_opacity * obj.appearance.object_opacity);
                if (obj.fill_gradient) {
                  fill_brush = effects_pipeline.create_gradient_brush(
                      ctx, *obj.fill_gradient, camera, fill_op);
                } else {
                  fill_brush = effects_pipeline.create_brush(
                      ctx, obj.fill, fill_op);
                }
                if (fill_brush) {
                  ctx->FillGeometry(geo.Get(), fill_brush.Get());
                }
              }

              if (obj.has_stroke && obj.stroke_width_mm > 0.0) {
                const float stroke_op = static_cast<float>(
                    obj.appearance.stroke_opacity * obj.appearance.object_opacity);
                auto stroke_brush = effects_pipeline.create_brush(
                    ctx, obj.stroke, stroke_op);
                if (stroke_brush) {
                  const float zoom = static_cast<float>(camera.zoom());
                  const float stroke_width_px = std::max(
                      1.0f / zoom, static_cast<float>(obj.stroke_width_mm));
                  ctx->DrawGeometry(geo.Get(), stroke_brush.Get(), stroke_width_px);
                }
              }
            };

            if (!has_effects) {
              draw_geometry_contents(context);
            } else {
              d2d::ComPtr<ID2D1CommandList> cmd_list;
              HRESULT hr = context->CreateCommandList(cmd_list.ReleaseAndGetAddressOf());
              if (SUCCEEDED(hr)) {
                d2d::ComPtr<ID2D1Image> old_target;
                context->GetTarget(old_target.ReleaseAndGetAddressOf());
                context->SetTarget(cmd_list.Get());

                draw_geometry_contents(context);

                cmd_list->Close();
                context->SetTarget(old_target.Get());

                const float zm = static_cast<float>(camera.zoom());
                const float hw = static_cast<float>(camera.width()) * 0.5f;
                const float hh = static_cast<float>(camera.height()) * 0.5f;
                const D2D1_MATRIX_3X2_F v_matrix =
                    D2D1::Matrix3x2F::Scale(zm, -zm) *
                    D2D1::Matrix3x2F::Translation(hw, hh);

                context->SetTransform(D2D1::Matrix3x2F::Identity());
                d2d::ComPtr<ID2D1Image> out_image;
                if (effects_pipeline.apply_effects(context, cmd_list.Get(),
                                                   obj.appearance, camera.zoom(),
                                                   out_image, diagnostics)) {
                  context->DrawImage(out_image.Get(), nullptr, nullptr,
                                     D2D1_INTERPOLATION_MODE_LINEAR,
                                     D2D1_COMPOSITE_MODE_SOURCE_OVER);
                } else {
                  context->DrawImage(cmd_list.Get(), nullptr, nullptr,
                                     D2D1_INTERPOLATION_MODE_LINEAR,
                                     D2D1_COMPOSITE_MODE_SOURCE_OVER);
                }
                context->SetTransform(v_matrix);
              } else {
                draw_geometry_contents(context);
              }
            }
            ++rendered_count;
            return true;
          } else if constexpr (std::is_same_v<T, document::TextObject>) {
#ifdef PIKDRAW_TYPOGRAPHY_REAL_OUTLINES
            try {
              const auto layout = typography::TextLayout::layout_text(obj.text, obj.font);
              auto path = typography::text_to_path_outlines(layout);
              const auto effective_transform = (parent_transform * obj.transform) *
                                               geometry::Transform2D::translation(
                                                   obj.position.x, obj.position.y) *
                                               geometry::Transform2D::scale(1, -1);
              if (!identity_transform(effective_transform)) {
                path = path.transformed(effective_transform);
              }
              auto geo = geometry_cache.get_or_create_path(
                  factory, obj.id, snapshot.revision(), path,
                  camera.center().x, camera.center().y);
              if (geo) {
                auto brush = effects_pipeline.create_brush(
                    context, obj.fill,
                    static_cast<float>(obj.appearance.fill_opacity *
                                       obj.appearance.object_opacity));
                if (brush) {
                  context->FillGeometry(geo.Get(), brush.Get());
                }
              }
              ++rendered_count;
              return true;
            } catch (...) {
              return false;
            }
#else
            if (obj.text.empty()) {
              ++rendered_count;
              return true;
            }
            return false;
#endif
          } else if constexpr (std::is_same_v<T, document::ImageObject>) {
            if (obj.pixels_rgba.empty() || obj.width_px <= 0 || obj.height_px <= 0) {
              return false;
            }
            std::vector<uint32_t> bgra_pixels;
            bgra_pixels.resize(obj.pixels_rgba.size());
            for (size_t i = 0; i < obj.pixels_rgba.size(); ++i) {
              uint32_t packed = obj.pixels_rgba[i];
              uint8_t r = static_cast<uint8_t>((packed >> 24) & 0xff);
              uint8_t g = static_cast<uint8_t>((packed >> 16) & 0xff);
              uint8_t b = static_cast<uint8_t>((packed >> 8) & 0xff);
              uint8_t a = static_cast<uint8_t>(packed & 0xff);
              float a_norm = static_cast<float>(a) / 255.0f;
              uint8_t r_p = static_cast<uint8_t>(static_cast<float>(r) * a_norm);
              uint8_t g_p = static_cast<uint8_t>(static_cast<float>(g) * a_norm);
              uint8_t b_p = static_cast<uint8_t>(static_cast<float>(b) * a_norm);
              bgra_pixels[i] = (static_cast<uint32_t>(a) << 24) |
                               (static_cast<uint32_t>(r_p) << 16) |
                               (static_cast<uint32_t>(g_p) << 8) |
                               static_cast<uint32_t>(b_p);
            }

            D2D1_BITMAP_PROPERTIES1 bmp_props{};
            bmp_props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
            bmp_props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
            bmp_props.dpiX = 96.0f;
            bmp_props.dpiY = 96.0f;
            bmp_props.bitmapOptions = D2D1_BITMAP_OPTIONS_NONE;

            D2D1_SIZE_U img_size = D2D1::SizeU(static_cast<UINT32>(obj.width_px),
                                               static_cast<UINT32>(obj.height_px));
            d2d::ComPtr<ID2D1Bitmap1> d2d_bitmap;
            HRESULT hr = context->CreateBitmap(
                img_size, bgra_pixels.data(), static_cast<UINT32>(obj.width_px * 4),
                &bmp_props, d2d_bitmap.ReleaseAndGetAddressOf());
            if (FAILED(hr)) {
              return false;
            }

            const auto effective_transform = parent_transform * obj.transform;
            auto mapped_rect = effective_transform.map_rect(obj.bounds_rect);
            float x0 = static_cast<float>(mapped_rect.x - camera.center().x);
            float x1 = static_cast<float>(mapped_rect.x + mapped_rect.width - camera.center().x);
            float y_top = static_cast<float>(mapped_rect.y + mapped_rect.height - camera.center().y);
            float y_bottom = static_cast<float>(mapped_rect.y - camera.center().y);
            D2D1_RECT_F dst_rect = D2D1::RectF(x0, y_top, x1, y_bottom);

            context->DrawBitmap(d2d_bitmap.Get(), &dst_rect);
            ++rendered_count;
            return true;
          } else {
            // GroupObject
            const auto child_transform = parent_transform * obj.transform;
            for (const auto child_id : obj.children_ids) {
              const auto *child = snapshot.objects().lookup_ref(child_id);
              if (child) {
                render_object(snapshot, *child, child_transform, camera,
                              context, factory, rendered_count, diagnostics,
                              depth + 1);
              }
            }
            ++rendered_count;
            return true;
          }
        },
        variant);
  }

  bool render_to_pixel_buffer(const document::DocumentSnapshot &snapshot,
                              const Camera2D &camera, PixelBufferTarget &target,
                              RenderMetric *out_metric) {
    if (should_use_software(target)) {
      return fallback_rasterizer.render(snapshot, camera, target, out_metric);
    }

    auto start_time = std::chrono::steady_clock::now();
    auto *context = device_mgr.context();
    auto *factory = device_mgr.factory();
    if (!context || !factory) {
      return fallback_rasterizer.render(snapshot, camera, target, out_metric);
    }

    // Create D2D bitmap target matching target dimensions
    D2D1_BITMAP_PROPERTIES1 props{};
    props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    props.dpiX = 96.0f;
    props.dpiY = 96.0f;
    props.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

    D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT32>(target.width),
                                   static_cast<UINT32>(target.height));
    d2d::ComPtr<ID2D1Bitmap1> render_bitmap;
    HRESULT hr = context->CreateBitmap(size, nullptr, 0, &props,
                                       render_bitmap.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
      return fallback_rasterizer.render(snapshot, camera, target, out_metric);
    }

    context->SetTarget(render_bitmap.Get());
    context->BeginDraw();
    context->Clear(D2D1::ColorF(background.r / 255.0f, background.g / 255.0f,
                                background.b / 255.0f, background.a / 255.0f));

    // Camera relative projection matrix: world +Y is up, screen +Y is down
    const float zoom = static_cast<float>(camera.zoom());
    const float half_w = static_cast<float>(camera.width()) * 0.5f;
    const float half_h = static_cast<float>(camera.height()) * 0.5f;

    D2D1_MATRIX_3X2_F view_matrix =
        D2D1::Matrix3x2F::Scale(zoom, -zoom) *
        D2D1::Matrix3x2F::Translation(half_w, half_h);
    context->SetTransform(view_matrix);

    // Visible bounds culling via SpatialIndex
    const auto visible_bounds = camera.visible_world_bounds();
    const auto visible_refs = spatial_index.query_visible_refs(
        snapshot, visible_bounds, camera.zoom(), 0.0);

    size_t rendered_count = 0;
    bool resource_failure = false;
    std::vector<RenderDiagnostic> diagnostics;

    for (const auto &ref : visible_refs) {
      if (!ref.object)
        continue;
      if (!render_object(snapshot, *ref.object, geometry::Transform2D::identity(),
                         camera, context, factory, rendered_count, diagnostics)) {
        resource_failure = true;
      }
    }

    hr = context->EndDraw();
    if (FAILED(hr)) {
      if (hr == D2DERR_RECREATE_TARGET || hr == DXGI_ERROR_DEVICE_REMOVED) {
        device_mgr.recover();
      }
      return fallback_rasterizer.render(snapshot, camera, target, out_metric);
    }
    if (resource_failure) {
      return fallback_rasterizer.render(snapshot, camera, target, out_metric);
    }

    // Read back bitmap pixels into target via Staging Bitmap
    if (target.pixels || target.premultiplied_rgba) {
      D2D1_BITMAP_PROPERTIES1 cpu_props = props;
      cpu_props.bitmapOptions = D2D1_BITMAP_OPTIONS_CANNOT_DRAW | D2D1_BITMAP_OPTIONS_CPU_READ;

      d2d::ComPtr<ID2D1Bitmap1> cpu_bitmap;
      hr = context->CreateBitmap(size, nullptr, 0, &cpu_props,
                                 cpu_bitmap.ReleaseAndGetAddressOf());
      if (SUCCEEDED(hr)) {
        D2D1_POINT_2U dst_pt{0, 0};
        hr = cpu_bitmap->CopyFromBitmap(&dst_pt, render_bitmap.Get(), nullptr);
        if (SUCCEEDED(hr)) {
          D2D1_MAPPED_RECT mapped{};
          hr = cpu_bitmap->Map(D2D1_MAP_OPTIONS_READ, &mapped);
          if (SUCCEEDED(hr)) {
            if (target.pixels) {
              for (int32_t y = 0; y < target.height; ++y) {
                const uint8_t *src_row = mapped.bits + static_cast<size_t>(y) * mapped.pitch;
                uint8_t *dst_row = target.pixels + static_cast<size_t>(y) * target.stride_bytes;
                for (int32_t x = 0; x < target.width; ++x) {
                  uint8_t b_prem = src_row[x * 4 + 0];
                  uint8_t g_prem = src_row[x * 4 + 1];
                  uint8_t r_prem = src_row[x * 4 + 2];
                  uint8_t a = src_row[x * 4 + 3];
                  if (a == 0) {
                    dst_row[x * 4 + 0] = 0;
                    dst_row[x * 4 + 1] = 0;
                    dst_row[x * 4 + 2] = 0;
                    dst_row[x * 4 + 3] = 0;
                  } else if (a == 255) {
                    dst_row[x * 4 + 0] = r_prem;
                    dst_row[x * 4 + 1] = g_prem;
                    dst_row[x * 4 + 2] = b_prem;
                    dst_row[x * 4 + 3] = 255;
                  } else {
                    dst_row[x * 4 + 0] = static_cast<uint8_t>(
                        std::clamp((static_cast<int>(r_prem) * 255 + a / 2) / a, 0, 255));
                    dst_row[x * 4 + 1] = static_cast<uint8_t>(
                        std::clamp((static_cast<int>(g_prem) * 255 + a / 2) / a, 0, 255));
                    dst_row[x * 4 + 2] = static_cast<uint8_t>(
                        std::clamp((static_cast<int>(b_prem) * 255 + a / 2) / a, 0, 255));
                    dst_row[x * 4 + 3] = a;
                  }
                }
              }
            }
            if (target.premultiplied_rgba) {
              for (int32_t y = 0; y < target.height; ++y) {
                const uint8_t *src_row = mapped.bits + static_cast<size_t>(y) * mapped.pitch;
                float *dst_row = target.premultiplied_rgba +
                                 static_cast<size_t>(y) * (target.stride_bytes / sizeof(float));
                for (int32_t x = 0; x < target.width; ++x) {
                  dst_row[x * 4 + 0] = static_cast<float>(src_row[x * 4 + 2]) / 255.0f; // R
                  dst_row[x * 4 + 1] = static_cast<float>(src_row[x * 4 + 1]) / 255.0f; // G
                  dst_row[x * 4 + 2] = static_cast<float>(src_row[x * 4 + 0]) / 255.0f; // B
                  dst_row[x * 4 + 3] = static_cast<float>(src_row[x * 4 + 3]) / 255.0f; // A
                }
              }
            }
            cpu_bitmap->Unmap();
          }
        }
      }
    }

    if (out_metric) {
      out_metric->snapshot_revision = snapshot.revision();
      out_metric->total_objects = snapshot.object_count();
      out_metric->rendered_objects = rendered_count;
      out_metric->culled_objects = snapshot.object_count() > rendered_count
                                       ? snapshot.object_count() - rendered_count
                                       : 0;
      out_metric->render_duration = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - start_time);
      out_metric->diagnostics = std::move(diagnostics);
    }

    return true;
  }
};

D2DRenderBackend::D2DRenderBackend() : impl_(std::make_unique<Impl>()) {}
D2DRenderBackend::~D2DRenderBackend() = default;
D2DRenderBackend::D2DRenderBackend(D2DRenderBackend &&) noexcept = default;
D2DRenderBackend &D2DRenderBackend::operator=(D2DRenderBackend &&) noexcept = default;

DeviceLossStatus D2DRenderBackend::device_status() const noexcept {
  return impl_->device_mgr.status();
}

bool D2DRenderBackend::recover_device() {
  return impl_->device_mgr.recover();
}

bool D2DRenderBackend::set_color_preview(
    std::shared_ptr<const color::IccPreview> preview) {
  impl_->color_preview = std::move(preview);
  return impl_->fallback_rasterizer.set_color_preview(impl_->color_preview);
}

bool D2DRenderBackend::set_canvas_background(color::DeviceRGBA background) {
  impl_->background = background;
  return impl_->fallback_rasterizer.set_canvas_background(background);
}

bool D2DRenderBackend::is_hardware_accelerated() const noexcept {
  return impl_->device_mgr.is_hardware();
}

bool D2DRenderBackend::render(const document::DocumentSnapshot &snapshot,
                             const Camera2D &camera, PixelBufferTarget &target,
                             RenderMetric *out_metric) {
  return impl_->render_to_pixel_buffer(snapshot, camera, target, out_metric);
}

bool D2DRenderBackend::render_gpu(const document::DocumentSnapshot &snapshot,
                                 const Camera2D &camera,
                                 const GpuSurfaceTarget &target,
                                 RenderMetric *out_metric) {
  if (!target.is_valid() || !impl_->device_mgr.is_valid()) {
    return false;
  }

  auto *context = impl_->device_mgr.context();
  auto *factory = impl_->device_mgr.factory();
  if (!context || !factory)
    return false;

  IDXGISurface *dxgi_surface = reinterpret_cast<IDXGISurface *>(target.native_resource);
  if (!dxgi_surface)
    return false;

  D2D1_BITMAP_PROPERTIES1 props{};
  props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
  props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
  props.dpiX = 96.0f * target.device_pixel_ratio;
  props.dpiY = 96.0f * target.device_pixel_ratio;
  props.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

  d2d::ComPtr<ID2D1Bitmap1> surface_bitmap;
  HRESULT hr = context->CreateBitmapFromDxgiSurface(
      dxgi_surface, &props, surface_bitmap.ReleaseAndGetAddressOf());
  if (FAILED(hr))
    return false;

  context->SetTarget(surface_bitmap.Get());
  context->BeginDraw();
  context->Clear(D2D1::ColorF(impl_->background.r / 255.0f,
                              impl_->background.g / 255.0f,
                              impl_->background.b / 255.0f,
                              impl_->background.a / 255.0f));

  const float zoom = static_cast<float>(camera.zoom());
  const float half_w = static_cast<float>(camera.width()) * 0.5f;
  const float half_h = static_cast<float>(camera.height()) * 0.5f;

  D2D1_MATRIX_3X2_F view_matrix =
      D2D1::Matrix3x2F::Scale(zoom, -zoom) *
      D2D1::Matrix3x2F::Translation(half_w, half_h);
  context->SetTransform(view_matrix);

  const auto visible_bounds = camera.visible_world_bounds();
  const auto visible_refs = impl_->spatial_index.query_visible_refs(
      snapshot, visible_bounds, camera.zoom(), 0.0);

  size_t rendered_count = 0;
  bool resource_failure = false;
  std::vector<RenderDiagnostic> diagnostics;

  for (const auto &ref : visible_refs) {
    if (!ref.object)
      continue;
    if (!impl_->render_object(snapshot, *ref.object, geometry::Transform2D::identity(),
                              camera, context, factory, rendered_count, diagnostics)) {
      resource_failure = true;
    }
  }

  hr = context->EndDraw();
  if (FAILED(hr)) {
    if (hr == D2DERR_RECREATE_TARGET || hr == DXGI_ERROR_DEVICE_REMOVED) {
      impl_->device_mgr.recover();
    }
    return false;
  }
  if (resource_failure) {
    return false;
  }

  if (out_metric) {
    out_metric->snapshot_revision = snapshot.revision();
    out_metric->total_objects = snapshot.object_count();
    out_metric->rendered_objects = rendered_count;
    out_metric->culled_objects = snapshot.object_count() > rendered_count
                                     ? snapshot.object_count() - rendered_count
                                     : 0;
    out_metric->diagnostics = std::move(diagnostics);
  }

  return true;
}

} // namespace pikdraw::rendering
