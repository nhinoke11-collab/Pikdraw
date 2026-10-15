#include "pikdraw/desktop/viewport_widget.hpp"
#include <QPainter>
#include <QImage>
#include <chrono>

namespace pikdraw::desktop {
namespace {
bool fits_preview_budget(int width, int height) {
    constexpr size_t max_bytes=64*1024*1024;
    return width>0 && height>0 && width<=rendering::Camera2D::MaxViewportDim &&
        height<=rendering::Camera2D::MaxViewportDim &&
        static_cast<size_t>(width)<=max_bytes/4/static_cast<size_t>(height);
}
}

PikDrawViewportWidget::PikDrawViewportWidget(QWidget* parent)
    : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(true);
    camera_.set_viewport_size(width(), height());
    backend_ = std::make_unique<rendering::SkiaRasterBackend>();
}

PikDrawViewportWidget::~PikDrawViewportWidget() = default;

void PikDrawViewportWidget::load_scene(rendering::ExperimentalSceneSnapshot scene) {
    scene_ = std::move(scene);
    update();
}

void PikDrawViewportWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (fits_preview_budget(width(),height()) && camera_.set_viewport_size(width(), height()))
        back_buffer_.resize(static_cast<size_t>(width()) * height() * 4);
}

void PikDrawViewportWidget::paintEvent(QPaintEvent*) {
    const auto t_start = std::chrono::steady_clock::now();
    rendering::FrameMetric metric;
    metric.frame_index = ++frame_counter_;

    const int w = width();
    const int h = height();
    if (!fits_preview_budget(w,h) || !camera_.set_viewport_size(w,h)) {
        QPainter painter(this); painter.fillRect(rect(),Qt::white);
        painter.drawText(rect(),Qt::AlignCenter,QStringLiteral("Preview exceeds the 64 MiB pixel buffer budget"));
        return;
    }

    if (back_buffer_.size() != static_cast<size_t>(w) * h * 4) {
        back_buffer_.resize(static_cast<size_t>(w) * h * 4);
    }

    rendering::PixelBufferTarget target{
        back_buffer_.data(),
        back_buffer_.size(),
        w,
        h,
        w * 4
    };

    if (!backend_->render_frame(camera_, scene_, target, metric)) {
        QPainter painter(this); painter.fillRect(rect(),Qt::white);
        painter.drawText(rect(),Qt::AlignCenter,QStringLiteral("Preview failed or contains unsupported content"));
        return;
    }

    const auto t_present_start = std::chrono::steady_clock::now();
    QPainter painter(this);
    QImage img(back_buffer_.data(), w, h, w * 4, QImage::Format_ARGB32_Premultiplied);
    painter.drawImage(0, 0, img);
    const auto t_present_end = std::chrono::steady_clock::now();

    metric.cpu_paint_submission_us = std::chrono::duration<double, std::micro>(t_present_end - t_present_start).count();
    metric.cpu_total_us = std::chrono::duration<double, std::micro>(t_present_end - t_start).count();
    metric.actual_present_not_measured = true;
    tracer_.record_frame(metric);
}

void PikDrawViewportWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton) {
        is_panning_ = true;
        last_mouse_pos_ = event->pos();
    }
}

void PikDrawViewportWidget::mouseMoveEvent(QMouseEvent* event) {
    if (is_panning_) {
        const QPoint delta = event->pos() - last_mouse_pos_;
        last_mouse_pos_ = event->pos();
        const double world_dx = static_cast<double>(delta.x()) / camera_.zoom();
        const double world_dy = static_cast<double>(delta.y()) / camera_.zoom();
        auto c = camera_.center();
        c.x -= world_dx;
        c.y -= world_dy;
        camera_.set_center(c);
        update();
    }
}

void PikDrawViewportWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton) {
        is_panning_ = false;
    }
}

void PikDrawViewportWidget::wheelEvent(QWheelEvent* event) {
    const double factor = (event->angleDelta().y() > 0) ? 1.15 : (1.0 / 1.15);
    camera_.set_zoom(camera_.zoom() * factor);
    update();
}

} // namespace pikdraw::desktop
