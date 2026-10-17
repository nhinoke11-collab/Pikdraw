#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <Psapi.h>

#include "pikdraw/document/commands/path_commands.hpp"
#include "pikdraw/document/document.hpp"
#include "pikdraw/rendering/software_rasterizer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace pikdraw;
using namespace pikdraw::document;
using namespace pikdraw::rendering;

namespace {

struct Scene final {
  Scene(std::string id, int32_t width, int32_t height,
        geometry::Point2D center, double zoom)
      : document(std::move(id)),
        camera(center, zoom, width, height),
        target_pixels(static_cast<size_t>(width) *
                          static_cast<size_t>(height) * 4,
                      0),
        target{target_pixels.data(), width, height, width * 4} {}

  Document document;
  Camera2D camera;
  std::vector<uint8_t> target_pixels;
  PixelBufferTarget target;
};

struct ProcessMemory final {
  size_t private_bytes{};
  size_t working_set_bytes{};
  size_t peak_private_bytes{};
  size_t peak_working_set_bytes{};
};

[[nodiscard]] ProcessMemory process_memory() {
  PROCESS_MEMORY_COUNTERS_EX counters{};
  counters.cb = static_cast<DWORD>(sizeof(counters));
  if (!GetProcessMemoryInfo(GetCurrentProcess(),
                            reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&counters),
                            counters.cb))
    throw std::runtime_error("GetProcessMemoryInfo failed");
  return {static_cast<size_t>(counters.PrivateUsage),
          static_cast<size_t>(counters.WorkingSetSize),
          static_cast<size_t>(counters.PeakPagefileUsage),
          static_cast<size_t>(counters.PeakWorkingSetSize)};
}

[[nodiscard]] uint64_t file_time_ticks(const FILETIME &time) noexcept {
  return (static_cast<uint64_t>(time.dwHighDateTime) << 32) |
         static_cast<uint64_t>(time.dwLowDateTime);
}

[[nodiscard]] uint64_t process_cpu_time_100ns() {
  FILETIME created{};
  FILETIME exited{};
  FILETIME kernel{};
  FILETIME user{};
  if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
    throw std::runtime_error("GetProcessTimes failed");
  return file_time_ticks(kernel) + file_time_ticks(user);
}

void add_rectangle(Scene &scene, double x, double y, double width,
                   double height, const Appearance &appearance) {
  PathObject path;
  path.id = generate_object_id();
  path.name = "Performance probe path";
  path.path = geometry::Path2D::make_rect(x, y, width, height);
  path.fill = color::RGBColor{0.12, 0.48, 0.78, 1.0};
  path.has_stroke = false;
  path.appearance = appearance;
  for (auto &node : path.appearance.effects)
    node.id = generate_effect_instance_id();
  const auto result =
      scene.document.execute_command(std::make_unique<AddObjectCommand>(path));
  if (!result.success)
    throw std::runtime_error("Could not add benchmark object");
}

[[nodiscard]] EffectNode blur(double sigma_mm) {
  EffectNode node;
  node.id = generate_effect_instance_id();
  node.parameters = GaussianBlurEffect{sigma_mm, sigma_mm};
  return node;
}

[[nodiscard]] EffectNode shadow(double sigma_mm) {
  EffectNode node;
  node.id = generate_effect_instance_id();
  node.parameters = DropShadowEffect{
      8.0, -6.0, sigma_mm, sigma_mm,
      color::RGBColor{0.02, 0.03, 0.05, 0.9}, 0.65};
  return node;
}

[[nodiscard]] std::unique_ptr<Scene> make_scene(std::string_view name) {
  auto scene = std::make_unique<Scene>("effects-performance-probe", 640, 480,
                                       geometry::Point2D{128.0, 96.0}, 1.5);
  Appearance appearance;
  if (name == "blur_single") {
    appearance.effects.push_back(blur(1.5));
    add_rectangle(*scene, 16.0, 16.0, 224.0, 160.0, appearance);
  } else if (name == "shadow_single") {
    appearance.effects.push_back(shadow(1.5));
    add_rectangle(*scene, 16.0, 16.0, 224.0, 160.0, appearance);
  } else if (name == "blur_shadow_stack") {
    appearance.effects.push_back(blur(1.0));
    appearance.effects.push_back(shadow(1.5));
    add_rectangle(*scene, 16.0, 16.0, 224.0, 160.0, appearance);
  } else if (name == "many_blurred") {
    scene = std::make_unique<Scene>("effects-performance-many", 640, 480,
                                    geometry::Point2D{100.0, 100.0}, 2.0);
    appearance.effects.push_back(blur(0.8));
    for (int row = 0; row < 8; ++row) {
      for (int column = 0; column < 8; ++column) {
        add_rectangle(*scene, 4.0 + column * 24.0, 4.0 + row * 24.0,
                      18.0, 18.0, appearance);
      }
    }
  } else if (name == "source_only") {
    add_rectangle(*scene, 16.0, 16.0, 224.0, 160.0, appearance);
  } else {
    throw std::invalid_argument("Unknown scenario");
  }
  return scene;
}

[[nodiscard]] double percentile(std::vector<double> values, double q) {
  if (values.empty())
    throw std::invalid_argument("No timing samples");
  std::sort(values.begin(), values.end());
  const auto rank = static_cast<size_t>(
      std::ceil(q * static_cast<double>(values.size())));
  return values[std::clamp<size_t>(rank == 0 ? 0 : rank - 1, 0,
                                   values.size() - 1)];
}

[[nodiscard]] uint64_t checksum(const std::vector<uint8_t> &pixels) noexcept {
  uint64_t value = 1469598103934665603ull;
  for (size_t index = 0; index < pixels.size(); index += 64) {
    value ^= pixels[index];
    value *= 1099511628211ull;
  }
  return value;
}

void require_success(SoftwareRasterizer &renderer, Scene &scene,
                     size_t expected_objects,
                     size_t &peak_gaussian_scratch_capacity_bytes,
                     size_t &peak_drop_shadow_scratch_capacity_bytes) {
  std::fill(scene.target_pixels.begin(), scene.target_pixels.end(), 0);
  RenderMetric metric;
  if (!renderer.render(*scene.document.current_snapshot(), scene.camera,
                       scene.target, &metric))
    throw std::runtime_error("SoftwareRasterizer::render returned false");
  if (metric.rendered_objects != expected_objects)
    throw std::runtime_error("Unexpected rendered object count");
  const auto errors = std::count_if(
      metric.diagnostics.begin(), metric.diagnostics.end(),
      [](const RenderDiagnostic &item) { return item.error; });
  if (errors != 0)
    throw std::runtime_error("Renderer reported an error diagnostic");
  peak_gaussian_scratch_capacity_bytes = std::max(
      peak_gaussian_scratch_capacity_bytes,
      metric.peak_gaussian_blur_scratch_capacity_bytes);
  peak_drop_shadow_scratch_capacity_bytes = std::max(
      peak_drop_shadow_scratch_capacity_bytes,
      metric.peak_drop_shadow_scratch_capacity_bytes);
}

[[nodiscard]] int run(std::string_view scenario, int iterations,
                      std::string_view configuration) {
  if (iterations < 3 || iterations > 200)
    throw std::invalid_argument("Iterations must be between 3 and 200");
  auto scene = make_scene(scenario);
  const size_t object_count =
      scene->document.current_snapshot()->objects().size();
  SoftwareRasterizer renderer;
  size_t peak_gaussian_scratch_capacity_bytes = 0;
  size_t peak_drop_shadow_scratch_capacity_bytes = 0;

  const auto cold_start = std::chrono::steady_clock::now();
  require_success(renderer, *scene, object_count,
                  peak_gaussian_scratch_capacity_bytes,
                  peak_drop_shadow_scratch_capacity_bytes);
  const double cold_ms = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - cold_start)
                             .count();

  const auto memory_baseline = process_memory();
  for (int warmup = 0; warmup < 3; ++warmup)
    require_success(renderer, *scene, object_count,
                    peak_gaussian_scratch_capacity_bytes,
                    peak_drop_shadow_scratch_capacity_bytes);

  std::vector<double> samples;
  std::vector<double> cpu_samples;
  samples.reserve(static_cast<size_t>(iterations));
  cpu_samples.reserve(static_cast<size_t>(iterations));
  uint64_t frame_checksum = 0;
  for (int sample = 0; sample < iterations; ++sample) {
    const auto cpu_start = process_cpu_time_100ns();
    const auto start = std::chrono::steady_clock::now();
    require_success(renderer, *scene, object_count,
                    peak_gaussian_scratch_capacity_bytes,
                    peak_drop_shadow_scratch_capacity_bytes);
    const auto end = std::chrono::steady_clock::now();
    const auto cpu_end = process_cpu_time_100ns();
    samples.push_back(
        std::chrono::duration<double, std::milli>(end - start).count());
    cpu_samples.push_back(static_cast<double>(cpu_end - cpu_start) / 10000.0);
    frame_checksum =
        (frame_checksum * 1099511628211ull) ^ checksum(scene->target_pixels);
  }
  const auto memory_peak = process_memory();
  const auto private_delta =
      memory_peak.peak_private_bytes > memory_baseline.private_bytes
          ? memory_peak.peak_private_bytes - memory_baseline.private_bytes
          : 0;
  const auto working_set_delta =
      memory_peak.peak_working_set_bytes > memory_baseline.working_set_bytes
          ? memory_peak.peak_working_set_bytes -
                memory_baseline.working_set_bytes
          : 0;
  double total_wall_ms = 0.0;
  double total_process_cpu_ms = 0.0;
  for (size_t index = 0; index < samples.size(); ++index) {
    total_wall_ms += samples[index];
    total_process_cpu_ms += cpu_samples[index];
  }
  const double process_cpu_percent =
      total_wall_ms > 0.0 ? 100.0 * total_process_cpu_ms / total_wall_ms : 0.0;

  std::cout << std::fixed << std::setprecision(3)
            << "{\"scenario\":\"" << scenario << "\",\"configuration\":\""
            << configuration << "\",\"viewport\":\"640x480\","
            << "\"objects\":" << object_count << ",\"cold_ms\":" << cold_ms
            << ",\"warmup_iterations\":3,\"samples\":" << iterations
            << ",\"p50_ms\":" << percentile(samples, 0.50)
            << ",\"p95_ms\":" << percentile(samples, 0.95)
            << ",\"process_cpu_p50_ms\":" << percentile(cpu_samples, 0.50)
            << ",\"process_cpu_p95_ms\":" << percentile(cpu_samples, 0.95)
            << ",\"process_cpu_percent_one_core\":"
            << process_cpu_percent
            << ",\"peak_private_delta_bytes\":" << private_delta
            << ",\"peak_working_set_delta_bytes\":" << working_set_delta
            << ",\"peak_gaussian_blur_scratch_capacity_bytes\":"
            << peak_gaussian_scratch_capacity_bytes
            << ",\"peak_drop_shadow_scratch_capacity_bytes\":"
            << peak_drop_shadow_scratch_capacity_bytes
            << ",\"checksum\":" << frame_checksum << "}\n";
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  try {
    std::string scenario = "blur_single";
    std::string configuration = "unspecified";
    int iterations = 40;
    for (int index = 1; index < argc; ++index) {
      const std::string_view argument = argv[index];
      if (argument == "--scenario" && index + 1 < argc) {
        scenario = argv[++index];
      } else if (argument == "--configuration" && index + 1 < argc) {
        configuration = argv[++index];
      } else if (argument == "--iterations" && index + 1 < argc) {
        iterations = std::stoi(argv[++index]);
      } else {
        throw std::invalid_argument("Invalid benchmark arguments");
      }
    }
    return run(scenario, iterations, configuration);
  } catch (const std::exception &error) {
    std::cerr << "effects benchmark failed: " << error.what() << '\n';
    return 2;
  }
}
