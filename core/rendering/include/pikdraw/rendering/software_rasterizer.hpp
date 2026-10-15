#pragma once

#include "pikdraw/rendering/backend.hpp"
#include "pikdraw/rendering/culling.hpp"
#include "pikdraw/color/preview/color_transform.hpp"

namespace pikdraw::rendering {

/// CPU Software Rasterizer baseline fulfilling ADR 0002 software fallback requirements
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

    bool render(
        const document::DocumentSnapshot& snapshot,
        const Camera2D& camera,
        PixelBufferTarget& target,
        RenderMetric* out_metric = nullptr) override;

private:
    void render_page_background(
        const document::Page& page,
        const Camera2D& camera,
        PixelBufferTarget& target);

    void render_path_object(
        const document::PathObject& obj,
        const Camera2D& camera,
        PixelBufferTarget& target);

    void render_text_object(
        const document::TextObject& obj,
        const Camera2D& camera,
        PixelBufferTarget& target);

    void draw_line_screen(
        int32_t x0, int32_t y0, int32_t x1, int32_t y1,
        color::DeviceRGBA color,
        PixelBufferTarget& target);

    void draw_rect_filled_screen(
        int32_t x, int32_t y, int32_t w, int32_t h,
        color::DeviceRGBA color,
        PixelBufferTarget& target);

    DeviceLossStatus loss_status_{DeviceLossStatus::Healthy};
};

} // namespace pikdraw::rendering
