#include "../../powerclip_view.hpp"
#include "pikdraw/rendering/d2d_backend.hpp"
#include "d2d_internal.hpp"
#include "../../pdf_source_preview.hpp"
#include "d2d_scene_compositor.hpp"
#include "pikdraw/document/commands/contour_commands.hpp"
#include "pikdraw/document/annotations/dimension_layout.hpp"
#include "pikdraw/document/annotations/dimension_object.hpp"
#include "pikdraw/document/annotations/annotation_layout.hpp"
#include "pikdraw/document/annotations/annotation_object.hpp"
#include "pikdraw/rendering/spatial_index.hpp"
#include "pikdraw/typography/layout/glyph_outline.hpp"
#include "pikdraw/typography/layout/text_layout.hpp"
#include "pikdraw/typography/layout/text_on_path.hpp"

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

bool is_device_lost(HRESULT hr) noexcept {
  return hr == D2DERR_RECREATE_TARGET || hr == DXGI_ERROR_DEVICE_REMOVED ||
         hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}

D2D1_MATRIX_3X2_F object_projection(const geometry::Transform2D &object,
                                   const Camera2D &camera,
                                   float viewport_origin_x = 0.0f,
                                   float viewport_origin_y = 0.0f) {
  const double z = camera.zoom();
  // Subtract camera origin in binary64 before narrowing to D2D floats.
  return D2D1::Matrix3x2F(
      static_cast<float>(object.a * z), static_cast<float>(-object.b * z),
      static_cast<float>(object.c * z), static_cast<float>(-object.d * z),
      static_cast<float>((object.tx - camera.center().x) * z +
                         camera.width() * 0.5 + viewport_origin_x),
      static_cast<float>(-(object.ty - camera.center().y) * z +
                         camera.height() * 0.5 + viewport_origin_y));
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
  RenderViewContext view;
  document::ObjectId excluded_owner{0};
  color::DeviceRGBA background{255, 255, 255, 255};
  std::shared_ptr<const color::IccPreview> color_preview;
  RasterImageResolver raster_image_resolver;
  d2d::ComPtr<ID2D1Bitmap1> pixel_render_bitmap;
  d2d::ComPtr<ID2D1Bitmap1> pixel_staging_bitmap;
  d2d::ComPtr<IDXGISwapChain1> swap_chain;
  d2d::ComPtr<IDXGISurface1> swap_chain_gdi_surface;
  HDC swap_chain_gdi_dc{};
  HWND swap_chain_hwnd{};
  int32_t swap_chain_width{};
  int32_t swap_chain_height{};
  float swap_chain_dpi_ratio{1.0f};
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

  void clear_swap_chain() {
    if (swap_chain_gdi_surface && swap_chain_gdi_dc)
      (void)swap_chain_gdi_surface->ReleaseDC(nullptr);
    swap_chain_gdi_surface.Reset();
    swap_chain_gdi_dc = nullptr;
    if (auto *context = device_mgr.context())
      context->SetTarget(nullptr);
    swap_chain.Reset();
    swap_chain_hwnd = nullptr;
    swap_chain_width = 0;
    swap_chain_height = 0;
    swap_chain_dpi_ratio = 1.0f;
  }

  bool create_swap_chain(HWND hwnd, int32_t width, int32_t height,
                         float dpi_ratio) {
    if (!hwnd || !IsWindow(hwnd) || width <= 0 || height <= 0 ||
        !std::isfinite(dpi_ratio) || dpi_ratio <= 0.0f ||
        !device_mgr.is_valid())
      return false;

    if (swap_chain && swap_chain_hwnd == hwnd) {
      if (swap_chain_gdi_dc)
        return false;
      if (width != swap_chain_width || height != swap_chain_height) {
        if (auto *context = device_mgr.context())
          context->SetTarget(nullptr);
        const HRESULT resize_hr = swap_chain->ResizeBuffers(
            0, static_cast<UINT>(width), static_cast<UINT>(height),
            DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE);
        if (FAILED(resize_hr)) {
          if (is_device_lost(resize_hr))
            recover_device();
          return false;
        }
        swap_chain_width = width;
        swap_chain_height = height;
      }
      swap_chain_dpi_ratio = dpi_ratio;
      return true;
    }
    if (swap_chain)
      clear_swap_chain();

    d2d::ComPtr<IDXGIDevice> dxgi_device;
    d2d::ComPtr<IDXGIAdapter> adapter;
    d2d::ComPtr<IDXGIFactory2> factory;
    if (FAILED(device_mgr.d3d_device()->QueryInterface(
            IID_PPV_ARGS(dxgi_device.ReleaseAndGetAddressOf()))) ||
        FAILED(dxgi_device->GetAdapter(adapter.ReleaseAndGetAddressOf())) ||
        FAILED(adapter->GetParent(IID_PPV_ARGS(factory.ReleaseAndGetAddressOf()))))
      return false;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE;

    d2d::ComPtr<IDXGISwapChain1> created;
    const HRESULT hr = factory->CreateSwapChainForHwnd(
        device_mgr.d3d_device(), hwnd, &desc, nullptr, nullptr,
        created.ReleaseAndGetAddressOf());
    if (FAILED(hr))
      return false;

    if (auto *context = device_mgr.context())
      context->SetTarget(nullptr);
    swap_chain = std::move(created);
    swap_chain_hwnd = hwnd;
    swap_chain_width = width;
    swap_chain_height = height;
    swap_chain_dpi_ratio = dpi_ratio;
    return true;
  }

  bool recover_device() {
    const HWND hwnd = swap_chain_hwnd;
    const int32_t width = swap_chain_width;
    const int32_t height = swap_chain_height;
    const float dpi_ratio = swap_chain_dpi_ratio;
    clear_swap_chain();
    clear_pixel_bitmaps();
    image_cache.clear();
    geometry_cache.clear();
    if (!device_mgr.recover())
      return false;
    return !hwnd || create_swap_chain(hwnd, width, height, dpi_ratio);
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
    if (!effects_ok) {
      context->DrawImage(command_list.Get(), nullptr, nullptr,
                         D2D1_INTERPOLATION_MODE_LINEAR,
                         D2D1_COMPOSITE_MODE_SOURCE_OVER);
      return true;
    }
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
      size_t depth = 0,
      bool exclude_derived_annotations = false) {
    document_raster_dpi = (snapshot.page().color_mode_explicit || snapshot.page().raster_dpi_explicit)
        ? snapshot.raster_dpi() : 0;
    if (depth > 256) {
      diagnostics.push_back({document::kInvalidObjectId, true,
                             "Group nesting exceeds the Direct2D traversal limit"});
      return false;
    }

    const auto project = [&camera, scene](const geometry::Transform2D &transform) {
      return object_projection(transform, camera,
          scene ? scene->viewport_origin_x : 0.0f,
          scene ? scene->viewport_origin_y : 0.0f);
    };

    return std::visit(
        [&](const auto &obj) -> bool {
          using T = std::decay_t<decltype(obj)>;
          if (!obj.visible || (excluded_owner && obj.id == excluded_owner)) return true;
          const auto fail = [&](const char *message) {
            diagnostics.push_back({obj.id, true, message});
            return false;
          };
          if constexpr (std::is_same_v<T, document::PathObject>) {
            if (exclude_derived_annotations &&
                (obj.contour_spec.starts_with("annotation:v1") ||
                 obj.contour_spec.starts_with("detail_view:v1")))
              return true;
            const auto effective_transform = parent_transform * obj.transform;
            auto geo = geometry_cache.get_or_create_path(
                factory, obj.id, snapshot.revision(), obj.path);
            if (!geo) {
              return fail("Direct2D could not create the path geometry");
            }

            auto draw_geometry_contents = [&](ID2D1DeviceContext *ctx) {
              ScopedTransform transform(ctx, project(effective_transform));
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

              if (obj.has_stroke) {
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

              if (obj.contour_spec.starts_with("detail_view:v1")) {
                const auto detail = document::deserialize_detail_view_spec(
                    obj.contour_spec);
                if (!detail) return fail("Invalid detail view source metadata");
                if (detail->source_page_id != snapshot.active_page_id())
                  return fail("Detail view source page is not the active page");
                const auto layout = document::evaluate_detail_view_layout(*detail);
                auto frame_geo = geometry_cache.get_or_create_path(
                    factory, obj.id, snapshot.revision(), layout.frame_path, 199);
                if (!frame_geo)
                  return fail("Direct2D could not create the detail view frame mask");
                d2d::ComPtr<ID2D1TransformedGeometry> transformed_mask;
                const auto mask_transform = project(effective_transform);
                HRESULT mask_hr = factory->CreateTransformedGeometry(
                    frame_geo.Get(), &mask_transform,
                    transformed_mask.ReleaseAndGetAddressOf());
                if (FAILED(mask_hr))
                  return fail("Direct2D could not transform the detail view frame mask");
                d2d::ComPtr<ID2D1Layer> detail_layer;
                mask_hr = ctx->CreateLayer(nullptr, detail_layer.ReleaseAndGetAddressOf());
                if (FAILED(mask_hr))
                  return fail("Direct2D could not allocate the detail view clip layer");

                D2D1_MATRIX_3X2_F previous_transform{};
                ctx->GetTransform(&previous_transform);
                ctx->SetTransform(D2D1::Matrix3x2F::Identity());
                const auto layer_parameters = D2D1::LayerParameters(
                    D2D1::InfiniteRect(), transformed_mask.Get(),
                    D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                    D2D1::Matrix3x2F::Identity(), 1.0f, nullptr,
                    D2D1_LAYER_OPTIONS_NONE);
                ctx->PushLayer(layer_parameters, detail_layer.Get());
                bool source_ok = true;
                const double source_zoom = camera.zoom() * detail->scale_ratio;
                const auto source_refs = SpatialCuller::query_visible_refs(
                    snapshot, detail->source_region, source_zoom,
                    source_zoom > 0.0 ? 1.0 / source_zoom : 0.0);
                const auto source_transform =
                    effective_transform * layout.source_to_frame_transform;
                for (const auto &source_ref : source_refs) {
                  if (!source_ref.object || source_ref.id == obj.id) continue;
                  if (!render_object(snapshot, *source_ref.object, source_transform,
                                     camera, ctx, factory, rendered_count,
                                     diagnostics, scene, depth + 1, true)) {
                    source_ok = false;
                    break;
                  }
                }
                ctx->PopLayer();
                ctx->SetTransform(previous_transform);
                if (!source_ok) return false;

                // Keep the frame edge visible above the clipped source scene.
                {
                  ScopedTransform frame_transform(ctx, project(effective_transform));
                  const float zoom = static_cast<float>(camera.zoom());
                  const float width = std::max(1.0f / zoom,
                      static_cast<float>(obj.stroke_width_mm));
                  auto frame_brush = effects_pipeline.create_brush(
                      ctx, obj.stroke, 1.0f, color_preview.get());
                  if (!frame_brush)
                    return fail("Direct2D could not create the detail view frame brush");
                  ctx->DrawGeometry(frame_geo.Get(), frame_brush.Get(), width);
                }
              }

              if (obj.contour_spec.starts_with("contour:v1;")) {
                auto feat = document::deserialize_contour_feature(obj.contour_spec);
                if (feat) {
                  if (obj.contour_paints) {
                    feat->outline_color = obj.contour_paints->outline;
                    feat->fill_color = obj.contour_paints->fill;
                  }
                  const auto res = document::evaluate_contour(obj, *feat);
                  const float zoom = static_cast<float>(camera.zoom());
                  const float contour_w = std::max(1.0f / zoom,
                      static_cast<float>(feat->stroke_width_mm > 0.0 ? feat->stroke_width_mm : 0.2));
                  d2d::ComPtr<ID2D1Brush> stroke_b;
                  if (feat->has_outline) {
                    stroke_b = effects_pipeline.create_brush(
                        ctx, feat->outline_color, 1.0f, color_preview.get());
                  }
                  for (size_t s = 0; s < res.steps.size(); ++s) {
                    const auto& step = res.steps[s];
                    if (step.is_empty) continue;
                    for (size_t l = 0; l < step.loops.size(); ++l) {
                      auto loop_geo = geometry_cache.get_or_create_path(
                          factory, obj.id, snapshot.revision(), step.loops[l],
                          static_cast<uint8_t>(20 + (s * 4 + l) % 200));
                      if (!loop_geo) continue;
                      if (feat->has_fill) {
                        auto fill_b = effects_pipeline.create_brush(
                            ctx, feat->fill_color, 1.0f, color_preview.get());
                        if (fill_b) ctx->FillGeometry(loop_geo.Get(), fill_b.Get());
                      }
                      if (stroke_b)
                        ctx->DrawGeometry(loop_geo.Get(), stroke_b.Get(), contour_w);
                    }
                  }
                }
              }

              if (obj.contour_spec.starts_with("dimension:v1")) {
                const auto dim = document::deserialize_dimension_spec(obj.contour_spec);
                if (dim) {
                  const auto dim_layout = document::evaluate_dimension_layout(*dim, snapshot);
                  if (dim_layout.valid) {
                    if (!dim_layout.arrows.empty()) {
                      auto arr_brush = effects_pipeline.create_brush(
                          ctx, dim->style.line_color, 1.0f, color_preview.get());
                      if (arr_brush) {
                        for (size_t a_idx = 0; a_idx < dim_layout.arrows.size(); ++a_idx) {
                          auto arr_geo = geometry_cache.get_or_create_path(
                              factory, obj.id, snapshot.revision(), dim_layout.arrows[a_idx],
                              static_cast<uint8_t>(210 + a_idx));
                          if (!arr_geo) continue;
                          if (dim->style.arrow_type == geometry::ArrowType::ClosedFilled ||
                              dim->style.arrow_type == geometry::ArrowType::Dot)
                            ctx->FillGeometry(arr_geo.Get(), arr_brush.Get());
                          else
                            ctx->DrawGeometry(arr_geo.Get(), arr_brush.Get(),
                                static_cast<float>((std::max)(
                                    0.1, dim->style.line_width_mm)));
                        }
                      }
                    }
                    if (!dim_layout.formatted_text.empty()) {
#ifdef PIKDRAW_TYPOGRAPHY_REAL_OUTLINES
                      try {
                        typography::FontIdentity font{typography::FontIdentity::make_default()};
                        font.size_pt = (dim->style.text_size_mm > 0.5 ? dim->style.text_size_mm : 3.0) * (72.0 / 25.4);
                        typography::LayoutOptions opts;
                        opts.alignment = typography::TextAlignment::Center;
                        const auto t_layout = typography::TextLayout::layout_text(dim_layout.formatted_text, font, opts);
                        const auto t_path = typography::text_to_path_outlines(t_layout, false);
                        const auto tb = t_path.bounds();

                        const double tw2 = tb.width * 0.5;
                        const double th2 = tb.height * 0.5;

                        geometry::Transform2D t_xform =
                            geometry::Transform2D::translation(dim_layout.text_position.x, dim_layout.text_position.y) *
                            geometry::Transform2D::rotation_radians(dim_layout.text_rotation_rad) *
                            geometry::Transform2D::translation(-tw2, th2) *
                            geometry::Transform2D::scale(1.0, -1.0);

                        const auto final_t_path = t_path.transformed(t_xform);
                        auto text_geo = geometry_cache.get_or_create_path(
                            factory, obj.id, snapshot.revision(), final_t_path, 254);
                        if (text_geo) {
                          auto text_b = effects_pipeline.create_brush(
                              ctx, dim->style.text_color, 1.0f, color_preview.get());
                          if (text_b) {
                            ctx->FillGeometry(text_geo.Get(), text_b.Get());
                          }
                        }
                      } catch (...) {}
#endif
                    }
                  }
                }
              }

              if (obj.contour_spec.starts_with("annotation:v1")) {
                const auto parsed_annotation =
                    document::deserialize_annotation_spec(obj.contour_spec);
                if (!parsed_annotation) {
                  diagnostics.push_back({obj.id, true,
                      "Invalid annotation metadata; annotation display is incomplete"});
                  return false;
                } else {
                  auto ann_val = *parsed_annotation;
                  ann_val.style.color = obj.stroke;
                  if (!ann_val.text.empty() || !ann_val.leader_points.empty()) {
                    const auto ann_layout = document::evaluate_annotation_layout(ann_val);
                    if (!ann_layout.arrow_path.empty()) {
                      auto arr_brush = effects_pipeline.create_brush(
                          ctx, ann_val.style.color, 1.0f, color_preview.get());
                      if (!arr_brush) {
                        diagnostics.push_back({obj.id, true,
                            "Failed to create Direct2D annotation arrow brush"});
                        return false;
                      }
                      auto arr_geo = geometry_cache.get_or_create_path(
                          factory, obj.id, snapshot.revision(), ann_layout.arrow_path, 220);
                      if (!arr_geo) {
                        diagnostics.push_back({obj.id, true,
                            "Failed to create Direct2D annotation arrow geometry"});
                        return false;
                      }
                      if (ann_val.style.arrow_type == geometry::ArrowType::ClosedFilled ||
                          ann_val.style.arrow_type == geometry::ArrowType::Dot) {
                        ctx->FillGeometry(arr_geo.Get(), arr_brush.Get());
                      } else {
                        ctx->DrawGeometry(arr_geo.Get(), arr_brush.Get(),
                            static_cast<float>((std::max)(0.1, ann_val.style.line_width_mm)));
                      }
                    }
                    if (!ann_layout.formatted_text.empty()) {
#ifdef PIKDRAW_TYPOGRAPHY_REAL_OUTLINES
                      try {
                        typography::FontIdentity font{typography::FontIdentity::make_default()};
                        font.family_name = ann_val.style.font_family;
                        font.size_pt = (ann_val.style.text_size_mm > 0.5 ? ann_val.style.text_size_mm : 2.5) * (72.0 / 25.4);
                        typography::LayoutOptions opts;
                        opts.alignment = typography::TextAlignment::Left;
                        const auto t_layout = typography::TextLayout::layout_text(ann_layout.formatted_text, font, opts);
                        const auto t_path = typography::text_to_path_outlines(t_layout, false);

                        geometry::Transform2D t_xform =
                            geometry::Transform2D::translation(ann_layout.text_origin.x, ann_layout.text_origin.y) *
                            geometry::Transform2D::scale(1.0, -1.0);

                        const auto final_t_path = t_path.transformed(t_xform);
                        auto text_geo = geometry_cache.get_or_create_path(
                            factory, obj.id, snapshot.revision(), final_t_path, 253);
                        if (!text_geo) {
                          diagnostics.push_back({obj.id, true,
                              "Failed to create Direct2D annotation text geometry"});
                          return false;
                        }
                        auto text_b = effects_pipeline.create_brush(
                            ctx, ann_val.style.color, 1.0f, color_preview.get());
                        if (!text_b) {
                          diagnostics.push_back({obj.id, true,
                              "Failed to create Direct2D annotation text brush"});
                          return false;
                        }
                        ctx->FillGeometry(text_geo.Get(), text_b.Get());
                      } catch (...) {
                        diagnostics.push_back({obj.id, true,
                            "Failed to generate Direct2D annotation text outlines"});
                        return false;
                      }
#else
                      diagnostics.push_back({obj.id, true,
                          "Annotation text rendering requires native font outlines"});
                      return false;
#endif
                    }
                  }
                }
              }

              if (obj.contour_spec.starts_with("detail_view:v1")) {
                const auto parsed_detail_view =
                    document::deserialize_detail_view_spec(obj.contour_spec);
                if (!parsed_detail_view) {
                  diagnostics.push_back({obj.id, true,
                      "Invalid detail view metadata; detail view display is incomplete"});
                  return false;
                } else {
                  auto dv = *parsed_detail_view;
                  dv.color = obj.stroke;
                  const auto dv_layout = document::evaluate_detail_view_layout(dv);
                  if (!dv_layout.formatted_label.empty()) {
#ifdef PIKDRAW_TYPOGRAPHY_REAL_OUTLINES
                    try {
                      typography::FontIdentity font{typography::FontIdentity::make_default()};
                      font.family_name = "Segoe UI";
                      font.size_pt = (dv.text_size_mm > 0.5 ? dv.text_size_mm : 2.5) * (72.0 / 25.4);
                      typography::LayoutOptions opts;
                      opts.alignment = typography::TextAlignment::Center;
                      const auto t_layout = typography::TextLayout::layout_text(dv_layout.formatted_label, font, opts);
                      const auto t_path = typography::text_to_path_outlines(t_layout, false);
                      const auto tb = t_path.bounds();

                      geometry::Transform2D t_xform =
                          geometry::Transform2D::translation(dv_layout.label_origin.x, dv_layout.label_origin.y + tb.height) *
                          geometry::Transform2D::scale(1.0, -1.0);

                      const auto final_t_path = t_path.transformed(t_xform);
                      auto text_geo = geometry_cache.get_or_create_path(
                          factory, obj.id, snapshot.revision(), final_t_path, 252);
                      if (!text_geo) {
                        diagnostics.push_back({obj.id, true,
                            "Failed to create Direct2D detail view label geometry"});
                        return false;
                      }
                      auto text_b = effects_pipeline.create_brush(
                          ctx, dv.color, 1.0f, color_preview.get());
                      if (!text_b) {
                        diagnostics.push_back({obj.id, true,
                            "Failed to create Direct2D detail view label brush"});
                        return false;
                      }
                      ctx->FillGeometry(text_geo.Get(), text_b.Get());
                    } catch (...) {
                      diagnostics.push_back({obj.id, true,
                          "Failed to generate Direct2D detail view label outlines"});
                      return false;
                    }
#else
                    diagnostics.push_back({obj.id, true,
                        "Detail view label rendering requires native font outlines"});
                    return false;
#endif
                  }
                }
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
              geometry::Path2D path;
              geometry::Transform2D effective_transform;
              if (obj.is_text_on_path()) {
                auto path_opt = snapshot.objects().get(obj.path_binding_id);
                if (path_opt && std::holds_alternative<document::PathObject>(*path_opt)) {
                  const auto& host_path = std::get<document::PathObject>(*path_opt);
                  typography::TextOnPathOptions top_opts;
                  top_opts.start_offset_mm = obj.path_offset_mm;
                  top_opts.baseline_offset_mm = obj.path_baseline_offset_mm;
                  top_opts.reversed = obj.path_reversed;
                  top_opts.alignment = obj.alignment;
                  auto top_res = typography::layout_text_on_path(host_path.path, obj.text, obj.font, top_opts);
                  path = std::move(top_res.combined_outlines);
                  effective_transform = (parent_transform * host_path.transform) * geometry::Transform2D::scale(1, -1);
                }
              }
              if (path.empty()) {
                const auto layout = obj.layout();
                path = typography::text_to_path_outlines(layout, obj.underline, obj.strikethrough);
                effective_transform = (parent_transform * obj.transform) *
                                                 geometry::Transform2D::translation(
                                                     obj.position.x, obj.position.y) *
                                                 geometry::Transform2D::scale(1, -1);
              }
              auto geo = geometry_cache.get_or_create_path(
                  factory, obj.id, snapshot.revision(), path, 3);
              if (!geo)
                return fail("Direct2D could not create text glyph-outline geometry");
              const auto draw_text = [&](ID2D1DeviceContext *ctx) {
                ScopedTransform transform(ctx, project(effective_transform));
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
            const auto resolved = raster_image_resolver
                ? raster_image_resolver(snapshot, obj) : nullptr;
            const auto& image = resolved ? *resolved : obj;
            const auto pixels = image.rgba_pixels();
            if (image.width_px <= 0 || image.height_px <= 0 ||
                pixels.size() != static_cast<size_t>(image.width_px) *
                                              static_cast<size_t>(image.height_px)) {
              return fail("Image pixel dimensions do not match the stored RGBA pixels");
            }
            auto d2d_bitmap = image_cache.get(context, image, snapshot.revision());
            if (!d2d_bitmap)
              return fail("Direct2D could not allocate or upload the image bitmap");
            HRESULT hr = S_OK;

            const auto effective_transform = parent_transform * image.transform;
            D2D1_RECT_F dst_rect = D2D1::RectF(
                static_cast<float>(image.bounds_rect.x),
                static_cast<float>(image.bounds_rect.y),
                static_cast<float>(image.bounds_rect.right()),
                static_cast<float>(image.bounds_rect.bottom()));
            ScopedTransform transform(context, project(effective_transform));

            d2d::ComPtr<ID2D1Layer> clip_layer;
            if (image.clip_path) {
              auto clip_geometry = geometry_cache.get_or_create_path(
                  factory, image.id, snapshot.revision(), *image.clip_path, 1);
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
                                D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
            if (clip_layer)
              context->PopLayer();
            ++rendered_count;
            return true;
          } else {
            // GroupObject
            const auto child_transform = parent_transform * obj.transform;
            if (obj.is_clipping_container()) {
              const auto* mask_child = snapshot.objects().lookup_ref(obj.clip->mask_object_id);
              const auto mask_geometry = mask_child ? document::frame_geometry(*mask_child) : std::nullopt;
              if (mask_geometry) {
                const auto& mask_obj = *mask_geometry;
                const auto mask_effective_transform = child_transform * mask_obj.transform;
                auto mask_geo = geometry_cache.get_or_create_path(
                    factory, mask_obj.id, snapshot.revision(), mask_obj.path);

                if (obj.clip->preserve_frame_appearance && mask_obj.has_fill && mask_geo) {
                  ScopedTransform transform(context, project(mask_effective_transform));
                  d2d::ComPtr<ID2D1Brush> fill_brush;
                  const float fill_op = static_cast<float>(
                      mask_obj.appearance.fill_opacity * mask_obj.appearance.object_opacity);
                  if (mask_obj.fill_gradient) {
                    fill_brush = effects_pipeline.create_gradient_brush(
                        context, *mask_obj.fill_gradient, fill_op, color_preview.get());
                  } else {
                    fill_brush = effects_pipeline.create_brush(
                        context, mask_obj.fill, fill_op, color_preview.get());
                  }
                  if (fill_brush) {
                    context->FillGeometry(mask_geo.Get(), fill_brush.Get());
                  }
                }

                bool pushed_layer = false;
                D2D1_MATRIX_3X2_F prev_layer_xform{};
                if (mask_geo) {
                  d2d::ComPtr<ID2D1TransformedGeometry> transformed_mask;
                  const auto mask_xform = project(mask_effective_transform);
                  HRESULT hr_geo = factory->CreateTransformedGeometry(mask_geo.Get(), &mask_xform, transformed_mask.ReleaseAndGetAddressOf());
                  if (SUCCEEDED(hr_geo)) {
                    context->GetTransform(&prev_layer_xform);
                    context->SetTransform(D2D1::Matrix3x2F::Identity());
                    context->PushLayer(
                        D2D1::LayerParameters(
                            D2D1::InfiniteRect(),
                            transformed_mask.Get(),
                            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                            D2D1::Matrix3x2F::Identity(),
                            1.0f,
                            nullptr,
                            D2D1_LAYER_OPTIONS_NONE),
                        nullptr);
                    pushed_layer = true;
                  }
                }

                for (const auto child_id : obj.children_ids) {
                  if (child_id == obj.clip->mask_object_id) continue;
                  const auto *child = snapshot.objects().lookup_ref(child_id);
                  if (!child) {
                    diagnostics.push_back({obj.id, true,
                                           "Group references a missing child object"});
                    if (pushed_layer) {
                      context->PopLayer();
                      context->SetTransform(prev_layer_xform);
                    }
                    return false;
                  }
                  if (!render_object(snapshot, *child, child_transform, camera,
                                     context, factory, rendered_count, diagnostics,
                                     scene, depth + 1,
                                     exclude_derived_annotations)) {
                    if (pushed_layer) {
                      context->PopLayer();
                      context->SetTransform(prev_layer_xform);
                    }
                    return false;
                  }
                }

                if (pushed_layer) {
                  context->PopLayer();
                  context->SetTransform(prev_layer_xform);
                }

                if (obj.clip->preserve_frame_appearance && mask_obj.has_stroke && mask_geo) {
                  ScopedTransform transform(context, project(mask_effective_transform));
                  const float stroke_op = static_cast<float>(
                      mask_obj.appearance.stroke_opacity * mask_obj.appearance.object_opacity);
                  auto stroke_brush = effects_pipeline.create_brush(
                      context, mask_obj.stroke, stroke_op, color_preview.get());
                  if (stroke_brush) {
                    const float zoom = static_cast<float>(camera.zoom());
                    const float stroke_width_px = std::max(
                        1.0f / zoom, static_cast<float>(mask_obj.stroke_width_mm));
                    context->DrawGeometry(mask_geo.Get(), stroke_brush.Get(), stroke_width_px);
                  }
                }

                ++rendered_count;
                return true;
              }
            }

            if (obj.is_clipping_container()) return fail("Unsupported PowerClip frame geometry");
            for (const auto child_id : obj.children_ids) {
              const auto *child = snapshot.objects().lookup_ref(child_id);
              if (!child) {
                diagnostics.push_back({obj.id, true,
                                       "Group references a missing child object"});
                return false;
              }
              if (!render_object(snapshot, *child, child_transform, camera,
                                 context, factory, rendered_count, diagnostics,
                                 scene, depth + 1,
                                 exclude_derived_annotations))
                return false;
            }
            ++rendered_count;
            return true;
          }
        },
        variant);
  }

  bool render_edit_view(const document::DocumentSnapshot& snapshot,const Camera2D& camera,
      ID2D1DeviceContext* context,ID2D1Factory1* factory,size_t& rendered_count,
      std::vector<RenderDiagnostic>& diagnostics,d2d::SceneDrawState* scene,int width,int height) {
    bool resource_failure=false;
    if (view.owner) {
      const auto* entry = snapshot.hierarchy().find(view.owner);
      const auto* object = snapshot.objects().lookup_ref(view.owner);
      const auto* group = object ? std::get_if<document::GroupObject>(object) : nullptr;
      if (!entry || !group) resource_failure = true;
      else {
        ScopedTransform identity(context, D2D1::Matrix3x2F::Identity());
        d2d::ComPtr<ID2D1SolidColorBrush> wash;
        if (FAILED(context->CreateSolidColorBrush(D2D1::ColorF(.94f,.94f,.94f,.67f), &wash))) resource_failure = true;
        else context->FillRectangle(D2D1::RectF(0,0,float(width),float(height)), wash.Get());
        excluded_owner = 0;
        if (!view.reveal_contents || view.edit_frame) {
          if (!render_object(snapshot, *object, entry->parent_world, camera, context, factory,
              rendered_count, diagnostics, scene)) resource_failure = true;
        } else for (auto id : group->children_ids) {
          if (group->clip && id == group->clip->mask_object_id) continue;
          const auto* child = snapshot.objects().lookup_ref(id);
          if (!child || !render_object(snapshot, *child, entry->world, camera, context, factory,
              rendered_count, diagnostics, scene)) resource_failure = true;
        }
        if (auto outline = detail::clip_frame_outline(snapshot, view.owner, camera))
          if (!render_object(snapshot, document::ObjectVariant{*outline}, {}, camera, context,
              factory, rendered_count, diagnostics, scene)) resource_failure = true;
      }
    }
    return !resource_failure;
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
    image_cache.begin_frame(device_mgr.d3d_device());

    if (auto source_preview = detail::make_pdf_source_preview(snapshot.active_page())) {
      document::ObjectVariant preview_object{std::move(*source_preview)};
      if (!render_object(snapshot, preview_object, geometry::Transform2D::identity(),
                         camera, context, factory, rendered_count, diagnostics,
                         &scene))
        resource_failure = true;
    }

    for (const auto &ref : visible_refs) {
      if (!ref.object)
        continue;
      if (!render_object(snapshot, *ref.object, geometry::Transform2D::identity(),
                         camera, context, factory, rendered_count, diagnostics,
                         &scene)) {
        resource_failure = true;
      }
    }

    if (!render_edit_view(snapshot,camera,context,factory,rendered_count,diagnostics,&scene,target.width,target.height)) resource_failure=true;
    hr = scene.drawing_active ? context->EndDraw() : S_OK;
    scene.drawing_active = false;
    if (FAILED(hr)) {
      if (hr == D2DERR_RECREATE_TARGET || hr == DXGI_ERROR_DEVICE_REMOVED) {
        recover_device();
      }
      return fail("Direct2D EndDraw failed; the partial GPU frame was rejected");
    }
    if (resource_failure) {
      if (image_cache.memory_pressure_encountered()) {
        // Do not reject an imported image because its GPU cache allocation is
        // blocked by system pressure. The software renderer consumes the
        // already-decoded immutable source pixels into the caller's target,
        // avoiding another full-image upload buffer and keeping the canvas
        // usable while applications release RAM.
        RenderMetric fallback_metric{};
        const bool fallback_ok = fallback_rasterizer.render(
            snapshot, camera, target, &fallback_metric);
        if (out_metric) *out_metric = std::move(fallback_metric);
        return fallback_ok;
      }
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

bool D2DRenderBackend::supports_effect(
    const document::EffectParameters &parameters) const noexcept {
  return std::visit([](const auto &effect) noexcept {
    using T = std::decay_t<decltype(effect)>;
    if constexpr (std::is_same_v<T, document::GaussianBlurEffect>) {
      return true;
    } else if constexpr (std::is_same_v<T, document::DropShadowEffect>) {
      return effect.blend_mode == document::BlendMode::Normal;
    }
    return false;
  }, parameters);
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

bool D2DRenderBackend::create_swap_chain_target(void *hwnd, int32_t width,
                                                int32_t height,
                                                float device_pixel_ratio) {
  return impl_->create_swap_chain(static_cast<HWND>(hwnd), width, height,
                                  device_pixel_ratio);
}

GpuSurfaceTarget D2DRenderBackend::swap_chain_target() const noexcept {
  return {GpuTargetType::DxgiSwapChain, impl_->swap_chain.Get(),
          impl_->swap_chain_width, impl_->swap_chain_height,
          impl_->swap_chain_dpi_ratio};
}

void D2DRenderBackend::release_swap_chain_target() noexcept {
  impl_->clear_swap_chain();
}

bool D2DRenderBackend::resize_swap_chain_target(int32_t width,
                                                int32_t height) {
  if (!impl_->swap_chain || impl_->swap_chain_gdi_dc ||
      width <= 0 || height <= 0)
    return false;
  if (width == impl_->swap_chain_width && height == impl_->swap_chain_height)
    return true;
  if (auto *context = impl_->device_mgr.context())
    context->SetTarget(nullptr);
  const HRESULT hr = impl_->swap_chain->ResizeBuffers(
      0, static_cast<UINT>(width), static_cast<UINT>(height),
      DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE);
  if (FAILED(hr)) {
    if (is_device_lost(hr))
      impl_->recover_device();
    return false;
  }
  impl_->swap_chain_width = width;
  impl_->swap_chain_height = height;
  return true;
}

bool D2DRenderBackend::present_swap_chain_target(uint32_t sync_interval) {
  if (!impl_->swap_chain || impl_->swap_chain_gdi_dc || sync_interval > 4)
    return false;
  if (auto *context = impl_->device_mgr.context())
    context->SetTarget(nullptr);
  const HRESULT hr = impl_->swap_chain->Present(sync_interval, 0);
  if (is_device_lost(hr)) {
    impl_->recover_device();
    return false;
  }
  return SUCCEEDED(hr);
}

void *D2DRenderBackend::acquire_swap_chain_target_dc() {
  if (!impl_->swap_chain || impl_->swap_chain_gdi_dc)
    return nullptr;
  d2d::ComPtr<IDXGISurface1> surface;
  if (FAILED(impl_->swap_chain->GetBuffer(
          0, IID_PPV_ARGS(surface.ReleaseAndGetAddressOf()))))
    return nullptr;
  HDC dc = nullptr;
  if (FAILED(surface->GetDC(FALSE, &dc)) || !dc)
    return nullptr;
  impl_->swap_chain_gdi_surface = std::move(surface);
  impl_->swap_chain_gdi_dc = dc;
  return dc;
}

bool D2DRenderBackend::release_swap_chain_target_dc() {
  if (!impl_->swap_chain_gdi_surface || !impl_->swap_chain_gdi_dc)
    return false;
  const HRESULT hr = impl_->swap_chain_gdi_surface->ReleaseDC(nullptr);
  impl_->swap_chain_gdi_surface.Reset();
  impl_->swap_chain_gdi_dc = nullptr;
  return SUCCEEDED(hr);
}

bool D2DRenderBackend::capture_swap_chain_target(PixelBufferTarget &target) {
  if (!target.is_valid() || !impl_->swap_chain ||
      impl_->swap_chain_gdi_dc ||
      target.width != impl_->swap_chain_width ||
      target.height != impl_->swap_chain_height || !impl_->device_mgr.is_valid())
    return false;

  d2d::ComPtr<ID3D11Texture2D> back_buffer;
  if (FAILED(impl_->swap_chain->GetBuffer(
          0, IID_PPV_ARGS(back_buffer.ReleaseAndGetAddressOf()))))
    return false;
  D3D11_TEXTURE2D_DESC desc{};
  back_buffer->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.MiscFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  d2d::ComPtr<ID3D11Texture2D> staging;
  if (FAILED(impl_->device_mgr.d3d_device()->CreateTexture2D(
          &desc, nullptr, staging.ReleaseAndGetAddressOf())))
    return false;

  auto *d3d_context = impl_->device_mgr.d3d_context();
  if (!d3d_context)
    return false;
  d3d_context->CopyResource(staging.Get(), back_buffer.Get());
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(d3d_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
    return false;
  for (int32_t y = 0; y < target.height; ++y) {
    const auto *src = static_cast<const uint8_t *>(mapped.pData) +
                      static_cast<size_t>(y) * mapped.RowPitch;
    for (int32_t x = 0; x < target.width; ++x) {
      const auto *pixel = src + static_cast<size_t>(x) * 4;
      target.set_pixel(x, y, pixel[2], pixel[1], pixel[0], pixel[3]);
    }
  }
  d3d_context->Unmap(staging.Get(), 0);
  return true;
}

bool D2DRenderBackend::render(const document::DocumentSnapshot &snapshot,
                             const Camera2D &camera, PixelBufferTarget &target,
                             RenderMetric *out_metric) {
  impl_->view = view_context_;
  impl_->excluded_owner = view_context_.owner;
  impl_->raster_image_resolver = raster_image_resolver_;
  impl_->fallback_rasterizer.set_raster_image_resolver(raster_image_resolver_);
  impl_->fallback_rasterizer.set_view_context(view_context_);
  return impl_->render_to_pixel_buffer(snapshot, camera, target, out_metric);
}

bool D2DRenderBackend::render_gpu(const document::DocumentSnapshot &snapshot,
                                 const Camera2D &camera,
                                 const GpuSurfaceTarget &target,
                                 RenderMetric *out_metric) {
  impl_->view=view_context_;impl_->excluded_owner=view_context_.owner;
  impl_->raster_image_resolver = raster_image_resolver_;
  impl_->fallback_rasterizer.set_raster_image_resolver(raster_image_resolver_);
  if (out_metric)
    out_metric->used_gpu = false;
  if (!target.is_valid() || impl_->swap_chain_gdi_dc ||
      !std::isfinite(target.device_pixel_ratio) ||
      target.device_pixel_ratio <= 0.0f || !impl_->device_mgr.is_valid()) {
    return false;
  }

  auto *context = impl_->device_mgr.context();
  auto *factory = impl_->device_mgr.factory();
  if (!context || !factory)
    return false;

  d2d::ComPtr<IDXGISurface> swap_chain_surface;
  IDXGISurface *dxgi_surface = nullptr;
  if (target.type == GpuTargetType::DxgiSwapChain) {
    auto *swap_chain = reinterpret_cast<IDXGISwapChain1 *>(target.native_resource);
    if (!swap_chain || FAILED(swap_chain->GetBuffer(
                            0, IID_PPV_ARGS(swap_chain_surface.ReleaseAndGetAddressOf()))))
      return false;
    dxgi_surface = swap_chain_surface.Get();
  } else if (target.type == GpuTargetType::GpuTexture) {
    dxgi_surface = reinterpret_cast<IDXGISurface *>(target.native_resource);
  } else {
    return false;
  }
  DXGI_SURFACE_DESC surface_desc{};
  if (FAILED(dxgi_surface->GetDesc(&surface_desc)) ||
      surface_desc.Width != static_cast<UINT>(target.width) ||
      surface_desc.Height != static_cast<UINT>(target.height) ||
      surface_desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM ||
      surface_desc.SampleDesc.Count != 1)
    return false;

  if (!std::isfinite(target.viewport_x) || !std::isfinite(target.viewport_y) ||
      !std::isfinite(target.viewport_width) ||
      !std::isfinite(target.viewport_height) || target.viewport_x < 0.0f ||
      target.viewport_y < 0.0f || target.viewport_width < 0.0f ||
      target.viewport_height < 0.0f)
    return false;
  const double surface_width_dips =
      static_cast<double>(target.width) / target.device_pixel_ratio;
  const double surface_height_dips =
      static_cast<double>(target.height) / target.device_pixel_ratio;
  const float viewport_x = target.viewport_x;
  const float viewport_y = target.viewport_y;
  const float viewport_width = target.viewport_width == 0.0f
      ? static_cast<float>(surface_width_dips) : target.viewport_width;
  const float viewport_height = target.viewport_height == 0.0f
      ? static_cast<float>(surface_height_dips) : target.viewport_height;
  if (viewport_width <= 0.0f || viewport_height <= 0.0f ||
      static_cast<double>(viewport_x) + viewport_width > surface_width_dips + 0.01 ||
      static_cast<double>(viewport_y) + viewport_height > surface_height_dips + 0.01)
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
  const auto viewport = D2D1::RectF(
      static_cast<float>(viewport_x), static_cast<float>(viewport_y),
      static_cast<float>(viewport_x + viewport_width),
      static_cast<float>(viewport_y + viewport_height));
  const bool full_surface = viewport_x == 0 && viewport_y == 0 &&
      std::abs(viewport_width - surface_width_dips) < 0.01 &&
      std::abs(viewport_height - surface_height_dips) < 0.01;
  if (full_surface) {
    context->Clear(D2D1::ColorF(impl_->background.r / 255.0f,
                                impl_->background.g / 255.0f,
                                impl_->background.b / 255.0f,
                                impl_->background.a / 255.0f));
  } else {
    d2d::ComPtr<ID2D1SolidColorBrush> background_brush;
    if (FAILED(context->CreateSolidColorBrush(
            D2D1::ColorF(impl_->background.r / 255.0f,
                         impl_->background.g / 255.0f,
                         impl_->background.b / 255.0f,
                         impl_->background.a / 255.0f),
            background_brush.ReleaseAndGetAddressOf()))) {
      (void)context->EndDraw();
      context->SetTarget(nullptr);
      return false;
    }
    context->PushAxisAlignedClip(viewport, D2D1_ANTIALIAS_MODE_ALIASED);
    context->FillRectangle(viewport, background_brush.Get());
  }

  const float zoom = static_cast<float>(camera.zoom());
  const float half_w = static_cast<float>(camera.width()) * 0.5f;
  const float half_h = static_cast<float>(camera.height()) * 0.5f;

  D2D1_MATRIX_3X2_F view_matrix =
      D2D1::Matrix3x2F::Scale(zoom, -zoom) *
      D2D1::Matrix3x2F::Translation(half_w + viewport_x,
                                    half_h + viewport_y);
  const bool paper_ok = d2d::draw_page(context, snapshot.page(), camera,
                                      impl_->background,
                                      viewport_x, viewport_y);
  context->SetTransform(view_matrix);
  d2d::SceneDrawState scene{};
  scene.target_bitmap = surface_bitmap.Get();
  scene.size = D2D1::SizeU(static_cast<UINT32>(target.width),
                           static_cast<UINT32>(target.height));
  scene.dpi_x = props.dpiX;
  scene.dpi_y = props.dpiY;
  scene.view_matrix = view_matrix;
  scene.drawing_active = true;
  scene.viewport_origin_x = viewport_x;
  scene.viewport_origin_y = viewport_y;

  const auto visible_bounds = camera.visible_world_bounds();
  const auto visible_refs = impl_->spatial_index.query_visible_refs(
      snapshot, visible_bounds, camera.zoom(), 0.0);

  size_t rendered_count = 0;
  bool resource_failure = !paper_ok;
  std::vector<RenderDiagnostic> diagnostics;
  impl_->image_cache.begin_frame(impl_->device_mgr.d3d_device());

  if (auto source_preview = detail::make_pdf_source_preview(snapshot.active_page())) {
    document::ObjectVariant preview_object{std::move(*source_preview)};
    if (!impl_->render_object(snapshot, preview_object,
                              geometry::Transform2D::identity(), camera, context,
                              factory, rendered_count, diagnostics, &scene))
      resource_failure = true;
  }

  for (const auto &ref : visible_refs) {
    if (!ref.object)
      continue;
    if (!impl_->render_object(snapshot, *ref.object, geometry::Transform2D::identity(),
                              camera, context, factory, rendered_count, diagnostics,
                              &scene)) {
      resource_failure = true;
    }
  }

  if(!impl_->render_edit_view(snapshot,camera,context,factory,rendered_count,diagnostics,&scene,target.width,target.height)) resource_failure=true;
  if (!full_surface)
    context->PopAxisAlignedClip();
  hr = scene.drawing_active ? context->EndDraw() : S_OK;
  scene.drawing_active = false;
  context->SetTarget(nullptr);
  if (FAILED(hr)) {
    if (is_device_lost(hr)) {
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
