#include "pikdraw/raster/edit_preview_service.hpp"

#include "pikdraw/raster/edit_geometry.hpp"
#include "pikdraw/raster/evaluator.hpp"
#include "pikdraw/raster/profile_conversion.hpp"

#include <algorithm>
#include <bit>
#include <condition_variable>
#include <deque>
#include <iterator>
#include <limits>
#include <mutex>
#include <new>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace pikdraw::raster {
namespace {

constexpr size_t kMaximumQueuedPreviews = 32;

uint64_t hash_graph(const document::RasterEditGraph& graph) noexcept {
  uint64_t hash = 14695981039346656037ull;
  const auto mix = [&hash](uint64_t value) {
    for (unsigned byte = 0; byte < 8; ++byte) {
      hash ^= static_cast<uint8_t>(value >> (byte * 8));
      hash *= 1099511628211ull;
    }
  };
  const auto mix_bytes = [&hash](const std::vector<uint8_t>& bytes) {
    for (const auto byte : bytes) {
      hash ^= byte;
      hash *= 1099511628211ull;
    }
  };
  mix(graph.schema_version);
  for (const auto& node : graph.nodes) {
    mix(node.id.value);
    mix(node.enabled ? 1u : 0u);
    mix(node.algorithm_version);
    mix(node.parameters.index());
    std::visit([&](const auto& parameters) {
      using T = std::decay_t<decltype(parameters)>;
      if constexpr (std::is_same_v<T, document::RasterCropParams>) {
        mix(std::bit_cast<uint64_t>(parameters.x));
        mix(std::bit_cast<uint64_t>(parameters.y));
        mix(std::bit_cast<uint64_t>(parameters.width));
        mix(std::bit_cast<uint64_t>(parameters.height));
      } else if constexpr (std::is_same_v<T, document::RasterFlipParams>) {
        mix(parameters.horizontal ? 1u : 0u);
        mix(parameters.vertical ? 1u : 0u);
      } else if constexpr (std::is_same_v<T, document::RasterRotateParams>) {
        mix(std::bit_cast<uint64_t>(parameters.degrees));
      } else if constexpr (std::is_same_v<T, document::RasterExposureParams>) {
        mix(std::bit_cast<uint64_t>(parameters.exposure_ev));
      } else if constexpr (std::is_same_v<T, document::RasterLevelsParams>) {
        mix(static_cast<uint8_t>(parameters.channel));
        mix(std::bit_cast<uint64_t>(parameters.black_in));
        mix(std::bit_cast<uint64_t>(parameters.white_in));
        mix(std::bit_cast<uint64_t>(parameters.gamma));
        mix(std::bit_cast<uint64_t>(parameters.black_out));
        mix(std::bit_cast<uint64_t>(parameters.white_out));
      } else if constexpr (std::is_same_v<T, document::RasterHslParams>) {
        mix(std::bit_cast<uint64_t>(parameters.hue_degrees));
        mix(std::bit_cast<uint64_t>(parameters.saturation_percent));
        mix(std::bit_cast<uint64_t>(parameters.lightness_percent));
      } else if constexpr (std::is_same_v<T, document::RasterCurvesParams>) {
        mix(static_cast<uint8_t>(parameters.channel));
        for (const auto point : parameters.output_points)
          mix(std::bit_cast<uint64_t>(point));
      } else if constexpr (std::is_same_v<T, document::RasterWhiteBalanceParams>) {
        mix(std::bit_cast<uint64_t>(parameters.temperature_shift_percent));
        mix(std::bit_cast<uint64_t>(parameters.tint_percent));
      } else if constexpr (std::is_same_v<T, document::RasterVibranceParams>) {
        mix(std::bit_cast<uint64_t>(parameters.amount_percent));
      } else if constexpr (std::is_same_v<T, document::RasterSelectiveHslParams>) {
        mix(static_cast<uint8_t>(parameters.range));
        mix(std::bit_cast<uint64_t>(parameters.hue_degrees));
        mix(std::bit_cast<uint64_t>(parameters.saturation_percent));
        mix(std::bit_cast<uint64_t>(parameters.lightness_percent));
        mix(std::bit_cast<uint64_t>(parameters.feather_percent));
      } else if constexpr (std::is_same_v<T, document::RasterLightParams>) {
        mix(std::bit_cast<uint64_t>(parameters.exposure_ev));
        mix(std::bit_cast<uint64_t>(parameters.contrast_percent));
        mix(std::bit_cast<uint64_t>(parameters.highlights_percent));
        mix(std::bit_cast<uint64_t>(parameters.shadows_percent));
        mix(std::bit_cast<uint64_t>(parameters.whites_percent));
        mix(std::bit_cast<uint64_t>(parameters.blacks_percent));
      } else if constexpr (std::is_same_v<T, document::RasterColorBalanceParams>) {
        mix(static_cast<uint8_t>(parameters.range));
        mix(std::bit_cast<uint64_t>(parameters.red_cyan_percent));
        mix(std::bit_cast<uint64_t>(parameters.green_magenta_percent));
        mix(std::bit_cast<uint64_t>(parameters.blue_yellow_percent));
        mix(parameters.preserve_luminosity ? 1u : 0u);
      } else if constexpr (std::is_same_v<T, document::RasterChannelMixerParams>) {
        for (const auto coefficient : parameters.coefficients)
          mix(std::bit_cast<uint64_t>(coefficient));
      } else if constexpr (std::is_same_v<T, document::RasterCloneStampParams>) {
        mix(std::bit_cast<uint64_t>(parameters.source_x));
        mix(std::bit_cast<uint64_t>(parameters.source_y));
        mix(std::bit_cast<uint64_t>(parameters.target_x));
        mix(std::bit_cast<uint64_t>(parameters.target_y));
        mix(std::bit_cast<uint64_t>(parameters.radius_percent));
        mix(std::bit_cast<uint64_t>(parameters.hardness_percent));
        mix(std::bit_cast<uint64_t>(parameters.opacity_percent));
      } else if constexpr (std::is_same_v<T, document::RasterEraserParams>) {
        mix(std::bit_cast<uint64_t>(parameters.diameter_px));
        mix(parameters.points.size());
        for (const auto& point : parameters.points) {
          mix(std::bit_cast<uint64_t>(point.x));
          mix(std::bit_cast<uint64_t>(point.y));
        }
      }
    }, node.parameters);
    mix(node.mask.has_value() ? 1u : 0u);
    if (node.mask) {
      mix(node.mask->id);
      mix(node.mask->width_px);
      mix(node.mask->height_px);
      mix(node.mask->density_percent);
      mix(node.mask->inverted ? 1u : 0u);
      mix(static_cast<uint8_t>(node.mask->kind));
      mix_bytes(node.mask->coverage);
      mix(std::bit_cast<uint64_t>(node.mask->left));
      mix(std::bit_cast<uint64_t>(node.mask->top));
      mix(std::bit_cast<uint64_t>(node.mask->width));
      mix(std::bit_cast<uint64_t>(node.mask->height));
      mix(std::bit_cast<uint64_t>(node.mask->feather_px));
      mix(std::bit_cast<uint64_t>(node.mask->gradient_angle_degrees));
    }
  }
  return hash;
}

bool has_enabled_nodes(const document::RasterEditGraph& graph) noexcept {
  return std::any_of(graph.nodes.begin(), graph.nodes.end(),
      [](const document::RasterEditNode& node) { return node.enabled; });
}

bool needs_preview(const document::ImageObject& image) noexcept {
  return has_enabled_nodes(image.edit_graph) ||
      (image.source.preview_pixel_color_basis ==
           document::RasterPixelColorBasis::EmbeddedProfileUnapplied &&
       !image.source.icc_profile.empty());
}

uint16_t read_u16le(std::span<const uint8_t> bytes, size_t offset) noexcept {
  return static_cast<uint16_t>(bytes[offset]) |
      static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8);
}

uint8_t to_u8(uint16_t value) noexcept {
  return static_cast<uint8_t>((static_cast<uint32_t>(value) * 255u + 32767u) /
                              65535u);
}

}  // namespace

struct RasterEditPreviewService::Impl {
  struct Key {
    std::string document_id;
    uint64_t epoch{0};
    document::ObjectId image_id{document::kInvalidObjectId};
    uintptr_t pixel_storage{0};
    uint8_t pixel_encoding{0};
    uintptr_t profile_storage{0};
    uint64_t profile_size{0};
    uint8_t color_basis{0};
    uint32_t width{0};
    uint32_t height{0};
    uint64_t graph_hash{0};
    friend bool operator==(const Key&, const Key&) = default;
  };

  struct KeyHash {
    size_t operator()(const Key& key) const noexcept {
      size_t hash = std::hash<std::string>{}(key.document_id);
      const auto combine = [&hash](uint64_t value) {
        hash ^= static_cast<size_t>(value) + 0x9e3779b97f4a7c15ull +
            (hash << 6) + (hash >> 2);
      };
      combine(key.epoch);
      combine(key.image_id);
      combine(key.pixel_storage);
      combine(key.pixel_encoding);
      combine(key.profile_storage);
      combine(key.profile_size);
      combine(key.color_basis);
      combine(key.width);
      combine(key.height);
      combine(key.graph_hash);
      return hash;
    }
  };

  struct ObjectKey {
    std::string document_id;
    uint64_t epoch{0};
    document::ObjectId image_id{document::kInvalidObjectId};
    friend bool operator==(const ObjectKey&, const ObjectKey&) = default;
  };

  struct ObjectKeyHash {
    size_t operator()(const ObjectKey& key) const noexcept {
      size_t hash = std::hash<std::string>{}(key.document_id);
      hash ^= static_cast<size_t>(key.epoch) + 0x9e3779b97f4a7c15ull +
          (hash << 6) + (hash >> 2);
      hash ^= static_cast<size_t>(key.image_id) + 0x9e3779b97f4a7c15ull +
          (hash << 6) + (hash >> 2);
      return hash;
    }
  };

  struct CachedFrame {
    document::ImageObject image;
    std::shared_ptr<RasterMemoryBudget::Reservation> reservation;
    uint64_t bytes{0};
  };

  struct CacheEntry {
    std::shared_ptr<const CachedFrame> frame;
    uint64_t last_use{0};
  };

  struct Job {
    Key key;
    ObjectKey object_key;
    std::shared_ptr<const document::DocumentSnapshot> snapshot;
    Completion completion;
  };

  Impl(uint64_t working_bytes, uint64_t cache_bytes,
       std::shared_ptr<const color::ProfileResource> output_profile,
       std::shared_ptr<document::resources::MemoryBudgetPolicy> policy)
      : memory_policy(policy ? std::move(policy) :
            document::resources::process_memory_budget_policy()),
        memory_budget(working_bytes, {}, working_bytes == 0 ? memory_policy : nullptr),
        cache_capacity(cache_bytes),
        preview_rgb_profile(std::move(output_profile)) {}

  ~Impl() {
    {
      std::lock_guard lock(mutex);
      stopping = true;
      queue.clear();
    }
    worker.request_stop();
    condition.notify_all();
  }

  static Key make_key(const document::DocumentSnapshot& snapshot,
                      const document::ImageObject& image) {
    const auto pixels = image.rgba_pixels();
    const auto high_precision = image.rgba16le_pixels();
    const bool is_high_precision = !high_precision.empty();
    const auto* storage = is_high_precision
        ? (image.shared_pixels_rgba16le
            ? static_cast<const void*>(image.shared_pixels_rgba16le.get())
            : static_cast<const void*>(high_precision.data()))
        : (image.shared_pixels_rgba
            ? static_cast<const void*>(image.shared_pixels_rgba.get())
            : static_cast<const void*>(pixels.data()));
    return {snapshot.document_id(), snapshot.epoch(), image.id,
            reinterpret_cast<uintptr_t>(storage),
            static_cast<uint8_t>(is_high_precision ? 2 : 1),
            reinterpret_cast<uintptr_t>(image.source.icc_profile.data()),
            image.source.icc_profile.size(),
            static_cast<uint8_t>(image.source.preview_pixel_color_basis),
            image.width_px > 0 ? static_cast<uint32_t>(image.width_px) : 0,
            image.height_px > 0 ? static_cast<uint32_t>(image.height_px) : 0,
            hash_graph(image.edit_graph)};
  }

  static ObjectKey make_object_key(const Key& key) {
    return {key.document_id, key.epoch, key.image_id};
  }

  bool is_current(const Key& key) const {
    std::lock_guard lock(mutex);
    const auto found = latest.find(make_object_key(key));
    return !stopping && found != latest.end() && found->second == key;
  }

  void evict_oldest_locked() {
    auto oldest = cache.begin();
    for (auto it = std::next(cache.begin()); it != cache.end(); ++it)
      if (it->second.last_use < oldest->second.last_use) oldest = it;
    cache_bytes -= oldest->second.frame->bytes;
    cache.erase(oldest);
  }

  uint64_t current_cache_capacity() const noexcept {
    return cache_capacity ? cache_capacity : memory_policy->cache_limit_bytes();
  }

  void trim_cache_locked() {
    const auto capacity = current_cache_capacity();
    while (cache_bytes > capacity && !cache.empty()) evict_oldest_locked();
  }

  void evict_until_available_locked(uint64_t requested) {
    trim_cache_locked();
    while (memory_budget.available_bytes() < requested && !cache.empty()) {
      evict_oldest_locked();
    }
  }

  void set_diagnostic_if_current(const Key& key, std::string message) {
    std::lock_guard lock(mutex);
    const auto found = latest.find(make_object_key(key));
    if (!stopping && found != latest.end() && found->second == key)
      diagnostic = std::move(message);
  }

  void finish_job(const Job& job, std::shared_ptr<CachedFrame> result,
                  std::string error) {
    bool notify_completion = false;
    {
      std::lock_guard lock(mutex);
      pending.erase(job.key);
      const auto current = latest.find(job.object_key);
      if (!stopping && current != latest.end() && current->second == job.key) {
        trim_cache_locked();
        const auto capacity = current_cache_capacity();
        if (result && result->bytes <= capacity) {
          while (!cache.empty() &&
                 (cache.size() >= 1024 || cache_bytes > capacity - result->bytes)) {
            evict_oldest_locked();
          }
          const auto bytes = result->bytes;
          cache_bytes += bytes;
          cache.insert_or_assign(job.key,
              CacheEntry{std::move(result), ++clock});
          diagnostic.reset();
          notify_completion = true;
        } else if (result || !error.empty()) {
          if (result) error = "Raster preview exceeds the current available cache memory.";
          diagnostic = std::move(error);
          notify_completion = true;
        }
      }
    }
    if (notify_completion && job.completion) job.completion();
  }

  void evaluate(Job job, std::stop_token stop) {
    const auto* variant = job.snapshot->objects().lookup_ref(job.key.image_id);
    const auto* image = variant ? std::get_if<document::ImageObject>(variant) : nullptr;
    if (!image || make_key(*job.snapshot, *image) != job.key) {
      finish_job(job, {}, {});
      return;
    }
    if (image->width_px <= 0 || image->height_px <= 0) {
      finish_job(job, {}, "Raster preview source dimensions are invalid.");
      return;
    }
    const auto pixels = image->rgba_pixels();
    auto high_precision_pixels = image->rgba16le_pixels();
    bool is_high_precision = !high_precision_pixels.empty();
    const uint64_t pixel_count = static_cast<uint64_t>(image->width_px) *
                                 static_cast<uint64_t>(image->height_px);
    const uint64_t bytes_per_pixel = is_high_precision ? 8u : 4u;
    if (pixel_count != pixels.size() ||
        pixel_count > SIZE_MAX / bytes_per_pixel ||
        (is_high_precision && pixel_count * bytes_per_pixel !=
            high_precision_pixels.size())) {
      finish_job(job, {}, "Raster preview source pixels do not match its dimensions.");
      return;
    }
    if (raster_edit_requires_srgb_processing(image->edit_graph) &&
        image->source.preview_pixel_color_basis ==
            document::RasterPixelColorBasis::LegacyUnknown) {
      finish_job(job, {},
          "Resolve this image's source profile before applying color adjustments.");
      return;
    }

    const auto source_bytes = static_cast<size_t>(pixel_count * bytes_per_pixel);
    if (source_bytes > std::numeric_limits<uint64_t>::max() / 4u) {
      finish_job(job, {}, "Raster preview memory estimate overflowed.");
      return;
    }
    const auto peak_estimate = static_cast<uint64_t>(source_bytes) * 4u;
    {
      std::lock_guard lock(mutex);
      evict_until_available_locked(peak_estimate);
    }
    try {
      document::ImageObject processed_image;
      const document::ImageObject* evaluation_image = image;
      std::shared_ptr<RasterMemoryBudget::Reservation> profile_reservation;
      if (image->source.preview_pixel_color_basis ==
          document::RasterPixelColorBasis::EmbeddedProfileUnapplied) {
        if (!preview_rgb_profile) {
          finish_job(job, {},
              "Raster profile preview is unavailable because the output RGB profile could not be loaded.");
          return;
        }
        const uint64_t converted_bytes_per_pixel = is_high_precision ? 12u : 4u;
        if (pixel_count > std::numeric_limits<uint64_t>::max() /
                converted_bytes_per_pixel) {
          finish_job(job, {}, "Raster profile preview memory estimate overflowed.");
          return;
        }
        const uint64_t converted_bytes = pixel_count * converted_bytes_per_pixel;
        profile_reservation = memory_budget.try_reserve(converted_bytes);
        if (!profile_reservation) {
          finish_job(job, {},
              "Raster profile preview exceeds the available image-edit memory budget.");
          return;
        }
        RasterProfileConversionOptions conversion_options;
        conversion_options.transform = {
            color::IccRenderingIntent::RelativeColorimetric, false};
        conversion_options.cancelled = [this, &job, stop] {
          return stop.stop_requested() || !is_current(job.key);
        };
        auto converted = convert_image_profile_to_rgb(
            *image, preview_rgb_profile, conversion_options);
        if (!converted.succeeded()) {
          finish_job(job, {}, converted.status == RasterProfileConversionStatus::Cancelled
              ? std::string{} : std::move(converted.diagnostic));
          return;
        }
        processed_image = *image;
        processed_image.pixels_rgba.clear();
        processed_image.shared_pixels_rgba = std::move(converted.rgba8_preview);
        processed_image.shared_pixels_rgba16le =
            std::move(converted.rgba16le_master);
        processed_image.source.preview_pixel_color_basis =
            document::RasterPixelColorBasis::ConvertedToSRgb;
        evaluation_image = &processed_image;
        high_precision_pixels = evaluation_image->rgba16le_pixels();
        is_high_precision = !high_precision_pixels.empty();
      }
      RasterEvaluationOptions options;
      options.cancelled = [this, &job, stop] {
        return stop.stop_requested() || !is_current(job.key);
      };
      RasterEvaluationResult evaluated;
      if (is_high_precision) {
        const RasterPixelView view{
            high_precision_pixels, static_cast<uint32_t>(evaluation_image->width_px),
            static_cast<uint32_t>(evaluation_image->height_px),
            static_cast<uint64_t>(evaluation_image->width_px) * 8u,
            RasterPixelEncoding::SRgbRgba16LEStraightAlpha};
        evaluated = RasterEditEvaluator::evaluate(
            view, evaluation_image->edit_graph, memory_budget, options);
      } else {
        evaluated = RasterEditEvaluator::evaluate_packed_rgba8(
            evaluation_image->rgba_pixels(), static_cast<uint32_t>(evaluation_image->width_px),
            static_cast<uint32_t>(evaluation_image->height_px), evaluation_image->edit_graph,
            memory_budget, options);
      }
      if (!evaluated.succeeded()) {
        finish_job(job, {}, evaluated.status == RasterEvaluationStatus::Cancelled
            ? std::string{}
            : (evaluated.diagnostic.empty()
                ? "Raster preview evaluation failed."
                : std::move(evaluated.diagnostic)));
        return;
      }
      if (evaluated.image.width > static_cast<uint32_t>(
              std::numeric_limits<int32_t>::max()) ||
          evaluated.image.height > static_cast<uint32_t>(
              std::numeric_limits<int32_t>::max())) {
        finish_job(job, {}, "Raster preview dimensions exceed the renderer range.");
        return;
      }
      const uint64_t evaluated_pixels =
          static_cast<uint64_t>(evaluated.image.width) * evaluated.image.height;
      if (evaluated_pixels > std::numeric_limits<uint64_t>::max() / 4u ||
          evaluated_pixels > SIZE_MAX / sizeof(uint32_t)) {
        finish_job(job, {}, "Raster preview output size overflowed.");
        return;
      }
      const uint64_t packed_bytes = evaluated_pixels * 4u;
      {
        std::lock_guard lock(mutex);
        evict_until_available_locked(packed_bytes);
      }
      auto packed_reservation = memory_budget.try_reserve(packed_bytes);
      if (!packed_reservation) {
        finish_job(job, {},
            "Raster preview result exceeds the available image-edit memory budget.");
        return;
      }

      // Renderers may retain the pixels after cache eviction. Keep the memory
      // reservation alive until the last pixel owner releases them.
      auto packed_pixels = std::shared_ptr<std::vector<uint32_t>>(
          new std::vector<uint32_t>(
              static_cast<size_t>(evaluated.image.width) * evaluated.image.height),
          [reservation = packed_reservation](std::vector<uint32_t>* value) {
            (void)reservation;
            delete value;
          });
      for (size_t index = 0; index < packed_pixels->size(); ++index) {
        const size_t offset = index *
            (evaluated.image.encoding ==
                RasterPixelEncoding::SRgbRgba16LEStraightAlpha ? 8u : 4u);
        const auto channel = [&](size_t offset_in_pixel) {
          if (evaluated.image.encoding ==
              RasterPixelEncoding::SRgbRgba16LEStraightAlpha)
            return to_u8(read_u16le(evaluated.image.bytes,
                                    offset + offset_in_pixel * 2));
          return evaluated.image.bytes[offset + offset_in_pixel];
        };
        (*packed_pixels)[index] =
            (static_cast<uint32_t>(channel(0)) << 24) |
            (static_cast<uint32_t>(channel(1)) << 16) |
            (static_cast<uint32_t>(channel(2)) << 8) |
            static_cast<uint32_t>(channel(3));
      }

      auto frame = std::make_shared<CachedFrame>();
      auto& render_image = frame->image;
      render_image.id = image->id;
      render_image.name = image->name;
      render_image.bounds_rect = raster_edited_bounds(
          *image, evaluated.image.width, evaluated.image.height);
      render_image.transform = image->transform;
      render_image.width_px = static_cast<int32_t>(evaluated.image.width);
      render_image.height_px = static_cast<int32_t>(evaluated.image.height);
      render_image.shared_pixels_rgba = std::move(packed_pixels);
      render_image.source.preview_pixel_color_basis =
          evaluation_image->source.preview_pixel_color_basis;
      render_image.source.icc_profile = image->source.icc_profile;
      render_image.edit_graph = evaluation_image->edit_graph;
      render_image.clip_path = image->clip_path;
      render_image.visible = image->visible;
      render_image.locked = image->locked;
      frame->reservation = std::move(packed_reservation);
      frame->bytes = packed_bytes;
      finish_job(job, std::move(frame), {});
    } catch (const std::bad_alloc&) {
      finish_job(job, {}, "Memory allocation failed while building raster preview.");
    } catch (...) {
      finish_job(job, {}, "Raster preview failed because of an unexpected error.");
    }
  }

  void run(std::stop_token stop) {
    while (!stop.stop_requested()) {
      Job job;
      {
        std::unique_lock lock(mutex);
        condition.wait(lock, stop, [&] { return stopping || !queue.empty(); });
        if (stopping || stop.stop_requested()) return;
        job = std::move(queue.front());
        queue.pop_front();
      }
      evaluate(std::move(job), stop);
    }
  }

  mutable std::mutex mutex;
  std::condition_variable_any condition;
  const std::shared_ptr<document::resources::MemoryBudgetPolicy> memory_policy;
  RasterMemoryBudget memory_budget;
  const uint64_t cache_capacity;
  const std::shared_ptr<const color::ProfileResource> preview_rgb_profile;
  uint64_t cache_bytes{0};
  uint64_t clock{0};
  bool stopping{false};
  std::deque<Job> queue;
  std::unordered_set<Key, KeyHash> pending;
  std::unordered_map<ObjectKey, Key, ObjectKeyHash> latest;
  std::unordered_map<Key, CacheEntry, KeyHash> cache;
  std::optional<std::string> diagnostic;
  std::jthread worker;
};

RasterEditPreviewService::RasterEditPreviewService(
    uint64_t working_memory_bytes, uint64_t cache_memory_bytes,
    std::shared_ptr<const color::ProfileResource> preview_rgb_profile,
    std::shared_ptr<document::resources::MemoryBudgetPolicy> memory_policy)
    : impl_(std::make_unique<Impl>(working_memory_bytes, cache_memory_bytes,
                                  std::move(preview_rgb_profile),
                                  std::move(memory_policy))) {}

RasterEditPreviewService::~RasterEditPreviewService() = default;

void RasterEditPreviewService::request(
    const std::shared_ptr<const document::DocumentSnapshot>& snapshot,
    Completion completion) {
  if (!snapshot) return;
  std::vector<Impl::Job> jobs;
  const auto objects = snapshot->objects().all();
  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->stopping) return;
    impl_->trim_cache_locked();
    std::unordered_set<Impl::ObjectKey, Impl::ObjectKeyHash> active;
    for (const auto& [id, object] : objects) {
      (void)id;
      const auto* image = std::get_if<document::ImageObject>(&object);
      if (!image || !needs_preview(*image))
        continue;
      auto key = Impl::make_key(*snapshot, *image);
      auto object_key = Impl::make_object_key(key);
      active.insert(object_key);
      impl_->latest.insert_or_assign(object_key, key);
      if (auto cached = impl_->cache.find(key); cached != impl_->cache.end()) {
        cached->second.last_use = ++impl_->clock;
        continue;
      }
      if (impl_->pending.contains(key) ||
          impl_->queue.size() + jobs.size() >= kMaximumQueuedPreviews)
        continue;
      impl_->pending.insert(key);
      jobs.push_back({std::move(key), std::move(object_key), snapshot,
                      completion});
    }
    for (auto it = impl_->latest.begin(); it != impl_->latest.end();) {
      if (!active.contains(it->first)) it = impl_->latest.erase(it);
      else ++it;
    }
    for (auto& job : jobs) impl_->queue.push_back(std::move(job));
    if (!jobs.empty() && !impl_->worker.joinable()) {
      impl_->worker = std::jthread(
          [impl = impl_.get()](std::stop_token token) { impl->run(token); });
    }
  }
  if (!jobs.empty()) impl_->condition.notify_one();
}

std::shared_ptr<const document::ImageObject> RasterEditPreviewService::find(
    const document::DocumentSnapshot& snapshot,
    const document::ImageObject& image) const {
  if (!needs_preview(image))
    return nullptr;
  const auto key = Impl::make_key(snapshot, image);
  std::lock_guard lock(impl_->mutex);
  impl_->trim_cache_locked();
  auto found = impl_->cache.find(key);
  if (found == impl_->cache.end()) return nullptr;
  found->second.last_use = ++impl_->clock;
  const auto frame = found->second.frame;
  auto current = std::make_shared<document::ImageObject>(frame->image);
  current->transform = image.transform;
  current->bounds_rect = raster_edited_bounds(
      image, static_cast<uint32_t>(frame->image.width_px),
      static_cast<uint32_t>(frame->image.height_px));
  current->clip_path = image.clip_path;
  current->visible = image.visible;
  current->locked = image.locked;
  current->edit_graph = image.edit_graph;
  return current;
}

std::shared_ptr<const document::ImageObject>
RasterEditPreviewService::find_last_good(
    const document::DocumentSnapshot& snapshot,
    const document::ImageObject& image) const {
  if (!needs_preview(image)) return nullptr;
  const auto requested = Impl::make_key(snapshot, image);
  std::lock_guard lock(impl_->mutex);
  impl_->trim_cache_locked();
  Impl::CacheEntry* best = nullptr;
  for (auto& [key, entry] : impl_->cache) {
    const bool same_source =
        key.document_id == requested.document_id &&
        key.epoch == requested.epoch && key.image_id == requested.image_id &&
        key.pixel_storage == requested.pixel_storage &&
        key.pixel_encoding == requested.pixel_encoding &&
        key.profile_storage == requested.profile_storage &&
        key.profile_size == requested.profile_size &&
        key.color_basis == requested.color_basis &&
        key.width == requested.width && key.height == requested.height;
    if (!same_source || key.graph_hash == requested.graph_hash) continue;
    if (!best || entry.last_use > best->last_use) best = &entry;
  }
  if (!best) return nullptr;
  best->last_use = ++impl_->clock;
  const auto& cached = best->frame->image;
  auto current = std::make_shared<document::ImageObject>(cached);
  current->transform = image.transform;
  current->bounds_rect = raster_edited_bounds(
      image, static_cast<uint32_t>(cached.width_px),
      static_cast<uint32_t>(cached.height_px));
  current->clip_path = image.clip_path;
  current->visible = image.visible;
  current->locked = image.locked;
  current->edit_graph = image.edit_graph;
  return current;
}

std::optional<std::string> RasterEditPreviewService::take_diagnostic() {
  std::lock_guard lock(impl_->mutex);
  auto result = std::move(impl_->diagnostic);
  impl_->diagnostic.reset();
  return result;
}

}  // namespace pikdraw::raster
