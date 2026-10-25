#include "pikdraw/raster/analysis/histogram_service.hpp"
#include "pikdraw/raster/edit_geometry.hpp"

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace pikdraw::raster {
namespace {

constexpr uint64_t kEditedHistogramWorkingBytes = 256ULL * 1024ULL * 1024ULL;

struct HistogramKey {
  std::string document_id;
  uint64_t epoch{0};
  uint64_t revision{0};
  document::ObjectId image_id{document::kInvalidObjectId};
  uintptr_t storage{0};
  RasterPixelEncoding encoding{RasterPixelEncoding::SRgbRgba8StraightAlpha};
  uint32_t width{0};
  uint32_t height{0};
  RasterHistogramMode mode{RasterHistogramMode::Source};
  friend bool operator==(const HistogramKey&, const HistogramKey&) = default;
};

std::optional<HistogramKey> make_key(
    const document::DocumentSnapshot& snapshot,
    document::ObjectId image_id, RasterHistogramMode mode) {
  const auto* object = snapshot.objects().lookup_ref(image_id);
  if (!object) return std::nullopt;
  const auto* image = std::get_if<document::ImageObject>(object);
  if (!image || image->width_px <= 0 || image->height_px <= 0)
    return std::nullopt;

  const auto width = static_cast<uint32_t>(image->width_px);
  const auto height = static_cast<uint32_t>(image->height_px);
  const uint64_t pixel_count = static_cast<uint64_t>(width) * height;
  const auto pixels16 = image->rgba16le_pixels();
  if (pixel_count <= std::numeric_limits<uint64_t>::max() / 8u &&
      pixels16.size() == pixel_count * 8u) {
    return HistogramKey{snapshot.document_id(), snapshot.epoch(),
        snapshot.revision(), image_id,
        reinterpret_cast<uintptr_t>(pixels16.data()),
        RasterPixelEncoding::SRgbRgba16LEStraightAlpha, width, height, mode};
  }

  const auto pixels8 = image->rgba_pixels();
  if (pixels8.size() != pixel_count) return std::nullopt;
  return HistogramKey{snapshot.document_id(), snapshot.epoch(),
      snapshot.revision(), image_id,
      reinterpret_cast<uintptr_t>(pixels8.data()),
      RasterPixelEncoding::SRgbRgba8StraightAlpha, width, height, mode};
}

}  // namespace

struct RasterHistogramService::Impl {
  struct Job {
    document::SnapshotPtr snapshot;
    HistogramKey key;
    std::vector<Completion> completions;
  };

  Impl() = default;

  ~Impl() {
    worker.request_stop();
    condition.notify_all();
  }

  RasterHistogramResult evaluate(const Job& job, std::stop_token token) {
    const auto* object = job.snapshot->objects().lookup_ref(job.key.image_id);
    const auto* image = object
        ? std::get_if<document::ImageObject>(object) : nullptr;
    if (!image) {
      RasterHistogramResult result;
      result.status = RasterHistogramStatus::InvalidSource;
      result.diagnostic = "The selected image is no longer present in its snapshot.";
      return result;
    }
    RasterHistogramOptions options;
    options.cancelled = [this, token, key = job.key] {
      if (token.stop_requested()) return true;
      std::lock_guard lock(mutex);
      return !desired || *desired != key;
    };

    const auto compute_source = [&] {
      if (job.key.encoding == RasterPixelEncoding::SRgbRgba16LEStraightAlpha) {
        const auto bytes = image->rgba16le_pixels();
        return compute_raster_histogram(
            {bytes, job.key.width, job.key.height,
             static_cast<uint64_t>(job.key.width) * 8u,
             RasterPixelEncoding::SRgbRgba16LEStraightAlpha}, options);
      }
      return compute_raster_histogram(
          image->rgba_pixels(), job.key.width, job.key.height, options);
    };
    const bool has_enabled_node = std::any_of(
        image->edit_graph.nodes.begin(), image->edit_graph.nodes.end(),
        [](const auto& node) { return node.enabled; });
    if (job.key.mode == RasterHistogramMode::Source || !has_enabled_node)
      return compute_source();

    if (raster_edit_requires_srgb_processing(image->edit_graph) &&
        (image->source.preview_pixel_color_basis ==
             document::RasterPixelColorBasis::EmbeddedProfileUnapplied ||
         image->source.preview_pixel_color_basis ==
             document::RasterPixelColorBasis::LegacyUnknown)) {
      RasterHistogramResult result;
      result.status = RasterHistogramStatus::InvalidSource;
      result.diagnostic =
          "Resolve the source profile before analyzing edited color values.";
      return result;
    }

    RasterEvaluationOptions evaluation_options;
    evaluation_options.cancelled = options.cancelled;
    RasterMemoryBudget working_memory(kEditedHistogramWorkingBytes);
    RasterEvaluationResult evaluated;
    if (job.key.encoding == RasterPixelEncoding::SRgbRgba16LEStraightAlpha) {
      const auto bytes = image->rgba16le_pixels();
      evaluated = RasterEditEvaluator::evaluate(
          {bytes, job.key.width, job.key.height,
           static_cast<uint64_t>(job.key.width) * 8u,
           RasterPixelEncoding::SRgbRgba16LEStraightAlpha},
          image->edit_graph, working_memory, evaluation_options);
    } else {
      evaluated = RasterEditEvaluator::evaluate_packed_rgba8(
          image->rgba_pixels(), job.key.width, job.key.height,
          image->edit_graph, working_memory, evaluation_options);
    }
    if (!evaluated.succeeded()) {
      RasterHistogramResult result;
      switch (evaluated.status) {
        case RasterEvaluationStatus::Cancelled:
          result.status = RasterHistogramStatus::Cancelled;
          break;
        case RasterEvaluationStatus::InsufficientMemory:
          result.status = RasterHistogramStatus::InsufficientMemory;
          break;
        case RasterEvaluationStatus::AllocationFailure:
          result.status = RasterHistogramStatus::AllocationFailure;
          break;
        default:
          result.status = RasterHistogramStatus::InvalidSource;
          break;
      }
      result.diagnostic = std::move(evaluated.diagnostic);
      return result;
    }
    const RasterPixelView edited_view{
        evaluated.image.bytes, evaluated.image.width, evaluated.image.height,
        evaluated.image.row_stride, evaluated.image.encoding};
    return compute_raster_histogram(edited_view, options);
  }

  void run(std::stop_token token) {
    while (!token.stop_requested()) {
      Job job;
      {
        std::unique_lock lock(mutex);
        condition.wait(lock, token, [this] { return queued.has_value(); });
        if (token.stop_requested()) return;
        if (!queued) continue;
        job = std::move(*queued);
        queued.reset();
        active = job.key;
        active_completions = std::move(job.completions);
        job.completions.clear();
      }

      std::shared_ptr<const RasterHistogramResult> completed;
      try {
        completed = std::make_shared<const RasterHistogramResult>(
            evaluate(job, token));
      } catch (...) {
        // A failed analysis must not terminate the persistent worker thread.
      }

      std::vector<Completion> callbacks;
      bool publish = false;
      {
        std::lock_guard lock(mutex);
        if (active && *active == job.key) active.reset();
        callbacks = std::move(active_completions);
        active_completions.clear();
        if (completed && completed->status != RasterHistogramStatus::Cancelled &&
            desired && *desired == job.key) {
          cached_key = job.key;
          cached = std::move(completed);
          publish = true;
        }
      }
      if (publish) {
        for (auto& callback : callbacks) {
          if (!callback) continue;
          try { callback(); } catch (...) {}
        }
      }
    }
  }

  mutable std::mutex mutex;
  std::condition_variable_any condition;
  std::optional<Job> queued;
  std::optional<HistogramKey> active;
  std::vector<Completion> active_completions;
  std::optional<HistogramKey> desired;
  std::optional<HistogramKey> cached_key;
  std::shared_ptr<const RasterHistogramResult> cached;
  std::jthread worker;
};

RasterHistogramService::RasterHistogramService()
    : impl_(std::make_unique<Impl>()) {}

RasterHistogramService::~RasterHistogramService() = default;

void RasterHistogramService::request(
    document::SnapshotPtr snapshot,
    document::ObjectId image_id,
    Completion completion,
    RasterHistogramMode mode) {
  if (!snapshot) {
    cancel();
    return;
  }
  const auto key = make_key(*snapshot, image_id, mode);
  if (!key) {
    cancel();
    return;
  }

  {
    std::lock_guard lock(impl_->mutex);
    impl_->desired = *key;
    if (impl_->cached_key && *impl_->cached_key == *key) return;
    if (impl_->active && *impl_->active == *key) {
      impl_->queued.reset();
      if (completion) impl_->active_completions.push_back(std::move(completion));
      return;
    }
    if (impl_->queued && impl_->queued->key == *key) {
      if (completion)
        impl_->queued->completions.push_back(std::move(completion));
      return;
    }
    Impl::Job job{std::move(snapshot), *key, {}};
    if (completion) job.completions.push_back(std::move(completion));
    impl_->queued = std::move(job);
    if (!impl_->worker.joinable()) {
      impl_->worker = std::jthread(
          [impl = impl_.get()](std::stop_token token) { impl->run(token); });
    }
  }
  impl_->condition.notify_one();
}

void RasterHistogramService::cancel() noexcept {
  std::lock_guard lock(impl_->mutex);
  impl_->desired.reset();
  impl_->queued.reset();
  impl_->cached_key.reset();
  impl_->cached.reset();
}

std::shared_ptr<const RasterHistogramResult> RasterHistogramService::find(
    const document::DocumentSnapshot& snapshot,
    document::ObjectId image_id,
    RasterHistogramMode mode) const {
  const auto key = make_key(snapshot, image_id, mode);
  if (!key) return nullptr;
  std::lock_guard lock(impl_->mutex);
  return impl_->cached_key && *impl_->cached_key == *key
      ? impl_->cached : nullptr;
}

bool RasterHistogramService::is_processing(
    const document::DocumentSnapshot& snapshot,
    document::ObjectId image_id,
    RasterHistogramMode mode) const {
  const auto key = make_key(snapshot, image_id, mode);
  if (!key) return false;
  std::lock_guard lock(impl_->mutex);
  return impl_->active && *impl_->active == *key;
}

}  // namespace pikdraw::raster
