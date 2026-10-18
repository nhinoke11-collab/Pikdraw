#include "pikdraw/rendering/software_rasterizer.hpp"
#include "pikdraw/rendering/d2d_backend.hpp"
#include "pikdraw/rendering/damage_tracker.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/document/commands/color_commands.hpp"
#include <algorithm>
#include <stdexcept>
#include <vector>

using namespace pikdraw;
using namespace pikdraw::document;
using namespace pikdraw::rendering;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
}
void test_document_raster_effects_dpi() {
    auto page = Page::make_default();
    page.width_mm = page.height_mm = 80;
    page.color_mode_explicit = true;
    Document doc("raster-dpi", page);
    PathObject path;
    path.id = generate_object_id();
    path.path = geometry::Path2D::make_rect(39.13, 39.27, 1.8, 1.5);
    path.fill = color::RGBColor{1, 0, 0, 1};
    path.has_stroke = false;
    EffectNode blur;
    blur.id = generate_effect_instance_id();
    blur.parameters = GaussianBlurEffect{.22, .22};
    path.appearance.effects.push_back(blur);
    check(doc.execute_command(std::make_unique<AddObjectCommand>(path)).success, "DPI fixture rejected");
    const auto low = doc.current_snapshot();
    check(doc.execute_command(std::make_unique<SetDocumentRasterDpiCommand>(300)).success, "DPI edit failed");
    const auto high = doc.current_snapshot();
    DamageTracker damage;
    damage.update(*low, *high, 12);
    check(damage.has_damage() && damage.is_full_damage(), "DPI change did not invalidate effects frame");
    Camera2D camera({40, 40}, 12, 128, 128);
    std::vector<uint8_t> low_pixels(128 * 128 * 4), high_pixels(low_pixels.size());
    PixelBufferTarget low_target{low_pixels.data(), 128, 128, 128 * 4};
    PixelBufferTarget high_target{high_pixels.data(), 128, 128, 128 * 4};
    SoftwareRasterizer cpu;
    RenderMetric low_metric, high_metric;
    check(cpu.render(*low, camera, low_target, &low_metric), "CPU 72 DPI failed");
    check(cpu.render(*high, camera, high_target, &high_metric), "CPU 300 DPI failed");
    check(high_metric.peak_gaussian_blur_scratch_capacity_bytes > low_metric.peak_gaussian_blur_scratch_capacity_bytes,
          "CPU scratch buffers ignored document DPI");
    check(low_pixels != high_pixels, "CPU effects sampling ignored document DPI");
    const auto high_copy = high_pixels;
    check(doc.undo() && doc.raster_dpi() == 72, "DPI undo failed");
    check(cpu.render(*doc.current_snapshot(), camera, high_target), "CPU render after undo failed");
    check(high_pixels == low_pixels, "DPI undo did not restore rendered samples");
    D2DRenderBackend gpu;
    check(gpu.render(*low, camera, low_target, &low_metric), "D2D 72 DPI failed");
    check(gpu.render(*high, camera, high_target, &high_metric), "D2D 300 DPI failed");
    check(low_metric.used_gpu && high_metric.used_gpu, "DPI test unexpectedly used CPU fallback");
    check(low_pixels != high_pixels, "D2D effect source buffers ignored document DPI");
    check(high_pixels[0] == 255 && low_pixels[0] == 255, "DPI changed page/background geometry");
}
