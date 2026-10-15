#pragma once

#include <QWidget>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include "pikdraw/rendering/camera.hpp"
#include "pikdraw/rendering/scene.hpp"
#include "pikdraw/rendering/trace.hpp"
#include "pikdraw/rendering/skia_backend.hpp"
#include <memory>
#include <vector>

namespace pikdraw::desktop {

class PikDrawViewportWidget : public QWidget {
    Q_OBJECT
public:
    explicit PikDrawViewportWidget(QWidget* parent = nullptr);
    ~PikDrawViewportWidget() override;

    void load_scene(rendering::ExperimentalSceneSnapshot scene);
    [[nodiscard]] const rendering::ViewportTracer& tracer() const noexcept { return tracer_; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    rendering::Camera2D camera_;
    rendering::ExperimentalSceneSnapshot scene_;
    rendering::ViewportTracer tracer_;
    std::unique_ptr<rendering::SkiaRasterBackend> backend_;
    bool is_panning_{false};
    QPoint last_mouse_pos_;
    uint64_t frame_counter_{0};
    std::vector<uint8_t> back_buffer_;
};

} // namespace pikdraw::desktop
