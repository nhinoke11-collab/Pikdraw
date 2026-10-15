#pragma once

#include "pikdraw/color/preview/color_transform.hpp"
#include "pikdraw/rendering/backend.hpp"
#include "pikdraw/rendering/culling.hpp"

namespace pikdraw::rendering {

/// Bounded pixel-center CPU baseline; not a GPU or performance acceptance
/// claim.
class SoftwareRasterizer : public IRenderBackend {
public:
  SoftwareRasterizer() = default;

  [[nodiscard]] DeviceLossStatus device_status() const noexcept override {
    return loss_status_;
  }

  bool recover_device() override {
    if (loss_status_ == DeviceLossStatus::FatalDriverCrash) {
      return false;
    }
    loss_status_ = DeviceLossStatus::Healthy;
    return true;
  }

  void simulate_device_loss(DeviceLossStatus status) noexcept {
    loss_status_ = status;
  }

  bool render(const document::DocumentSnapshot &snapshot,
              const Camera2D &camera, PixelBufferTarget &target,
              RenderMetric *out_metric = nullptr) override;

private:
  DeviceLossStatus loss_status_{DeviceLossStatus::Healthy};
};

} // namespace pikdraw::rendering
