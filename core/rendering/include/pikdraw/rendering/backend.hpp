#pragma once

#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include "pikdraw/rendering/camera.hpp"
#include "pikdraw/rendering/frame_target.hpp"
#include "pikdraw/color/preview/icc_preview.hpp"
#include <chrono>
#include <string>
#include <vector>

namespace pikdraw::rendering {

enum class DeviceLossStatus { Healthy, RecoverableLoss, FatalDriverCrash };

struct RenderDiagnostic {
  document::ObjectId object_id{document::kInvalidObjectId};
  bool error{false};
  std::string message;
};

struct RenderMetric {
  uint64_t snapshot_revision{0};
  size_t total_objects{0};
  size_t culled_objects{0};
  size_t rendered_objects{0};
  std::chrono::microseconds render_duration{0};
  // Peak live vector capacity observed while a Gaussian Blur node executes.
  // Includes isolated source/filter surfaces and Gaussian temporary buffers;
  // excludes allocator metadata, the caller target, geometry, and Drop Shadow
  // internals. This is a capacity measure, not process resident memory.
  size_t peak_gaussian_blur_scratch_capacity_bytes{0};
  // Peak live vector capacity observed while a Drop Shadow node executes.
  // Includes object-local source/current surfaces, alpha conversion, shadow
  // result, and Drop Shadow/Gaussian temporary vectors; excludes allocator
  // metadata, the caller target/backdrop, and process memory.
  size_t peak_drop_shadow_scratch_capacity_bytes{0};
  // Caller must surface errors/warnings. Software renderer caps this to 65
  // entries.
  std::vector<RenderDiagnostic> diagnostics;
};

class IRenderBackend {
public:
  virtual ~IRenderBackend() = default;

  [[nodiscard]] virtual DeviceLossStatus device_status() const noexcept = 0;
  virtual bool recover_device() = 0;
  virtual bool set_color_preview(std::shared_ptr<const color::IccPreview>) { return false; }
  virtual bool set_canvas_background(color::DeviceRGBA) { return false; }

  virtual bool render(const document::DocumentSnapshot &snapshot,
                      const Camera2D &camera, PixelBufferTarget &target,
                      RenderMetric *out_metric = nullptr) = 0;
};

} // namespace pikdraw::rendering
