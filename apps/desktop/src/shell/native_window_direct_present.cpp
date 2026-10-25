#include "pikdraw/desktop/shell/native_window.hpp"

#include "pikdraw/rendering/d2d_backend.hpp"
#include "../performance/canvas_trace.hpp"

#include <cstdlib>
#include <string_view>

namespace pikdraw::desktop {

#ifdef _WIN32
namespace {

bool direct_present_requested() noexcept {
  const char* value = std::getenv("PIKDRAW_DIRECT_PRESENT");
  return value && std::string_view(value) == "1";
}

}  // namespace

void NativeWindow::cleanup_direct_present_target() noexcept {
  if (!direct_present_active_)
    return;
  for (auto& tab : tabs_) {
    if (!tab.root)
      continue;
    if (auto* backend = dynamic_cast<rendering::D2DRenderBackend*>(
            &tab.root->renderer().backend()))
      backend->release_swap_chain_target();
  }
  direct_present_active_ = false;
}

bool NativeWindow::paint_direct_present_frame(int width, int height) {
  if (!direct_present_requested()) {
    cleanup_direct_present_target();
    return false;
  }
  if (!hwnd_ || width <= 0 || height <= 0) {
    cleanup_direct_present_target();
    return false;
  }

  auto* backend = dynamic_cast<rendering::D2DRenderBackend*>(
      &root().renderer().backend());
  if (!backend) {
    cleanup_direct_present_target();
    return false;
  }
  for (auto& tab : tabs_) {
    if (!tab.root || &tab.root->renderer().backend() == backend)
      continue;
    if (auto* other = dynamic_cast<rendering::D2DRenderBackend*>(
            &tab.root->renderer().backend()))
      other->release_swap_chain_target();
  }

  const int target_width = dip(width);
  const int target_height = dip(height);
  if (!backend->create_swap_chain_target(
          hwnd_, target_width, target_height, dpi_scale_)) {
    cleanup_direct_present_target();
    return false;
  }
  direct_present_active_ = true;

  auto* dc = static_cast<HDC>(backend->acquire_swap_chain_target_dc());
  if (!dc) {
    cleanup_direct_present_target();
    return false;
  }
  {
    struct RestoreDirectFrame {
      bool& flag;
      ~RestoreDirectFrame() { flag = false; }
    } restore{direct_present_frame_};
    direct_present_frame_ = true;
    paint(dc, width, height);
  }
  if (!backend->release_swap_chain_target_dc()) {
    cleanup_direct_present_target();
    return false;
  }

  const auto present_gdi_fallback = [&]() {
    auto* fallback_dc = static_cast<HDC>(backend->acquire_swap_chain_target_dc());
    if (!fallback_dc)
      return false;
    direct_present_frame_ = false;
    paint(fallback_dc, width, height);
    if (!backend->release_swap_chain_target_dc())
      return false;
    return backend->present_swap_chain_target(1);
  };

  if (!show_home_screen_) {
    const auto snapshot = cached_canvas_snapshot_;
    const RECT area = canvas_rect(width, height);
    const int canvas_width = area.right - area.left;
    const int canvas_height = area.bottom - area.top;
    if (!snapshot || canvas_width <= 0 || canvas_height <= 0) {
      if (!present_gdi_fallback()) {
        cleanup_direct_present_target();
        return false;
      }
      return true;
    }

    auto& camera = root().renderer().camera();
    const auto saved_camera = camera;
    camera.set_viewport_size(canvas_width, canvas_height);
    auto target = backend->swap_chain_target();
    target.viewport_x = static_cast<float>(area.left);
    target.viewport_y = static_cast<float>(area.top);
    target.viewport_width = static_cast<float>(canvas_width);
    target.viewport_height = static_cast<float>(canvas_height);
    if (canvas_trace_)
      record_performance_event(CanvasTraceKind::RenderBegin,
          active_trace_event_id_, active_trace_frame_id_);
    const bool rendered = backend->render_gpu(*snapshot, camera, target);
    if (canvas_trace_)
      record_performance_event(CanvasTraceKind::RenderEnd,
          active_trace_event_id_, active_trace_frame_id_);
    camera = saved_camera;
    if (!rendered) {
      if (!present_gdi_fallback()) {
        cleanup_direct_present_target();
        return false;
      }
      return true;
    }

    auto* overlay_dc = static_cast<HDC>(backend->acquire_swap_chain_target_dc());
    if (!overlay_dc) {
      if (!present_gdi_fallback()) {
        cleanup_direct_present_target();
        return false;
      }
      return true;
    }
    configure_ui_dc(overlay_dc);
    paint_canvas_overlay_layer(overlay_dc, width, height);
    paint_canvas_interaction_handles(overlay_dc, width, height);
    if (!backend->release_swap_chain_target_dc()) {
      cleanup_direct_present_target();
      return false;
    }
  }

  auto* top_layer_dc = static_cast<HDC>(backend->acquire_swap_chain_target_dc());
  if (!top_layer_dc) {
    if (!present_gdi_fallback()) {
      cleanup_direct_present_target();
      return false;
    }
    return true;
  }
  paint_topmost_ui_layer(top_layer_dc, width, height);
  if (!backend->release_swap_chain_target_dc()) {
    cleanup_direct_present_target();
    return false;
  }

  if (!backend->present_swap_chain_target(1)) {
    cleanup_direct_present_target();
    return false;
  }
  return true;
}
#endif

}  // namespace pikdraw::desktop
