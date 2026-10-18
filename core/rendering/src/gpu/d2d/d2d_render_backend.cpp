#include "pikdraw/rendering/d2d_backend.hpp"
#include "d2d_internal.hpp"
#include "d2d_scene_compositor.hpp"
#include "pikdraw/rendering/spatial_index.hpp"
#include "pikdraw/typography/layout/glyph_outline.hpp"
#include "pikdraw/typography/layout/text_layout.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace pikdraw::rendering {

namespace {

D2D1_MATRIX_3X2_F object_projection(const geometry::Transform2D &object,
                                   const Camera2D &camera) {
  const double z = camera.zoom();
  // Subtract camera origin in binary64 before narrowing to D2D floats.
  return D2D1::Matrix3x2F(
      static_cast<float>(object.a * z), static_cast<float>(-object.b * z),
      static_cast<float>(object.c * z), static_cast<float>(-object.d * z),
      static_cast<float>((object.tx - camera.center().x) * z + camera.width() * 0.5),
      static_cast<float>(-(object.ty - camera.center().y) * z + camera.height() * 0.5));
}

class ScopedTransform {
public:
  ScopedTransform(ID2D1DeviceContext *context, const D2D1_MATRIX_3X2_F &matrix)
      : context_(context) {
    context_->GetTransform(&previous_);
    context_->SetTransform(matrix);
  }
  ~ScopedTransform() { context_->SetTransform(previous_); }
private:
  ID2D1DeviceContext *context_;
  D2D1_MATRIX_3X2_F previous_{};
};

} // namespace

struct D2DRenderBackend::Impl {
  d2d::D2DDeviceManager device_mgr;
  d2d::D2DGeometryCache geometry_cache;
  d2d::D2DImageCache image_cache;
  d2d::D2DEffectsPipeline effects_pipeline;
  SpatialIndex spatial_index;
  SoftwareRasterizer fallback_rasterizer;
  color::DeviceRGBA background{255, 255, 255, 255};
  std::shared_ptr<const color::IccPreview> color_preview;
  d2d::ComPtr<ID2D1Bitmap1> pixel_render_bitmap;
  d2d::ComPtr<ID2D1Bitmap1> pixel_staging_bitmap;
  D2D1_SIZE_U pixel_bitmap_size{};
  bool d2d_initialized{false};
  int32_t document_raster_dpi{0};

  Impl() {
    d2d_initialized = device_mgr.initialize();
  }

  bool should_use_software() const {
    const char *env = std::getenv("PIKDRAW_RENDER_BACKEND");
    if (env && std::string_view(env) == "cpu") {
      return true;
    }
    // Once Direct2D initialized, runtime failures must be surfaced as failures.
    // Replaying a partly rendered frame through the CPU backend hides GPU bugs.
    return !d2d_initialized;
  }

  void clear_pixel_bitmaps() {
    pixel_render_bitmap.Reset();
    pixel_staging_bitmap.Reset();
    pixel_bitmap_size = {};
  }

  bool recover_device() {
    clear_pixel_bitmaps();
    image_cache.clear();
    geometry_cache.clear();
    return device_mgr.recover();
  }

  void update_metric(const document::DocumentSnapshot &snapshot,
                     RenderMetric *out_metric, size_t rendered_count,
                     const std::vector<RenderDiagnostic> &diagnostics,
                     std::chrono::steady_clock::time_point start_time,
                     bool used_gpu) const {
    if (!out_metric)
      return;
    out_metric->used_gpu = used_gpu;
    out_metric->snapshot_revision = snapshot.revision();
    out_metric->total_objects = snapshot.object_count();
    out_metric->rendered_objects = rendered_count;
    out_metric->culled_objects = snapshot.object_count() > rendered_count
                                     ? snapshot.object_count() - rendered_count
                                     : 0;
    out_metric->render_duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start_time);
    out_metric->diagnostics = diagnostics;
  }

  template <typename DrawSource>
  bool draw_with_appearance(ID2D1DeviceContext *context,
                            const Camera2D &camera,
                            const document::Appearance &appearance,
                            document::ObjectId object_id,
                            d2d::SceneDrawState *scene,
                            DrawSource &&draw_source,
                            std::vector<RenderDiagnostic> &diagnostics) {
    if (appearance.effects.empty() &&
        appearance.blend_mode == document::BlendMode::Normal) {
      return draw_source(context);
    }

    d2d::ComPtr<ID2D1CommandList> command_list;
    HRESULT hr = context->CreateCommandList(command_list.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
      diagnostics.push_back({object_id, true,
                             "Direct2D could not allocate the object effects command list"});
      return false;
    }

    d2d::ComPtr<ID2D1Image> old_target;
    context->GetTarget(old_target.ReleaseAndGetAddressOf());
    D2D1_MATRIX_3X2_F old_transform{};
    context->GetTransform(&old_transform);
    context->SetTarget(command_list.Get());
    const bool source_drawn = draw_source(context);
    hr = command_list->Close();
    context->SetTarget(old_target.Get());
    context->SetTransform(old_transform);
    if (!source_drawn || FAILED(hr)) {
      diagnostics.push_back({object_id, true,
                             "Direct2D could not record the object effects source"});
      return false;
    }

    context->SetTransform(D2D1::Matrix3x2F::Identity());
    d2d::ComPtr<ID2D1Image> output_image;
    const bool effects_ok = effects_pipeline.apply_document_effects(
        context, command_list.Get(), appearance, object_id, camera.zoom(), document_raster_dpi,
        output_image, diagnostics, color_preview.get());
    if (effects_ok && appearance.blend_mode == document::BlendMode::Normal) {
      context->DrawImage(output_image.Get(), nullptr, nullptr,
                         D2D1_INTERPOLATION_MODE_LINEAR,
                         D2D1_COMPOSITE_MODE_SOURCE_OVER);
    }
    context->SetTransform(old_transform);
    if (!effects_ok)
      return false;
    if (appearance.blend_mode == document::BlendMode::Normal)
      return true;
    if (!scene) {
      diagnostics.push_back({object_id, true,
                             "Direct2D scene blend state is unavailable"});
      return false;
    }
    return d2d::blend_with_scene(context, output_image.Get(), appearance.blend_mode,
                                 object_id, *scene, diagnostics);
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
      d2d::SceneDrawState *scene,
      size_t depth = 0) {
    document_raster_dpi = (snapshot.page().color_mode_explicit || snapshot.page().raster_dpi_explicit)
        ? snapshot.raster_dpi() : 0;
    if (depth > 256) {
      diagnostics.push_back({document::kInvalidObjectId, true,
                             "Group nesting exceeds the Direct2D traversal limit"});
      return false;
    }

    return std::visit(
        [&](const auto &obj) -> bool {
          using T = std::decay_t<decltype(obj)>;
          const auto fail = [&](const char *message) {
            diagnostics.push_back({obj.id, true, message});
            return false;
          };
          if constexpr (std::is_same_v<T, document::PathObject>) {
            const auto effective_transform = parent_transform * obj.transform;
            auto geo = geometry_cache.get_or_create_path(
                factory, obj.id, snapshot.revision(), obj.path);
            if (!geo) {
              return fail("Direct2D could not create the path geometry");
            }

            auto draw_geometry_contents = [&](ID2D1DeviceContext *ctx) {
              ScopedTransform transform(ctx, object_projection(effective_transform, camera));
              if (obj.has_fill) {
                d2d::ComPtr<ID2D1Brush> fill_brush;
                const float fill_op = static_cast<float>(
                    obj.appearance.fill_opacity * obj.appearance.object_opacity);
                if (obj.fill_gradient) {
                  fill_brush = effects_pipeline.create_gradient_brush(
                      ctx, *obj.fill_gradient, fill_op, color_preview.get());
                } else {
                  fill_brush = effects_pipeline.create_brush(
                      ctx, obj.fill, fill_op, color_preview.get());
                }
                if (!fill_brush)
                  return false;
                ctx->FillGeometry(geo.Get(), fill_brush.Get());
              }

              if (obj.has_stroke && obj.stroke_width_mm > 0.0) {
                const float stroke_op = static_cast<float>(
                    obj.appearance.stroke_opacity * obj.appearance.object_opacity);
                auto stroke_brush = effects_pipeline.create_brush(
                    ctx, obj.stroke, stroke_op, color_preview.get());
                if (!stroke_brush)
                  return false;
                const float zoom = static_cast<float>(camera.zoom());
                const float stroke_width_px = std::max(
                    1.0f / zoom, static_cast<float>(obj.stroke_width_mm));
                ctx->DrawGeometry(geo.Get(), stroke_brush.Get(), stroke_width_px);
              }
              return true;
            };

            if (!draw_with_appearance(context, camera, obj.appearance, obj.id, scene,
                                      draw_geometry_contents, diagnostics))
              return fail("Direct2D failed to render path appearance");
            ++rendered_count;
            return true;
          } else if constexpr (std::is_same_v<T, document::TextObject>) {
#ifdef PIKDRAW_TYPOGRAPHY_REAL_OUTLINES
            try {
              if (obj.text.empty()) {
                ++rendered_count;
                return true;
              }
              const auto layout = obj.layout();
              auto path = typography::text_to_path_outlines(layout, obj.underline);
              const auto effective_transform = (parent_transform * obj.transform) *
                                               geometry::Transform2D::translation(
                                                   obj.position.x, obj.position.y) *
                                               geometry::Transform2D::scale(1, -1);
              d2d::D2DGeometryCache text_geometry;
              auto geo = text_geometry.get_or_create_path(
                  factory, obj.id, snapshot.revision(), path);
              if (!geo)
                return fail("Direct2D could not create text glyph-outline geometry");
              const auto draw_text = [&](ID2D1DeviceContext *ctx) {
                ScopedTransform transform(ctx, object_projection(effective_transform, camera));
                auto brush = effects_pipeline.create_brush(
                    ctx, obj.fill,
                    static_cast<float>(obj.appearance.fill_opacity *
                                       obj.appearance.object_opacity), color_preview.get());
                if (!brush)
                  return false;
                ctx->FillGeometry(geo.Get(), brush.Get());
                return true;
              };
              if (!draw_with_appearance(context, camera, obj.appearance, obj.id, scene,
                                        draw_text, diagnostics))
                return fail("Direct2D failed to render text appearance");
              ++rendered_count;
              return true;
            } catch (...) {
              return fail("DirectWrite failed while converting text to glyph outlines");
            }
#else
            if (obj.text.empty()) {
              ++rendered_count;
              return true;
            }
            return fail("This build does not provide DirectWrite glyph outlines");
#endif
          } else if constexpr (std::is_same_v<T, document::ImageObject>) {
            if (obj.width_px <= 0 || obj.height_px <= 0 ||
                obj.pixels_rgba.size() != static_cast<size_t>(obj.width_px) *
                                              static_cast<size_t>(obj.height_px)) {
              return fail("Image pixel dimensions do not match the stored RGBA pixels");
            }
            auto d2d_bitmap = image_cache.get(context, obj, snapshot.revision());
            if (!d2d_bitmap)
              return fail("Direct2D image upload failed or exceeded the 256 MiB budget");
            HRESULT hr = S_OK;

            const auto effective_transform = parent_transform * obj.transform;
            D2D1_RECT_F dst_rect = D2D1::RectF(
                static_cast<float>(obj.bounds_rect.x),
                static_cast<float>(obj.bounds_rect.y),
                static_cast<float>(obj.bounds_rect.right()),
                static_cast<float>(obj.bounds_rect.bottom()));
            ScopedTransform transform(context, object_projection(effective_transform, camera));

            d2d::ComPtr<ID2D1Layer> clip_layer;
            if (obj.clip_path) {
              auto clip_geometry = geometry_cache.get_or_create_path(
                  factory, obj.id, snapshot.revision(), *obj.clip_path, 1);
              if (!clip_geometry)
                return fail("Direct2D could not create the image clip geometry");
              hr = context->CreateLayer(nullptr, clip_layer.ReleaseAndGetAddressOf());
              if (FAILED(hr))
                return fail("Direct2D could not create an image clip layer");
              D2D1_LAYER_PARAMETERS1 layer_parameters{};
              layer_parameters.contentBounds = D2D1::InfiniteRect();
              layer_parameters.geometricMask = clip_geometry.Get();
              layer_parameters.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
              layer_parameters.maskTransform = D2D1::Matrix3x2F::Identity();
              layer_parameters.opacity = 1.0f;
              layer_parameters.opacityBrush = nullptr;
              layer_parameters.layerOptions = D2D1_LAYER_OPTIONS1_NONE;
              context->PushLayer(&layer_parameters, clip_layer.Get());
            }

            context->DrawBitmap(d2d_bitmap.Get(), &dst_rect, 1.0f,
                                D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
            if (clip_layer)
              context->PopLayer();
            ++rendered_count;
            return true;
          } else {
            // GroupObject
            const auto child_transform = parent_transform * obj.transform;
            for (const auto child_id : obj.children_ids) {
              const auto *child = snapshot.objects().lookup_ref(child_id);
              if (!child) {
                diagnostics.push_back({obj.id, true,
                                       "Group references a missing child object"});
                return false;
              }
              if (!render_object(snapshot, *child, child_transform, camera,
                                 context, factory, rendered_count, diagnostics,
                                 scene, depth + 1))
                return false;
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
    if (out_metric)
      out_metric->used_gpu = false;
    if (!target.is_valid()) {
      if (out_metric) {
        out_metric->diagnostics = {{document::kInvalidObjectId, true,
                                    "Invalid Direct2D pixel-buffer target"}};
      }
      return false;
    }
    if (should_use_software()) {
      return fallback_rasterizer.render(snapshot, camera, target, out_metric);
    }

    auto start_time = std::chrono::steady_clock::now();
    std::vector<RenderDiagnostic> diagnostics;
    size_t rendered_count = 0;
    auto fail = [&](const char *message) {
      diagnostics.push_back({document::kInvalidObjectId, true, message});
      update_metric(snapshot, out_metric, rendered_count, diagnostics,
                    start_time, false);
      return false;
    };
    auto *context = device_mgr.context();
    auto *factory = device_mgr.factory();
    if (!context || !factory)
      return fail("Direct2D initialized without a usable device context or factory");

    // Create D2D bitmap target matching target dimensions
    D2D1_BITMAP_PROPERTIES1 props{};
    props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    props.dpiX = 96.0f;
    props.dpiY = 96.0f;
    props.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

    D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT32>(target.width),
                                   static_cast<UINT32>(target.height));
    HRESULT hr = S_OK;
    if (!pixel_render_bitmap || pixel_bitmap_size.width != size.width ||
        pixel_bitmap_size.height != size.height) {
      clear_pixel_bitmaps();
      hr = context->CreateBitmap(size, nullptr, 0, &props,
                                 pixel_render_bitmap.ReleaseAndGetAddressOf());
      if (FAILED(hr))
        return fail("Direct2D could not allocate the GPU render target bitmap");
      pixel_bitmap_size = size;
    }

    context->SetTarget(pixel_render_bitmap.Get());
    context->BeginDraw();
    context->SetTransform(D2D1::Matrix3x2F::Identity());
    context->Clear(D2D1::ColorF(background.r / 255.0f, background.g / 255.0f,
                                background.b / 255.0f, background.a / 255.0f));

    // Camera relative projection matrix: world +Y is up, screen +Y is down
    const float zoom = static_cast<float>(camera.zoom());
    const float half_w = static_cast<float>(camera.width()) * 0.5f;
    const float half_h = static_cast<float>(camera.height()) * 0.5f;

    D2D1_MATRIX_3X2_F view_matrix =
        D2D1::Matrix3x2F::Scale(zoom, -zoom) *
        D2D1::Matrix3x2F::Translation(half_w, half_h);
    const bool paper_ok = d2d::draw_page(context, snapshot.page(), camera, background);
    context->SetTransform(view_matrix);
    d2d::SceneDrawState scene{};
    scene.target_bitmap = pixel_render_bitmap.Get();
    scene.size = size;
    scene.dpi_x = props.dpiX;
    scene.dpi_y = props.dpiY;
    scene.view_matrix = view_matrix;
    scene.drawing_active = true;

    // Visible bounds culling via SpatialIndex
    const auto visible_bounds = camera.visible_world_bounds();
    const auto visible_refs = spatial_index.query_visible_refs(
        snapshot, visible_bounds, camera.zoom(), 0.0);

    bool resource_failure = !paper_ok;

    for (const auto &ref : visible_refs) {
      if (!ref.object)
        continue;
      if (!render_object(snapshot, *ref.object, geometry::Transform2D::identity(),
                         camera, context, factory, rendered_count, diagnostics,
                         &scene)) {
        resource_failure = true;
      }
    }

    hr = scene.drawing_active ? context->EndDraw() : S_OK;
    scene.drawing_active = false;
    if (FAILED(hr)) {
      if (hr == D2DERR_RECREATE_TARGET || hr == DXGI_ERROR_DEVICE_REMOVED) {
        recover_device();
      }
      return fail("Direct2D EndDraw failed; the partial GPU frame was rejected");
    }
    if (resource_failure) {
      update_metric(snapshot, out_metric, rendered_count, diagnostics,
                    start_time, false);
      return false;
    }

    // Read back bitmap pixels into target via Staging Bitmap
    if (target.pixels || target.bgra_pixels || target.premultiplied_rgba) {
      D2D1_BITMAP_PROPERTIES1 cpu_props = props;
      cpu_props.bitmapOptions = D2D1_BITMAP_OPTIONS_CANNOT_DRAW | D2D1_BITMAP_OPTIONS_CPU_READ;

      if (!pixel_staging_bitmap) {
        hr = context->CreateBitmap(size, nullptr, 0, &cpu_props,
                                   pixel_staging_bitmap.ReleaseAndGetAddressOf());
        if (FAILED(hr))
          return fail("Direct2D could not allocate the CPU staging bitmap");
      }
      {
        D2D1_POINT_2U dst_pt{0, 0};
        hr = pixel_staging_bitmap->CopyFromBitmap(&dst_pt, pixel_render_bitmap.Get(), nullptr);
        if (FAILED(hr)) {
          pixel_staging_bitmap.Reset();
          return fail("Direct2D could not copy the GPU target into the staging bitmap");
        }
        {
          D2D1_MAPPED_RECT mapped{};
          hr = pixel_staging_bitmap->Map(D2D1_MAP_OPTIONS_READ, &mapped);
          if (FAILED(hr)) {
            pixel_staging_bitmap.Reset();
            return fail("Direct2D staging bitmap Map(READ) failed");
          }
          if (!mapped.bits || mapped.pitch < static_cast<UINT32>(target.width) * 4u) {
            pixel_staging_bitmap->Unmap();
            pixel_staging_bitmap.Reset();
            return fail("Direct2D staging bitmap returned an invalid mapped row pitch");
          }
          {
            if (target.bgra_pixels) {
              for (int32_t y = 0; y < target.height; ++y) {
                const auto* source = mapped.bits + static_cast<size_t>(y) * mapped.pitch;
                auto* destination = target.byte_pixel(0, y);
                std::memcpy(destination, source, static_cast<size_t>(target.width) * 4);
                for (int32_t x = 0; x < target.width; ++x) {
                  auto* pixel = destination + x * 4;
                  const auto alpha = pixel[3];
                  if (alpha == 255) continue;
                  for (int channel = 0; channel < 3; ++channel)
                    pixel[channel] = alpha == 0 ? 0 : static_cast<uint8_t>(
                        std::min(255u, (unsigned(pixel[channel]) * 255u + alpha / 2u) / alpha));
                }
              }
            }
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
            hr = pixel_staging_bitmap->Unmap();
            if (FAILED(hr)) {
              pixel_staging_bitmap.Reset();
              return fail("Direct2D staging bitmap Unmap failed after readback");
            }
          }
        }
      }
    }

    update_metric(snapshot, out_metric, rendered_count, diagnostics,
                  start_time, true);

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
  return impl_->recover_device();
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
  if (out_metric)
    out_metric->used_gpu = false;
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
  context->SetTransform(D2D1::Matrix3x2F::Identity());
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
  const bool paper_ok = d2d::draw_page(context, snapshot.page(), camera, impl_->background);
  context->SetTransform(view_matrix);
  d2d::SceneDrawState scene{};
  scene.target_bitmap = surface_bitmap.Get();
  scene.size = D2D1::SizeU(static_cast<UINT32>(target.width),
                           static_cast<UINT32>(target.height));
  scene.dpi_x = props.dpiX;
  scene.dpi_y = props.dpiY;
  scene.view_matrix = view_matrix;
  scene.drawing_active = true;

  const auto visible_bounds = camera.visible_world_bounds();
  const auto visible_refs = impl_->spatial_index.query_visible_refs(
      snapshot, visible_bounds, camera.zoom(), 0.0);

  size_t rendered_count = 0;
  bool resource_failure = !paper_ok;
  std::vector<RenderDiagnostic> diagnostics;

  for (const auto &ref : visible_refs) {
    if (!ref.object)
      continue;
    if (!impl_->render_object(snapshot, *ref.object, geometry::Transform2D::identity(),
                              camera, context, factory, rendered_count, diagnostics,
                              &scene)) {
      resource_failure = true;
    }
  }

  hr = scene.drawing_active ? context->EndDraw() : S_OK;
  scene.drawing_active = false;
  if (FAILED(hr)) {
    if (hr == D2DERR_RECREATE_TARGET || hr == DXGI_ERROR_DEVICE_REMOVED) {
      impl_->recover_device();
    }
    return false;
  }
  if (resource_failure) {
    return false;
  }

  if (out_metric) {
    out_metric->used_gpu = true;
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
