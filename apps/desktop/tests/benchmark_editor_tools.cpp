#include "pikdraw/desktop/shell/native_window.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace pikdraw;
namespace {
class CorpusCommand final : public document::DocumentCommand {
  public:
    std::string_view name() const noexcept override {
        return "Editor benchmark corpus";
    }
    document::CommandResult execute(document::Document &doc) override {
        auto page = document::Page::make_default();
        page.width_mm = 1000;
        page.height_mm = 400;
        document::PersistentObjectMap objects;
        geometry::Path2D path;
        constexpr double tau = 6.283185307179586;
        for (int node = 0; node < 100; ++node) {
            const double angle = tau * node / 100;
            const geometry::Point2D point{7 * std::cos(angle), 7 * std::sin(angle)};
            if (node == 0)
                path.move_to(point);
            else
                path.line_to(point);
        }
        path.close();
        for (int index = 0; index < 1000; ++index) {
            document::PathObject object;
            object.id = document::generate_object_id();
            object.name = "Benchmark path";
            object.path = path;
            object.transform =
                geometry::Transform2D::translation(10 + (index % 50) * 20, 10 + (index / 50) * 20);
            object.fill = color::ColorValue::make_cmyk(.2, .5, .1, .1);
            object.has_stroke = false;
            page.layers.front().object_ids.push_back(object.id);
            objects = objects.set(object.id, object);
        }
        doc.apply_mutation(std::move(page), std::move(objects));
        return document::CommandResult::ok();
    }
    document::CommandResult undo(document::Document &) override {
        return document::CommandResult::fail("Setup only");
    }
};
template <class F> std::vector<double> measure(int count, F operation) {
    std::vector<double> samples;
    for (int i = 0; i < count; ++i) {
        const auto start = std::chrono::steady_clock::now();
        operation(i);
        samples.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
    std::sort(samples.begin(), samples.end());
    return samples;
}
void stats(const char *name, const std::vector<double> &values, bool comma = true) {
    std::cout << '"' << name << "\":{\"samples\":" << values.size()
              << ",\"p50_ms\":" << values[values.size() / 2]
              << ",\"p95_ms\":" << values[(values.size() * 95 - 1) / 100] << ",\"max_ms\":" << values.back()
              << '}' << (comma ? "," : "");
}
} // namespace
int main() {
    try {
        document::Document doc("editor-benchmark");
        if (!doc.execute_command(std::make_unique<CorpusCommand>()).success)
            throw std::runtime_error("Corpus setup failed");
        const auto snapshot = doc.current_snapshot();
        rendering::ViewportRenderer renderer;
        renderer.camera().set_center({500, 200});
        renderer.camera().set_zoom(.72);
        std::vector<uint8_t> pixels(800 * 600 * 4);
        rendering::PixelBufferTarget target{pixels.data(), 800, 600, 800 * 4};
        for (int warm = 0; warm < 3; ++warm)
            if (!renderer.render_frame(*snapshot, target))
                throw std::runtime_error("Warm render failed");
        const auto redraw = measure(30, [&](int) {
            renderer.camera().pan_pixels(1, 0);
            if (!renderer.render_frame(*snapshot, target))
                throw std::runtime_error("Render failed");
        });
        const auto pick = measure(60, [&](int sample) {
            const int index = (sample * 73) % 1000;
            const auto hit = vector::SelectionModel::hit_test(
                {10.0 + (index % 50) * 20, 10.0 + (index / 50) * 20}, *snapshot, .5);
            if (!hit)
                throw std::runtime_error("Benchmark pick missed its analytic center");
        });
        const auto region = [&](vector::RegionMode mode) {
            return measure(30, [&](int sample) {
                const auto ids =
                    vector::SelectionModel::query_region({100.0 + sample, 100, 700, 200}, mode, *snapshot);
                if (ids.empty())
                    throw std::runtime_error("Benchmark region unexpectedly empty");
            });
        };
        const auto window = region(vector::RegionMode::Window);
        const auto crossing = region(vector::RegionMode::Crossing);
        std::cout << "{\"objects\":1000,\"source_path_elements\":101000,\"raster_size\":[800,600],";
        stats("source_redraw", redraw);
        stats("point_pick", pick);
        stats("window_region", window);
        stats("crossing_region", crossing);
#ifdef _WIN32
        desktop::NativeWindow editor(1280, 800);
        if (!editor.initialize("PikDraw editor CPU paint benchmark"))
            throw std::runtime_error("Native window failed");
        ShowWindow(editor.hwnd(), SW_HIDE);
        if (!editor.root().document().reset_from_snapshot(snapshot))
            throw std::runtime_error("Native corpus failed");
        editor.fit_page_to_canvas();
        const auto id = snapshot->page().layers.front().object_ids.front();
        editor.root().vector_controller().set_selection({id}, *editor.root().document().current_snapshot());
        for (int warm = 0; warm < 3; ++warm)
            SendMessageW(editor.hwnd(), WM_PAINT, 0, 0);
        const auto paint = measure(60, [&](int) { SendMessageW(editor.hwnd(), WM_PAINT, 0, 0); });
        stats("cached_native_cpu_paint", paint);
#endif
        std::cout << "\"scope\":\"CPU wall time; hidden native paint; no present, input latency, GPU or "
                     "hardware qualification\"}\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
