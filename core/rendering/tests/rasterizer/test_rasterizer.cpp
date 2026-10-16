#include "pikdraw/rendering/renderer.hpp"
#include "pikdraw/document/document.hpp"
#include <cassert>
#include <iostream>
#include <vector>

using namespace pikdraw;
using namespace pikdraw::rendering;
using namespace pikdraw::document;

void test_software_rasterizer_frame() {
    Document doc("doc-raster-test");

    PathObject po;
    po.id = generate_object_id();
    po.path.move_to({100, 100});
    po.path.line_to({300, 100});
    po.path.line_to({300, 300});
    po.path.close();
    doc.execute_command(std::make_unique<AddObjectCommand>(po));

    ViewportRenderer renderer;
    renderer.camera().set_center({200.0, 200.0});
    renderer.camera().set_zoom(1.0);

    const int32_t w = 400;
    const int32_t h = 300;
    std::vector<uint8_t> buffer(w * h * 4, 0);
    PixelBufferTarget target{buffer.data(), w, h, w * 4};

    RenderMetric metric;
    bool ok = renderer.render_frame(*doc.current_snapshot(), target, &metric);

    assert(ok);
    assert(metric.total_objects == 1);
    assert(metric.rendered_objects == 1);
    assert(metric.culled_objects == 0);

    // Pixel at (200, 150) is on the stroked diagonal line (black #000000)
    size_t stroke_idx = (150 * w + 200) * 4;
    assert(buffer[stroke_idx + 0] == 0);
    assert(buffer[stroke_idx + 1] == 0);
    assert(buffer[stroke_idx + 2] == 0);

    // Pixel at (250, 200) is inside the page (white paper #FFFFFF)
    size_t page_idx = (200 * w + 250) * 4;
    assert(buffer[page_idx + 0] == 255);
    assert(buffer[page_idx + 1] == 255);
    assert(buffer[page_idx + 2] == 255);

    // Now test off-page canvas workspace (still white #FFFFFF within 40m limit)
    renderer.camera().set_center({-5000.0, -5000.0}); // Outside page, within 40m canvas
    renderer.render_frame(*doc.current_snapshot(), target, &metric);
    assert(buffer[0] == 255);
    assert(buffer[1] == 255);
    assert(buffer[2] == 255);

    // Test far beyond 40m canvas workspace limit (outer void #12141a)
    renderer.camera().set_center({-50000.0, -50000.0}); // Far outside 40m limit
    renderer.render_frame(*doc.current_snapshot(), target, &metric);
    assert(buffer[0] == 18);
    assert(buffer[1] == 20);
    assert(buffer[2] == 26);
}

void test_device_loss_and_recovery() {
    auto rasterizer = std::make_shared<SoftwareRasterizer>();
    ViewportRenderer renderer(rasterizer);

    Document doc("doc-loss-test");
    const int32_t w = 200;
    const int32_t h = 200;
    std::vector<uint8_t> buffer(w * h * 4, 0);
    PixelBufferTarget target{buffer.data(), w, h, w * 4};

    // 1. Healthy render
    assert(renderer.render_frame(*doc.current_snapshot(), target));

    // 2. Simulate Recoverable Loss
    rasterizer->simulate_device_loss(DeviceLossStatus::RecoverableLoss);
    // ViewportRenderer detects recoverable loss and automatically recovers!
    bool recovered = renderer.render_frame(*doc.current_snapshot(), target);
    assert(recovered);
    assert(rasterizer->device_status() == DeviceLossStatus::Healthy);

    // 3. Simulate Fatal Native Driver Crash
    rasterizer->simulate_device_loss(DeviceLossStatus::FatalDriverCrash);
    bool fatal_ok = renderer.render_frame(*doc.current_snapshot(), target);
    assert(!fatal_ok);
    assert(rasterizer->device_status() == DeviceLossStatus::FatalDriverCrash);
}

int main() {
    std::cout << "[test_rasterizer] Running software rasterizer tests...\n";
    test_software_rasterizer_frame();
    test_device_loss_and_recovery();
    std::cout << "[test_rasterizer] All tests passed!\n";
    return 0;
}
