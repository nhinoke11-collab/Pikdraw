#include "byte_codec.hpp"
#include "native_chunk_encode.hpp"
#include "native_chunk_decode.hpp"
#include "pikdraw/document/annotations/dimension_object.hpp"
#include "pikdraw/document/commands/color_commands.hpp"
#include "pikdraw/color/profiles/profile_resource.hpp"
#include "resource_budget.hpp"
#include <zlib.h>
#include <algorithm>
#include <bit>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace pikdraw::document {
using namespace storage_detail;
namespace {
constexpr size_t kMaxDocumentIccResourceCount = 10000;
constexpr size_t kMaxDocumentIccBytes = 64u * 1024u * 1024u;
constexpr size_t kMaxPathFeatureSpecBytes = 1024u * 1024u;
constexpr size_t kMaxDocumentPathFeatureSpecBytes = 64u * 1024u * 1024u;
constexpr uint32_t kMaxImportFidelityReports = 10000;
constexpr uint32_t kMaxImportFidelityIssues = 100000;
constexpr size_t kChunkedWriteThreshold = 1024u * 1024u;

void write_import_fidelity_report(Writer& writer,
                                  const ImportFidelityReport& report) {
  if (!report.is_valid())
    throw std::invalid_argument("Invalid import fidelity report metadata");
  writer.str(report.format_name);
  writer.str(report.profile_id);
  writer.u64(report.source_bytes);
  writer.f64(report.source_page_width_mm);
  writer.f64(report.source_page_height_mm);
  writer.u32(report.source_page_index);
  writer.u8(static_cast<uint8_t>(report.coverage));
  writer.u8(static_cast<uint8_t>(report.appearance));
  writer.u8(static_cast<uint8_t>(report.editability));
  writer.u8(static_cast<uint8_t>(report.production));
  writer.count(report.issues.size(), 4096);
  for (const auto& issue : report.issues) {
    writer.u8(static_cast<uint8_t>(issue.severity));
    writer.str(issue.source_locator);
    writer.str(issue.feature_code);
    writer.str(issue.diagnostic_code);
  }
}

ImportFidelityReport read_import_fidelity_report(Reader& reader) {
  ImportFidelityReport report;
  report.format_name = reader.str(64);
  report.profile_id = reader.str(128);
  report.source_bytes = reader.u64();
  report.source_page_width_mm = reader.f64();
  report.source_page_height_mm = reader.f64();
  report.source_page_index = reader.u32();
  const auto coverage = reader.u8();
  const auto appearance = reader.u8();
  const auto editability = reader.u8();
  const auto production = reader.u8();
  if (coverage > static_cast<uint8_t>(ImportReportCoverage::Complete) ||
      appearance > static_cast<uint8_t>(ImportReportAppearance::Verified) ||
      editability > static_cast<uint8_t>(ImportReportEditability::ExplicitConversion) ||
      production > static_cast<uint8_t>(ImportReportProduction::Assessed))
    throw std::invalid_argument("Invalid import fidelity report status");
  report.coverage = static_cast<ImportReportCoverage>(coverage);
  report.appearance = static_cast<ImportReportAppearance>(appearance);
  report.editability = static_cast<ImportReportEditability>(editability);
  report.production = static_cast<ImportReportProduction>(production);
  const auto issue_count = reader.count(4096, 13);
  report.issues.reserve(issue_count);
  for (uint32_t index = 0; index < issue_count; ++index) {
    ImportReportIssue issue;
    const auto severity = reader.u8();
    if (severity < static_cast<uint8_t>(ImportReportIssueSeverity::Info) ||
        severity > static_cast<uint8_t>(ImportReportIssueSeverity::Unsupported))
      throw std::invalid_argument("Invalid import fidelity issue severity");
    issue.severity = static_cast<ImportReportIssueSeverity>(severity);
    issue.source_locator = reader.str(128);
    issue.feature_code = reader.str(64);
    issue.diagnostic_code = reader.str(128);
    report.issues.push_back(std::move(issue));
  }
  if (!report.is_valid())
    throw std::invalid_argument("Invalid import fidelity report metadata");
  return report;
}

std::string profile_resource_key(const color::ProfileIdentifier& identity) {
  return std::to_string(static_cast<unsigned>(identity.color_space)) + ":" +
      identity.checksum_sha256;
}

struct RasterSourceTable {
  struct Asset {
    const std::vector<uint8_t>* inline_bytes{nullptr};
    std::shared_ptr<const RasterSourceProvider> provider;
    uint64_t byte_count{0};
  };
  std::vector<Asset> assets;
  std::unordered_map<const std::vector<uint8_t>*, uint32_t> inline_indices;
  std::unordered_map<const RasterSourceProvider*, uint32_t> provider_indices;
};

template <class T>
struct RasterValueTable {
  std::vector<const std::vector<T>*> assets;
  std::unordered_map<const std::vector<T>*, uint32_t> indices;

  void add(const std::vector<T>* value) {
    if (!value || value->empty() || indices.contains(value)) return;
    if (value->size() > std::numeric_limits<size_t>::max() / sizeof(T))
      throw std::length_error("Raster sample asset exceeds addressable memory");
    const auto byte_count = value->size() * sizeof(T);
    const std::string_view view(
        reinterpret_cast<const char*>(value->data()), byte_count);
    const auto hash = std::hash<std::string_view>{}(view);
    auto& candidates = content_buckets[hash];
    uint32_t index = 0;
    for (const auto candidate : candidates) {
      if (*assets[candidate - 1] == *value) {
        index = candidate;
        break;
      }
    }
    if (index == 0) {
      if (assets.size() >= std::numeric_limits<uint32_t>::max())
        throw std::length_error("Too many raster sample assets");
      index = static_cast<uint32_t>(assets.size() + 1);
      assets.push_back(value);
      candidates.push_back(index);
    }
    indices.emplace(value, index);
  }

  [[nodiscard]] uint32_t index(const std::vector<T>* value) const {
    if (!value || value->empty()) return 0;
    return indices.at(value);
  }

 private:
  std::unordered_map<size_t, std::vector<uint32_t>> content_buckets;
};

struct RasterPixelTables {
  RasterValueTable<uint32_t> rgba8;
  RasterValueTable<uint8_t> rgba16;
};

RasterPixelTables collect_raster_pixel_tables(
    const DocumentSnapshot& snapshot) {
  RasterPixelTables tables;
  for (const auto& page : snapshot.pages()) {
    for (const auto& [id, object] : page.objects.all()) {
      (void)id;
      const auto* image = std::get_if<ImageObject>(&object);
      if (!image) continue;
      tables.rgba8.add(image->shared_pixels_rgba
          ? image->shared_pixels_rgba.get() : &image->pixels_rgba);
      tables.rgba16.add(image->shared_pixels_rgba16le.get());
    }
  }
  return tables;
}

RasterSourceTable collect_raster_sources(const DocumentSnapshot& snapshot) {
  RasterSourceTable table;
  std::unordered_map<size_t, std::vector<uint32_t>> content_buckets;
  for (const auto& page : snapshot.pages()) {
    for (const auto& [id, object] : page.objects.all()) {
      (void)id;
      const auto* image = std::get_if<ImageObject>(&object);
      if (!image) continue;
      const auto& bytes = image->source.original_data();
      uint32_t index = 0;
      if (!bytes.empty()) {
        const auto* identity = &bytes;
        if (const auto found = table.inline_indices.find(identity);
            found != table.inline_indices.end()) {
          index = found->second;
        } else {
          const std::string_view view(
              reinterpret_cast<const char*>(bytes.data()), bytes.size());
          const auto content_hash = std::hash<std::string_view>{}(view);
          if (const auto bucket = content_buckets.find(content_hash);
              bucket != content_buckets.end()) {
            for (const auto candidate : bucket->second) {
              const auto* existing = table.assets[candidate - 1].inline_bytes;
              if (existing && *existing == bytes) {
                index = candidate;
                break;
              }
            }
          }
          if (index == 0) {
            if (table.assets.size() >= std::numeric_limits<uint32_t>::max())
              throw std::length_error("Too many embedded raster source assets");
            index = static_cast<uint32_t>(table.assets.size() + 1);
            table.assets.push_back({&bytes, {}, bytes.size()});
            content_buckets[content_hash].push_back(index);
          }
          table.inline_indices.emplace(identity, index);
        }
      }
      if (index == 0 && image->source.asset_provider && image->asset_source &&
          image->asset_source->mode == RasterStorageMode::Embedded) {
        const auto* identity = image->source.asset_provider.get();
        if (const auto found = table.provider_indices.find(identity);
            found != table.provider_indices.end()) {
          index = found->second;
        } else {
          const auto byte_count = identity->size_bytes();
          if (byte_count == 0 || byte_count > RasterSourceMetadata::kMaxSourceBytes ||
              byte_count != image->asset_source->encoded_byte_length)
            throw std::length_error("Raster source provider length differs from its descriptor");
          if (table.assets.size() >= std::numeric_limits<uint32_t>::max())
            throw std::length_error("Too many embedded raster source assets");
          index = static_cast<uint32_t>(table.assets.size() + 1);
          table.assets.push_back({nullptr, image->source.asset_provider, byte_count});
          table.provider_indices.emplace(identity, index);
        }
      }
      if (index != 0 && image->source.asset_provider) {
        table.provider_indices.emplace(image->source.asset_provider.get(), index);
      }
    }
  }
  return table;
}

void write_raster_source_asset(Writer& writer,
                               const RasterSourceTable::Asset& asset) {
  if (asset.inline_bytes) {
    writer.blob(*asset.inline_bytes,
        static_cast<uint32_t>(RasterSourceMetadata::kMaxSourceBytes));
    return;
  }
  if (!asset.provider || asset.byte_count == 0 ||
      asset.byte_count > RasterSourceMetadata::kMaxSourceBytes)
    throw std::invalid_argument("Native raster source asset has no readable data");
  writer.count(static_cast<size_t>(asset.byte_count),
      static_cast<uint32_t>(RasterSourceMetadata::kMaxSourceBytes));
  std::vector<uint8_t> buffer(64u * 1024u);
  for (uint64_t offset = 0; offset < asset.byte_count;) {
    const auto length = static_cast<size_t>(std::min<uint64_t>(
        buffer.size(), asset.byte_count - offset));
    size_t bytes_read = 0;
    std::string error;
    if (!asset.provider->read_at(offset,
            std::span<uint8_t>(buffer.data(), length), bytes_read, &error) ||
        bytes_read != length)
      throw std::runtime_error(error.empty()
          ? "Could not read the complete raster source provider"
          : "Could not read the raster source provider: " + error);
    writer.append(std::span<const uint8_t>(buffer.data(), bytes_read));
    offset += bytes_read;
  }
}

void write_raster_asset_reference(Writer& writer, const ImageObject& image,
                                  bool use_shared_pixel_table,
                                  uint32_t rgba16_index) {
  writer.flag(image.asset_source.has_value());
  if (image.asset_source) {
    const auto& source = *image.asset_source;
    writer.u64(source.asset_id.high);
    writer.u64(source.asset_id.low);
    writer.u64(source.revision);
    writer.u8(static_cast<uint8_t>(source.mode));
    writer.u64(source.encoded_byte_length);
    writer.str(source.media_type);
    writer.str(source.locator);
    writer.str(source.fingerprint_sha256);
    writer.u64(std::bit_cast<uint64_t>(source.source_modified_ticks));
  }

  writer.u32(RasterEditGraph::kSchemaVersion);
  writer.count(image.edit_graph.nodes.size(), RasterEditGraph::kMaxNodes);
  for (const auto& node : image.edit_graph.nodes) {
    writer.u64(node.id.value);
    writer.flag(node.enabled);
    uint32_t kind = 0;
    if (std::holds_alternative<RasterCropParams>(node.parameters))
      kind = static_cast<uint32_t>(RasterEditKind::Crop);
    else if (std::holds_alternative<RasterFlipParams>(node.parameters))
      kind = static_cast<uint32_t>(RasterEditKind::Flip);
    else if (std::holds_alternative<RasterRotateParams>(node.parameters))
      kind = static_cast<uint32_t>(RasterEditKind::Rotate);
    else if (std::holds_alternative<RasterExposureParams>(node.parameters))
      kind = static_cast<uint32_t>(RasterEditKind::Exposure);
    else if (std::holds_alternative<RasterLevelsParams>(node.parameters))
      kind = static_cast<uint32_t>(RasterEditKind::Levels);
    else if (std::holds_alternative<RasterHslParams>(node.parameters))
      kind = static_cast<uint32_t>(RasterEditKind::Hsl);
    else if (std::holds_alternative<RasterCurvesParams>(node.parameters))
      kind = static_cast<uint32_t>(RasterEditKind::Curves);
    else if (std::holds_alternative<RasterWhiteBalanceParams>(node.parameters))
      kind = static_cast<uint32_t>(RasterEditKind::WhiteBalance);
    else if (std::holds_alternative<RasterVibranceParams>(node.parameters))
      kind = static_cast<uint32_t>(RasterEditKind::Vibrance);
    else if (std::holds_alternative<RasterSelectiveHslParams>(node.parameters))
      kind = static_cast<uint32_t>(RasterEditKind::SelectiveHsl);
    else if (std::holds_alternative<RasterLightParams>(node.parameters))
      kind = static_cast<uint32_t>(RasterEditKind::Light);
    else if (std::holds_alternative<RasterChannelMixerParams>(node.parameters))
      kind = static_cast<uint32_t>(RasterEditKind::ChannelMixer);
    else if (std::holds_alternative<RasterCloneStampParams>(node.parameters))
      kind = static_cast<uint32_t>(RasterEditKind::CloneStamp);
    else if (std::holds_alternative<RasterColorBalanceParams>(node.parameters))
      kind = static_cast<uint32_t>(RasterEditKind::ColorBalance);
    else
      throw std::invalid_argument("Unsupported raster edit node kind");
    if (node.algorithm_version != 1)
      throw std::invalid_argument("Unsupported raster edit algorithm version");
    writer.u32(kind);
    writer.u32(node.algorithm_version);
    std::visit([&](const auto& parameters) {
      using T = std::decay_t<decltype(parameters)>;
      if constexpr (std::is_same_v<T, RasterCropParams>) {
        writer.f64(parameters.x);
        writer.f64(parameters.y);
        writer.f64(parameters.width);
        writer.f64(parameters.height);
      } else if constexpr (std::is_same_v<T, RasterFlipParams>) {
        writer.flag(parameters.horizontal);
        writer.flag(parameters.vertical);
      } else if constexpr (std::is_same_v<T, RasterRotateParams>) {
        writer.f64(parameters.degrees);
      } else if constexpr (std::is_same_v<T, RasterExposureParams>) {
        writer.f64(parameters.exposure_ev);
      } else if constexpr (std::is_same_v<T, RasterLevelsParams>) {
        writer.u8(static_cast<uint8_t>(parameters.channel));
        writer.f64(parameters.black_in);
        writer.f64(parameters.white_in);
        writer.f64(parameters.gamma);
        writer.f64(parameters.black_out);
        writer.f64(parameters.white_out);
      } else if constexpr (std::is_same_v<T, RasterHslParams>) {
        writer.f64(parameters.hue_degrees);
        writer.f64(parameters.saturation_percent);
        writer.f64(parameters.lightness_percent);
      } else if constexpr (std::is_same_v<T, RasterCurvesParams>) {
        writer.u8(static_cast<uint8_t>(parameters.channel));
        for (const auto point : parameters.output_points) writer.f64(point);
      } else if constexpr (std::is_same_v<T, RasterWhiteBalanceParams>) {
        writer.f64(parameters.temperature_shift_percent);
        writer.f64(parameters.tint_percent);
      } else if constexpr (std::is_same_v<T, RasterVibranceParams>) {
        writer.f64(parameters.amount_percent);
      } else if constexpr (std::is_same_v<T, RasterSelectiveHslParams>) {
        writer.u8(static_cast<uint8_t>(parameters.range));
        writer.f64(parameters.hue_degrees);
        writer.f64(parameters.saturation_percent);
        writer.f64(parameters.lightness_percent);
        writer.f64(parameters.feather_percent);
      } else if constexpr (std::is_same_v<T, RasterLightParams>) {
        writer.f64(parameters.exposure_ev);
        writer.f64(parameters.contrast_percent);
        writer.f64(parameters.highlights_percent);
        writer.f64(parameters.shadows_percent);
        writer.f64(parameters.whites_percent);
        writer.f64(parameters.blacks_percent);
      } else if constexpr (std::is_same_v<T, RasterChannelMixerParams>) {
        for (const auto coefficient : parameters.coefficients)
          writer.f64(coefficient);
      } else if constexpr (std::is_same_v<T, RasterCloneStampParams>) {
        writer.f64(parameters.source_x);
        writer.f64(parameters.source_y);
        writer.f64(parameters.target_x);
        writer.f64(parameters.target_y);
        writer.f64(parameters.radius_percent);
        writer.f64(parameters.hardness_percent);
        writer.f64(parameters.opacity_percent);
      } else if constexpr (std::is_same_v<T, RasterColorBalanceParams>) {
        writer.u8(static_cast<uint8_t>(parameters.range));
        writer.f64(parameters.red_cyan_percent);
        writer.f64(parameters.green_magenta_percent);
        writer.f64(parameters.blue_yellow_percent);
        writer.flag(parameters.preserve_luminosity);
      }
    }, node.parameters);
    writer.flag(node.mask.has_value());
    if (node.mask) {
      const auto& mask = *node.mask;
      const uint64_t pixel_count = static_cast<uint64_t>(mask.width_px) * mask.height_px;
      const bool pointwise =
          std::holds_alternative<RasterExposureParams>(node.parameters) ||
          std::holds_alternative<RasterLevelsParams>(node.parameters) ||
          std::holds_alternative<RasterHslParams>(node.parameters) ||
          std::holds_alternative<RasterCurvesParams>(node.parameters) ||
          std::holds_alternative<RasterWhiteBalanceParams>(node.parameters) ||
          std::holds_alternative<RasterVibranceParams>(node.parameters) ||
          std::holds_alternative<RasterSelectiveHslParams>(node.parameters) ||
          std::holds_alternative<RasterLightParams>(node.parameters) ||
          std::holds_alternative<RasterColorBalanceParams>(node.parameters);
      const bool supports_mask = pointwise ||
          std::holds_alternative<RasterChannelMixerParams>(node.parameters);
      if (!supports_mask || mask.id == 0 || mask.width_px == 0 ||
          mask.height_px == 0 || mask.density_percent > 100)
        throw std::invalid_argument("Invalid raster adjustment mask");
      if (mask.kind != RasterAdjustmentMaskKind::LinearGradient &&
          mask.gradient_angle_degrees != 0.0)
        throw std::invalid_argument("Only linear masks can store a gradient angle");
      writer.u64(mask.id);
      writer.u32(mask.width_px);
      writer.u32(mask.height_px);
      writer.u8(mask.density_percent);
      writer.flag(mask.inverted);
      writer.u8(static_cast<uint8_t>(mask.kind));
      if (mask.kind == RasterAdjustmentMaskKind::Bitmap) {
        if (pixel_count != mask.coverage.size())
          throw std::invalid_argument("Raster bitmap mask dimensions do not match its payload");
        writer.blob(mask.coverage, std::numeric_limits<uint32_t>::max());
      } else if (mask.kind == RasterAdjustmentMaskKind::Rectangle ||
                 mask.kind == RasterAdjustmentMaskKind::LinearGradient ||
                 mask.kind == RasterAdjustmentMaskKind::RadialGradient) {
        if (!mask.coverage.empty() || !std::isfinite(mask.left) ||
            !std::isfinite(mask.top) || !std::isfinite(mask.width) ||
            !std::isfinite(mask.height) || !std::isfinite(mask.feather_px) ||
            mask.left < 0.0 || mask.top < 0.0 || mask.width <= 0.0 ||
            mask.height <= 0.0 || mask.left + mask.width > 1.0 ||
            mask.top + mask.height > 1.0 || mask.feather_px < 0.0 ||
            mask.feather_px > (std::max)(mask.width_px, mask.height_px) ||
            (mask.kind == RasterAdjustmentMaskKind::LinearGradient
                ? !std::isfinite(mask.gradient_angle_degrees) ||
                      std::abs(mask.gradient_angle_degrees) > 36000.0
                : mask.gradient_angle_degrees != 0.0))
          throw std::invalid_argument("Invalid raster geometric mask");
        writer.f64(mask.left);
        writer.f64(mask.top);
        writer.f64(mask.width);
        writer.f64(mask.height);
        writer.f64(mask.feather_px);
        if (mask.kind == RasterAdjustmentMaskKind::LinearGradient)
          writer.f64(mask.gradient_angle_degrees);
      } else {
        throw std::invalid_argument("Unknown raster adjustment mask kind");
      }
    }
  }
  if (use_shared_pixel_table) {
    writer.u32(rgba16_index);
  } else {
    const auto high_precision_pixels = image.rgba16le_pixels();
    writer.flag(!high_precision_pixels.empty());
    if (!high_precision_pixels.empty())
      writer.blob(high_precision_pixels,
                  std::numeric_limits<uint32_t>::max());
  }
}

void read_raster_asset_reference(
    Reader& reader, ImageObject& image, bool use_shared_pixel_table,
    const std::vector<std::shared_ptr<const std::vector<uint8_t>>>& rgba16_assets) {
  if (reader.flag()) {
    RasterSourceDescriptor source;
    source.asset_id.high = reader.u64();
    source.asset_id.low = reader.u64();
    source.revision = reader.u64();
    source.mode = static_cast<RasterStorageMode>(reader.u8());
    source.encoded_byte_length = reader.u64();
    source.media_type = reader.str(256);
    source.locator = reader.str(32768);
    source.fingerprint_sha256 = reader.str(64);
    source.source_modified_ticks = std::bit_cast<int64_t>(reader.u64());
    image.asset_source = std::move(source);
  }

  const auto schema_version = reader.u32();
  if (schema_version < 1 || schema_version > RasterEditGraph::kSchemaVersion)
    throw std::invalid_argument("Invalid raster edit graph schema version");
  const auto count = reader.count(RasterEditGraph::kMaxNodes, 10);
  image.edit_graph.nodes.reserve(count);
  for (uint32_t index = 0; index < count; ++index) {
    RasterEditNode node;
    node.id.value = reader.u64();
    node.enabled = reader.flag();
    const auto stored_kind = schema_version == 1 ? reader.u8() : reader.u32();
    uint32_t kind = stored_kind;
    if (schema_version == 1) {
      if (stored_kind > 3)
        throw std::invalid_argument("Unknown legacy raster edit node kind");
      kind = stored_kind + 1;
    }
    if (schema_version >= 2) {
      node.algorithm_version = reader.u32();
      if (node.algorithm_version != 1)
        throw std::invalid_argument("Unsupported raster edit algorithm version");
    }
    switch (kind) {
    case static_cast<uint32_t>(RasterEditKind::Crop):
      node.parameters = RasterCropParams{reader.f64(), reader.f64(),
                                         reader.f64(), reader.f64()};
      break;
    case static_cast<uint32_t>(RasterEditKind::Flip):
      node.parameters = RasterFlipParams{reader.flag(), reader.flag()};
      break;
    case static_cast<uint32_t>(RasterEditKind::Rotate):
      node.parameters = RasterRotateParams{reader.f64()};
      break;
    case static_cast<uint32_t>(RasterEditKind::Exposure):
      node.parameters = RasterExposureParams{reader.f64()};
      break;
    case static_cast<uint32_t>(RasterEditKind::Levels):
      node.parameters = RasterLevelsParams{
          static_cast<RasterToneChannel>(reader.u8()), reader.f64(),
          reader.f64(), reader.f64(), reader.f64(), reader.f64()};
      break;
    case static_cast<uint32_t>(RasterEditKind::Hsl):
      if (schema_version < 5)
        throw std::invalid_argument("HSL raster nodes require graph schema v5");
      node.parameters = RasterHslParams{reader.f64(), reader.f64(), reader.f64()};
      break;
    case static_cast<uint32_t>(RasterEditKind::Curves): {
      if (schema_version < 6)
        throw std::invalid_argument("Curves raster nodes require graph schema v6");
      RasterCurvesParams curves;
      curves.channel = static_cast<RasterToneChannel>(reader.u8());
      for (auto& point : curves.output_points) point = reader.f64();
      node.parameters = curves;
      break;
    }
    case static_cast<uint32_t>(RasterEditKind::WhiteBalance):
      if (schema_version < 7)
        throw std::invalid_argument("White Balance raster nodes require graph schema v7");
      node.parameters = RasterWhiteBalanceParams{reader.f64(), reader.f64()};
      break;
    case static_cast<uint32_t>(RasterEditKind::Vibrance):
      if (schema_version < 8)
        throw std::invalid_argument("Vibrance raster nodes require graph schema v8");
      node.parameters = RasterVibranceParams{reader.f64()};
      break;
    case static_cast<uint32_t>(RasterEditKind::SelectiveHsl):
      if (schema_version < 9)
        throw std::invalid_argument("Selective HSL raster nodes require graph schema v9");
      node.parameters = RasterSelectiveHslParams{
          static_cast<RasterHueRange>(reader.u8()), reader.f64(), reader.f64(),
          reader.f64(), reader.f64()};
      break;
    case static_cast<uint32_t>(RasterEditKind::Light):
      if (schema_version < 12)
        throw std::invalid_argument("Light raster nodes require graph schema v12");
      node.parameters = RasterLightParams{reader.f64(), reader.f64(),
          reader.f64(), reader.f64(), reader.f64(), reader.f64()};
      break;
    case static_cast<uint32_t>(RasterEditKind::ChannelMixer):
      if (schema_version < 13)
        throw std::invalid_argument("Channel Mixer raster nodes require graph schema v13");
      {
        RasterChannelMixerParams mixer;
        for (auto& coefficient : mixer.coefficients) coefficient = reader.f64();
        node.parameters = mixer;
      }
      break;
    case static_cast<uint32_t>(RasterEditKind::CloneStamp):
      if (schema_version < 14)
        throw std::invalid_argument("Clone Stamp raster nodes require graph schema v14");
      node.parameters = RasterCloneStampParams{reader.f64(), reader.f64(),
          reader.f64(), reader.f64(), reader.f64(), reader.f64(), reader.f64()};
      break;
    case static_cast<uint32_t>(RasterEditKind::ColorBalance):
      if (schema_version < 17)
        throw std::invalid_argument("Color Balance raster nodes require graph schema v17");
      node.parameters = RasterColorBalanceParams{
          static_cast<RasterTonalRange>(reader.u8()), reader.f64(), reader.f64(),
          reader.f64(), reader.flag()};
      break;
    default:
      throw std::invalid_argument("Unknown raster edit node kind");
    }
    if (schema_version >= 10 && reader.flag()) {
      RasterAdjustmentMask mask;
      mask.id = reader.u64();
      mask.width_px = reader.u32();
      mask.height_px = reader.u32();
      mask.density_percent = reader.u8();
      mask.inverted = reader.flag();
      if (mask.id == 0 || mask.width_px == 0 || mask.height_px == 0 ||
          mask.density_percent > 100)
        throw std::invalid_argument("Invalid raster adjustment mask payload");
      const auto pixel_count = static_cast<uint64_t>(mask.width_px) * mask.height_px;
      if (schema_version >= 11) {
        const auto kind = reader.u8();
        const uint8_t maximum_kind = schema_version >= 15
            ? static_cast<uint8_t>(RasterAdjustmentMaskKind::RadialGradient)
            : static_cast<uint8_t>(RasterAdjustmentMaskKind::Rectangle);
        if (kind > maximum_kind)
          throw std::invalid_argument("Unknown raster adjustment mask kind");
        mask.kind = static_cast<RasterAdjustmentMaskKind>(kind);
      }
      if (mask.kind == RasterAdjustmentMaskKind::Bitmap) {
        const auto byte_count = reader.count(std::numeric_limits<uint32_t>::max());
        if (pixel_count != byte_count)
          throw std::invalid_argument("Invalid raster bitmap mask dimensions");
        const auto bytes = reader.take(byte_count);
        mask.coverage.assign(bytes.begin(), bytes.end());
      } else {
        mask.left = reader.f64();
        mask.top = reader.f64();
        mask.width = reader.f64();
        mask.height = reader.f64();
        mask.feather_px = reader.f64();
        if (schema_version >= 16 &&
            mask.kind == RasterAdjustmentMaskKind::LinearGradient)
          mask.gradient_angle_degrees = reader.f64();
        if (!std::isfinite(mask.left) || !std::isfinite(mask.top) ||
            !std::isfinite(mask.width) || !std::isfinite(mask.height) ||
            !std::isfinite(mask.feather_px) || mask.left < 0.0 || mask.top < 0.0 ||
            mask.width <= 0.0 || mask.height <= 0.0 ||
            mask.left + mask.width > 1.0 || mask.top + mask.height > 1.0 ||
            mask.feather_px < 0.0 ||
            mask.feather_px > (std::max)(mask.width_px, mask.height_px) ||
            (mask.kind == RasterAdjustmentMaskKind::LinearGradient
                ? !std::isfinite(mask.gradient_angle_degrees) ||
                      std::abs(mask.gradient_angle_degrees) > 36000.0
                : mask.gradient_angle_degrees != 0.0))
          throw std::invalid_argument("Invalid raster geometric mask payload");
      }
      node.mask = std::move(mask);
      if (std::holds_alternative<RasterCloneStampParams>(node.parameters))
        throw std::invalid_argument("Clone Stamp nodes do not support adjustment masks");
    }
    image.edit_graph.nodes.push_back(std::move(node));
  }
  if (use_shared_pixel_table) {
    const auto source_index = reader.u32();
    if (source_index > rgba16_assets.size())
      throw std::invalid_argument("RGBA16 raster asset reference is out of range");
    if (source_index != 0) {
      const auto pixel_count = static_cast<uint64_t>(image.width_px) *
          static_cast<uint64_t>(image.height_px);
      if (pixel_count > std::numeric_limits<uint32_t>::max() / 8u ||
          rgba16_assets[source_index - 1]->size() != pixel_count * 8u)
        throw std::invalid_argument(
            "High-precision raster byte count does not match image dimensions");
      image.shared_pixels_rgba16le = rgba16_assets[source_index - 1];
    }
  } else if (schema_version >= 3 && reader.flag()) {
    const auto pixel_count = static_cast<uint64_t>(image.width_px) *
                             static_cast<uint64_t>(image.height_px);
    if (pixel_count > std::numeric_limits<uint32_t>::max() / 8u)
      throw std::length_error("High-precision raster exceeds native blob size");
    const auto byte_count = reader.count(std::numeric_limits<uint32_t>::max());
    if (byte_count != pixel_count * 8u)
      throw std::invalid_argument(
          "High-precision raster byte count does not match image dimensions");
    const auto encoded_pixels = reader.take(byte_count);
    image.shared_pixels_rgba16le =
        std::make_shared<const std::vector<uint8_t>>(
            encoded_pixels.begin(), encoded_pixels.end());
  }
  // All loaded graphs are upgraded in memory. Future saves use stable kind
  // codes, algorithm versions, optional masks and the optional RGBA16LE source
  // payload; the reader continues to accept graph schemas v1 through v13.
  image.edit_graph.schema_version = RasterEditGraph::kSchemaVersion;
}

void add_profile_resource(
    const std::shared_ptr<const color::ProfileResource>& resource,
    std::vector<std::shared_ptr<const color::ProfileResource>>& resources,
    std::unordered_map<std::string, uint32_t>& identity_indices,
    ProfileResourceIndexMap& pointer_indices,
    size_t& total_bytes) {
  if (!resource) return;
  const auto key = profile_resource_key(resource->identity());
  const auto existing = identity_indices.find(key);
  if (existing != identity_indices.end()) {
    const auto& canonical = resources[existing->second - 1];
    if (canonical->bytes() != resource->bytes())
      throw std::invalid_argument("Conflicting ICC resources share one digest identity");
    pointer_indices.emplace(resource.get(), existing->second);
    return;
  }
  if (resources.size() >= kMaxDocumentIccResourceCount)
    throw std::length_error("Too many embedded ICC resources");
  if (resource->bytes().size() > kMaxDocumentIccBytes - total_bytes)
    throw std::length_error("Embedded ICC resources exceed the document byte budget");
  total_bytes += resource->bytes().size();
  const auto index = static_cast<uint32_t>(resources.size() + 1);
  identity_indices.emplace(key, index);
  pointer_indices.emplace(resource.get(), index);
  resources.push_back(resource);
}

void add_color_profile(const color::ColorValue& value,
                       std::vector<std::shared_ptr<const color::ProfileResource>>& resources,
                       std::unordered_map<std::string, uint32_t>& identity_indices,
                       ProfileResourceIndexMap& pointer_indices,
                       size_t& total_bytes) {
  add_profile_resource(value.profile_resource(), resources, identity_indices,
                       pointer_indices, total_bytes);
}

void add_appearance_profiles(
    const Appearance& appearance,
    std::vector<std::shared_ptr<const color::ProfileResource>>& resources,
    std::unordered_map<std::string, uint32_t>& identity_indices,
    ProfileResourceIndexMap& pointer_indices, size_t& total_bytes) {
  for (const auto& node : appearance.effects) {
    if (const auto* shadow = std::get_if<DropShadowEffect>(&node.parameters))
      add_color_profile(shadow->color, resources, identity_indices,
                        pointer_indices, total_bytes);
  }
}

void assign_stable_source_ids(EditablePath &source, uint64_t &next_id) {
  auto take_id = [&]() -> uint64_t {
    if (next_id == 0)
      throw std::overflow_error("Stable path ID range exhausted");
    const auto result = next_id;
    next_id = result == std::numeric_limits<uint64_t>::max() ? 0 : result + 1;
    reserve_object_id(result);
    return result;
  };
  for (auto &subpath : source.subpaths) {
    subpath.id = SubpathId{take_id()};
    for (auto &node : subpath.nodes)
      node.id = NodeId{take_id()};
    for (auto &segment : subpath.segments)
      segment.id = SegmentId{take_id()};
  }
  if (const auto error = source.validation_error())
    throw std::invalid_argument(*error);
}
} // namespace

std::vector<uint8_t> NativeFormat::serialize(const DocumentSnapshot &snapshot) {
  std::vector<uint8_t> bytes;
  const StreamWrite write = [&bytes](std::span<const uint8_t> chunk) {
    bytes.insert(bytes.end(), chunk.begin(), chunk.end());
  };
  const StreamPatch patch = [&bytes](uint64_t offset,
                                     std::span<const uint8_t> replacement) {
    if (offset > bytes.size() || replacement.size() > bytes.size() - offset)
      throw std::out_of_range("Native stream patch exceeds serialized bytes");
    std::copy(replacement.begin(), replacement.end(),
              bytes.begin() + static_cast<std::ptrdiff_t>(offset));
  };
  serialize_to(snapshot, write, patch);
  return bytes;
}

void NativeFormat::serialize_to(const DocumentSnapshot &snapshot,
                                const StreamWrite& write,
                                const StreamPatch& patch) {
  if (!write || !patch)
    throw std::invalid_argument("Native stream callbacks are required");
  if (snapshot.document_id().empty())
    throw std::invalid_argument("Snapshot has no persistent document ID");
  if (snapshot.page_count() == 0 || snapshot.page_count() > 10000)
    throw std::length_error("Document page count exceeds native format budget");
  const auto raster_source_table = collect_raster_sources(snapshot);
  const bool has_raster_source_table = !raster_source_table.assets.empty();
  const auto payload_version = has_raster_source_table
      ? kRasterAssetTableFormatVersion : kArtboardsFormatVersion;
  RasterPixelTables raster_pixel_tables;
  if (has_raster_source_table)
    raster_pixel_tables = collect_raster_pixel_tables(snapshot);
  const auto make_header = [&](uint32_t container_version, uint32_t flags,
                               uint32_t payload_length) {
    Writer header;
    for (const auto byte : kPikDrawMagic) header.u8(byte);
    header.u32(container_version);
    header.u32(flags);
    header.u64(snapshot.epoch());
    header.u64(snapshot.revision());
    header.u32(payload_length);
    header.u32(0);
    return header;
  };
  bool chunked_container_started = false;
  uint64_t chunk_count = 0;
  uint64_t container_body_bytes = 0;
  uint32_t container_body_crc_state = 0xffffffffu;
  const auto emit_container_body = [&](std::span<const uint8_t> bytes) {
    if (bytes.size() > std::numeric_limits<uint64_t>::max() -
                           container_body_bytes)
      throw std::overflow_error("Native container body size overflows u64");
    write(bytes);
    container_body_bytes += bytes.size();
    container_body_crc_state =
        crc32_update(container_body_crc_state, bytes.data(), bytes.size());
  };
  const auto start_chunked_container = [&]() {
    if (chunked_container_started) return;
    const auto header = make_header(
        has_raster_source_table ? kChunkedContainerFormatVersion
                                : kLegacyChunkedContainerFormatVersion,
        kNativeCompressionFlagChunked,
        std::numeric_limits<uint32_t>::max());
    Writer extension;
    extension.u64(0);
    extension.u64(0);
    write(header.bytes);
    write(extension.bytes);
    chunked_container_started = true;
  };
  const auto emit_payload_chunk = [&](std::span<const uint8_t> raw) {
    start_chunked_container();
    if (chunk_count == std::numeric_limits<uint64_t>::max())
      throw std::overflow_error("Native container chunk count overflows u64");
    append_native_chunk_record(emit_container_body, raw);
    ++chunk_count;
  };
  if (has_raster_source_table) start_chunked_container();
  Writer payload(true, emit_payload_chunk, kNativeChunkBytes);
  payload.str(snapshot.document_id());
  payload.u32(payload_version);
  payload.u32(kAuthoringPathSchemaMajor);
  payload.u32(kAuthoringPathSchemaMinor);
  payload.u32(kAppearanceSchemaVersion);
  payload.u32(kAppearanceSchemaMinor);
  payload.u32(kGradientSchemaMajor);
  payload.u32(kGradientSchemaMinor);
  payload.u8(static_cast<uint8_t>(snapshot.color_mode()));
  payload.str(snapshot.color_profile_name());
  payload.u32(static_cast<uint32_t>(snapshot.raster_dpi()));
  payload.u8((snapshot.page().color_mode_explicit ? 1 : 0) |
             (snapshot.page().raster_dpi_explicit ? 2 : 0));

  std::vector<std::shared_ptr<const PdfSourceAsset>> pdf_source_assets;
  std::unordered_map<const PdfSourceAsset*, uint32_t> pdf_source_asset_indices;
  for (const auto& page : snapshot.pages()) {
    if (!page.pdf_source) continue;
    const auto* source = page.pdf_source->asset.get();
    if (pdf_source_asset_indices.contains(source)) continue;
    if (pdf_source_assets.size() >= 10000)
      throw std::length_error("Too many embedded PDF source assets");
    const auto index = static_cast<uint32_t>(pdf_source_assets.size() + 1);
    pdf_source_asset_indices.emplace(source, index);
    pdf_source_assets.push_back(page.pdf_source->asset);
  }
  std::vector<std::shared_ptr<const color::ProfileResource>> profile_resources;
  std::unordered_map<std::string, uint32_t> profile_resource_indices;
  ProfileResourceIndexMap profile_resource_pointer_indices;
  size_t total_icc_bytes = 0;
  for (const auto& page : snapshot.pages()) {
    validate_page(page.layout);
    add_profile_resource(page.layout.color_profile_resource, profile_resources,
                         profile_resource_indices, profile_resource_pointer_indices,
                         total_icc_bytes);
    for (const auto& [id, object] : page.objects.all()) {
      (void)id;
      if (const auto* path = std::get_if<PathObject>(&object)) {
        add_color_profile(path->fill, profile_resources, profile_resource_indices,
                          profile_resource_pointer_indices, total_icc_bytes);
        add_color_profile(path->stroke, profile_resources, profile_resource_indices,
                          profile_resource_pointer_indices, total_icc_bytes);
        if (path->contour_paints) {
          add_color_profile(path->contour_paints->outline, profile_resources,
                            profile_resource_indices,
                            profile_resource_pointer_indices, total_icc_bytes);
          add_color_profile(path->contour_paints->fill, profile_resources,
                            profile_resource_indices,
                            profile_resource_pointer_indices, total_icc_bytes);
        }
        add_appearance_profiles(path->appearance, profile_resources,
                                profile_resource_indices,
                                profile_resource_pointer_indices, total_icc_bytes);
        if (path->fill_gradient)
          for (const auto& stop : path->fill_gradient->stops)
            add_color_profile(stop.color, profile_resources,
                              profile_resource_indices,
                              profile_resource_pointer_indices, total_icc_bytes);
      } else if (const auto* text = std::get_if<TextObject>(&object)) {
        add_color_profile(text->fill, profile_resources, profile_resource_indices,
                          profile_resource_pointer_indices, total_icc_bytes);
        add_appearance_profiles(text->appearance, profile_resources,
                                profile_resource_indices,
                                profile_resource_pointer_indices, total_icc_bytes);
      }
    }
  }
  struct PathFeatureSpecSource {
    ObjectId id;
    const PathObject* path;
  };
  std::vector<PathFeatureSpecSource> path_feature_specs;
  size_t total_path_feature_spec_bytes = 0;
  for (const auto& page : snapshot.pages()) {
    for (const auto& [id, object] : page.objects.all()) {
      const auto* path = std::get_if<PathObject>(&object);
      if (!path || path->contour_spec.empty()) continue;
      if (path->contour_spec.size() > kMaxPathFeatureSpecBytes)
        throw std::length_error("Path feature specification exceeds 1 MiB");
      if (path->contour_spec.starts_with("dimension:")) {
        std::string reason;
        if (!deserialize_dimension_spec(path->contour_spec, &reason))
          throw std::invalid_argument("Invalid dimension feature: " + reason);
      }
      if (path->contour_spec.size() >
          kMaxDocumentPathFeatureSpecBytes - total_path_feature_spec_bytes)
        throw std::length_error("Path feature specifications exceed document budget");
      total_path_feature_spec_bytes += path->contour_spec.size();
      path_feature_specs.push_back({id, path});
    }
  }
  payload.u8(static_cast<uint8_t>(snapshot.content_mode()));
  payload.count(pdf_source_assets.size(), 10000);
  for (const auto& source : pdf_source_assets) {
    payload.str(source->sha256_hex);
    payload.u32(source->page_count);
    payload.blob(*source->bytes, max_blob_bytes);
  }
  if (has_raster_source_table) {
    payload.count(raster_source_table.assets.size(), max_objects);
    for (const auto& source : raster_source_table.assets)
      write_raster_source_asset(payload, source);
    payload.count(raster_pixel_tables.rgba8.assets.size(), max_objects);
    for (const auto* pixels : raster_pixel_tables.rgba8.assets) {
      payload.count(pixels->size(), max_image_pixels);
      for (const auto pixel : *pixels) payload.u32(pixel);
    }
    payload.count(raster_pixel_tables.rgba16.assets.size(), max_objects);
    for (const auto* pixels : raster_pixel_tables.rgba16.assets)
      payload.blob(*pixels, std::numeric_limits<uint32_t>::max());
  }
  payload.count(snapshot.page_count(), 10000);
  payload.u64(snapshot.active_page_id());

  uint64_t maximum_stable_id = 0;
  for (const auto &page : snapshot.pages()) {
    validate_document_structure(page.layout, page.objects);
    for (const auto &[id, object] : page.objects.all()) {
      maximum_stable_id = std::max(maximum_stable_id, id);
      const Appearance *appearance = nullptr;
      if (const auto *path = std::get_if<PathObject>(&object)) {
        appearance = &path->appearance;
        if (path->fill_gradient) {
          maximum_stable_id = std::max(maximum_stable_id, path->fill_gradient->id);
          for (const auto &stop : path->fill_gradient->stops)
            maximum_stable_id = std::max(maximum_stable_id, stop.id);
        }
        if (path->authoring_source)
          for (const auto &subpath : path->authoring_source->subpaths) {
            maximum_stable_id = std::max(maximum_stable_id, subpath.id.value());
            for (const auto &node : subpath.nodes)
              maximum_stable_id = std::max(maximum_stable_id, node.id.value());
            for (const auto &segment : subpath.segments)
              maximum_stable_id = std::max(maximum_stable_id, segment.id.value());
          }
      } else if (const auto *text = std::get_if<TextObject>(&object))
        appearance = &text->appearance;
      if (appearance)
        for (const auto &effect : appearance->effects)
          maximum_stable_id = std::max(maximum_stable_id, effect.id);
    }
  }
  reserve_object_id(maximum_stable_id);
  uint64_t next_migration_id = maximum_stable_id ==
                                       std::numeric_limits<uint64_t>::max()
                                   ? 0
                                   : maximum_stable_id + 1;
  std::unordered_map<ObjectId, EditablePath> migrated_sources;
  for (const auto &page : snapshot.pages()) {
    for (const auto &[id, object] : page.objects.all()) {
      const auto *path = std::get_if<PathObject>(&object);
      if (!path || path->authoring_source)
        continue;
      std::string error;
      auto source = editable_path_from_path2d(path->path, &error);
      if (!source)
        throw std::invalid_argument("Path object " + std::to_string(id) +
                                    " cannot be migrated for native v6: " + error);
      assign_stable_source_ids(*source, next_migration_id);
      migrated_sources.emplace(id, std::move(*source));
    }
  }

  size_t total_objects = 0;
  size_t total_layers = 0;
  size_t total_layer_references = 0;
  size_t total_elements = 0;
  size_t total_effects = 0;
  size_t total_gradient_stops = 0;
  size_t total_import_reports = 0;
  size_t total_import_issues = 0;
  size_t total_artboards = 0;
  for (const auto &document_page : snapshot.pages()) {
    payload.u64(document_page.id);
    payload.str(document_page.name);
    const auto &page = document_page.layout;
    payload.f64(page.width_mm);
    payload.f64(page.height_mm);
    payload.u8(static_cast<uint8_t>(page.display_length_unit));
    payload.flag(page.display_length_unit_explicit);
    payload.u8(static_cast<uint8_t>(page.color_mode));
    payload.str(page.color_profile_name);
    payload.u32(static_cast<uint32_t>(page.raster_dpi));
    payload.u8((page.color_mode_explicit ? 1 : 0) |
               (page.raster_dpi_explicit ? 2 : 0));
    if (page.color_profile_resource) {
      const auto key = profile_resource_key(page.color_profile_resource->identity());
      payload.u32(profile_resource_indices.at(key));
    } else {
      payload.u32(0);
    }
    payload.flag(document_page.pdf_source.has_value());
    if (document_page.pdf_source) {
      const auto& reference = *document_page.pdf_source;
      payload.u32(pdf_source_asset_indices.at(reference.asset.get()));
      payload.u32(reference.source_page_index);
      payload.u8(reference.rotation_quarters_clockwise);
    }
    const auto* pdf_preview = document_page.pdf_source
        ? &*document_page.pdf_source : nullptr;
    payload.flag(pdf_preview && pdf_preview->preview_rgba);
    if (pdf_preview && pdf_preview->preview_rgba) {
      payload.u32(pdf_preview->preview_width_px);
      payload.u32(pdf_preview->preview_height_px);
      payload.count(pdf_preview->preview_rgba->size(), max_image_pixels);
      for (const uint32_t pixel : *pdf_preview->preview_rgba)
        payload.u32(pixel);
    }
    if (document_page.import_fidelity_reports.size() >
        kMaxImportFidelityReports - total_import_reports)
      throw std::length_error("Too many import fidelity reports in document");
    total_import_reports += document_page.import_fidelity_reports.size();
    payload.count(document_page.import_fidelity_reports.size(),
                  kMaxImportFidelityReports);
    for (const auto& report : document_page.import_fidelity_reports) {
      if (report.issues.size() > kMaxImportFidelityIssues - total_import_issues)
        throw std::length_error("Too many import fidelity issues in document");
      total_import_issues += report.issues.size();
      write_import_fidelity_report(payload, report);
    }
    std::vector<Artboard> artboards = document_page.artboards;
    if (artboards.empty())
      artboards.push_back(make_page_bounds_artboard(document_page.id, page));
    if (artboards.size() > 10000 - total_artboards)
      throw std::length_error("Too many artboards across document pages");
    total_artboards += artboards.size();
    if (page.layers.size() > 10000 - total_layers)
      throw std::length_error("Too many layers across document pages");
    total_layers += page.layers.size();
    payload.count(page.layers.size(), 10000);
    for (const auto &layer : page.layers) {
      payload.str(layer.name);
      payload.flag(layer.visible);
      payload.flag(layer.locked);
      payload.flag(layer.printable);
      payload.u32(layer.color_rgba);
      if (layer.object_ids.size() > max_objects - total_layer_references)
        throw std::length_error("Too many total layer references");
      total_layer_references += layer.object_ids.size();
      payload.count(layer.object_ids.size(), max_objects);
      for (auto id : layer.object_ids)
        payload.u64(id);
    }
    const auto objects = document_page.objects.all();
    if (objects.size() > max_objects - total_objects)
      throw std::length_error("Too many objects across document pages");
    total_objects += objects.size();
    payload.count(objects.size(), max_objects);
    for (const auto &[id, object] : objects) {
      validate_object_source(id, object);
      if (const auto *path = std::get_if<PathObject>(&object)) {
        if (path->path.size() > max_elements - total_elements)
          throw std::length_error("Too many total path elements");
        total_elements += path->path.size();
        if (path->fill_gradient) {
          if (path->fill_gradient->stops.size() >
              kMaxDocumentGradientStops - total_gradient_stops)
            throw std::length_error("Too many gradient stops across document pages");
          total_gradient_stops += path->fill_gradient->stops.size();
        }
        if (path->appearance.effects.size() > kMaxEffectsPerDocument - total_effects)
          throw std::length_error("Too many effect nodes across document pages");
        total_effects += path->appearance.effects.size();
      } else if (const auto *text = std::get_if<TextObject>(&object)) {
        if (text->appearance.effects.size() > kMaxEffectsPerDocument - total_effects)
          throw std::length_error("Too many effect nodes across document pages");
        total_effects += text->appearance.effects.size();
      } else if (const auto *image = std::get_if<ImageObject>(&object);
                 image && image->clip_path) {
        if (image->clip_path->size() > max_elements - total_elements)
          throw std::length_error("Too many total path elements");
        total_elements += image->clip_path->size();
      }
      const EditablePath *source_override = nullptr;
      if (const auto *path = std::get_if<PathObject>(&object);
          path && !path->authoring_source)
        source_override = &migrated_sources.at(id);
      write_object(payload, object, true, source_override, true, true,
                   &profile_resource_pointer_indices,
                   !has_raster_source_table);
      if (const auto* text = std::get_if<TextObject>(&object)) {
        payload.u8(static_cast<uint8_t>(text->alignment));
        payload.flag(text->underline);
        payload.f64(text->frame_width_mm);
        payload.f64(text->line_spacing_ratio);
        payload.flag(text->strikethrough);
        payload.f64(text->letter_spacing_mm);
      } else if (const auto* image = std::get_if<ImageObject>(&object)) {
        if (has_raster_source_table) {
          const auto* rgba8_pixels = image->shared_pixels_rgba
              ? image->shared_pixels_rgba.get() : &image->pixels_rgba;
          payload.u32(raster_pixel_tables.rgba8.index(rgba8_pixels));
        }
        const auto& source = image->source;
        payload.str(source.media_type);
        payload.f64(source.dpi_x);
        payload.f64(source.dpi_y);
        payload.flag(source.dpi_from_source);
        payload.u32(source.frame_index);
        payload.u32(source.frame_count);
        payload.u32(source.source_bits_per_pixel);
        payload.u32(source.exif_orientation);
        payload.flag(source.orientation_applied);
        const auto& original_bytes = source.original_data();
        if (has_raster_source_table) {
          uint32_t source_index = 0;
          if (!original_bytes.empty())
            source_index = raster_source_table.inline_indices.at(&original_bytes);
          else if (source.asset_provider)
            source_index = raster_source_table.provider_indices.at(
                source.asset_provider.get());
          payload.u32(source_index);
        } else {
          payload.blob(original_bytes,
              static_cast<uint32_t>(RasterSourceMetadata::kMaxSourceBytes));
        }
        payload.blob(source.icc_profile,
            static_cast<uint32_t>(RasterSourceMetadata::kMaxIccProfileBytes));
        payload.u8(static_cast<uint8_t>(source.preview_pixel_color_basis));
        const auto rgba16_index = has_raster_source_table
            ? raster_pixel_tables.rgba16.index(
                  image->shared_pixels_rgba16le.get())
            : 0u;
        write_raster_asset_reference(payload, *image,
                                     has_raster_source_table, rgba16_index);
      } else if (const auto* grp = std::get_if<GroupObject>(&object)) {
        payload.flag(grp->clip.has_value());
        if (grp->clip) {
          payload.u64(grp->clip->mask_object_id);
          payload.flag(grp->clip->lock_contents_to_frame);
          payload.flag(grp->clip->preserve_frame_appearance);
          payload.u8(static_cast<uint8_t>(grp->clip->fit_mode));
          payload.flag(grp->clip->is_empty_frame);
        }
      }
    }
    payload.count(artboards.size(), 10000);
    for (const auto& artboard : artboards) {
      if (!artboard.is_valid_for(document_page.id) ||
          (artboard.linked_to_page_bounds &&
           (artboard.x_mm != 0.0 || artboard.y_mm != 0.0 ||
            artboard.width_mm != page.width_mm || artboard.height_mm != page.height_mm)))
        throw std::invalid_argument("Invalid artboard in native document");
      payload.u64(artboard.id);
      payload.u64(artboard.owner_page_id);
      payload.str(artboard.name);
      payload.f64(artboard.x_mm);
      payload.f64(artboard.y_mm);
      payload.f64(artboard.width_mm);
      payload.f64(artboard.height_mm);
      payload.flag(artboard.linked_to_page_bounds);
    }
  }
  payload.count(profile_resources.size(),
                static_cast<uint32_t>(kMaxDocumentIccResourceCount));
  for (const auto& resource : profile_resources) {
    const auto& identity = resource->identity();
    payload.str(identity.name);
    payload.u8(static_cast<uint8_t>(identity.color_space));
    payload.str(identity.checksum_sha256);
    payload.blob(resource->bytes(),
                 static_cast<uint32_t>(color::ProfileResource::kMaxProfileBytes));
  }
  payload.count(path_feature_specs.size(), max_objects);
  for (const auto& feature : path_feature_specs) {
    payload.u64(feature.id);
    payload.str(feature.path->contour_spec);
    payload.flag(feature.path->contour_paints.has_value());
    if (feature.path->contour_paints) {
      write_color(payload, feature.path->contour_paints->outline,
                  &profile_resource_pointer_indices);
      write_color(payload, feature.path->contour_paints->fill,
                  &profile_resource_pointer_indices);
    }
  }
  const bool use_chunked_container = has_raster_source_table ||
      payload.size() >= kChunkedWriteThreshold;
  if (use_chunked_container) {
    start_chunked_container();
    payload.flush();
    Writer extension;
    extension.u64(payload.size());
    extension.u64(chunk_count);
    patch(36, extension.bytes);

    const auto header = make_header(
        has_raster_source_table ? kChunkedContainerFormatVersion
                                : kLegacyChunkedContainerFormatVersion,
        kNativeCompressionFlagChunked,
        std::numeric_limits<uint32_t>::max());
    Writer prefix;
    prefix.append(header.bytes);
    prefix.append(extension.bytes);
    const auto prefix_crc = NativeFormat::calculate_crc32(
        prefix.bytes.data(), prefix.bytes.size());
    const auto body_crc = container_body_crc_state ^ 0xffffffffu;
    const auto crc = combine_crc32(prefix_crc, body_crc, container_body_bytes);
    const uint8_t crc_bytes[4]{static_cast<uint8_t>(crc),
        static_cast<uint8_t>(crc >> 8), static_cast<uint8_t>(crc >> 16),
        static_cast<uint8_t>(crc >> 24)};
    patch(32, std::span<const uint8_t>(crc_bytes, sizeof(crc_bytes)));
    return;
  }

  const auto header = make_header(
      kArtboardsFormatVersion, 0, static_cast<uint32_t>(payload.size()));
  uint32_t crc_state = 0xffffffffu;
  const auto emit_legacy = [&](std::span<const uint8_t> bytes) {
    if (bytes.empty()) return;
    write(bytes);
    crc_state = crc32_update(crc_state, bytes.data(), bytes.size());
  };
  emit_legacy(header.bytes);
  emit_legacy(payload.bytes);
  const auto crc = crc_state ^ 0xffffffffu;
  const uint8_t crc_bytes[4]{static_cast<uint8_t>(crc),
      static_cast<uint8_t>(crc >> 8), static_cast<uint8_t>(crc >> 16),
      static_cast<uint8_t>(crc >> 24)};
  patch(32, std::span<const uint8_t>(crc_bytes, sizeof(crc_bytes)));
}

std::shared_ptr<DocumentSnapshot>
NativeFormat::deserialize(const uint8_t *data, size_t length,
                          std::string *out_error,
                          const std::atomic_bool *cancel_requested) {
  if (out_error)
    out_error->clear();
  try {
    if (!data || length < 36)
      throw std::length_error("Native file is shorter than its header");
    const auto memory_budget = native_read_memory_budget_bytes();
    if (memory_budget == 0 || length > memory_budget)
      throw std::length_error(
          "Native file exceeds the current available-memory quota");
    Reader header(std::span<const uint8_t>(data, 36), false,
                  cancel_requested);
    for (const auto byte : kPikDrawMagic)
      if (header.u8() != byte)
        throw std::invalid_argument("Invalid PKDR magic");
    const auto container_version = header.u32();
    if (container_version != kLegacyFormatVersion &&
        container_version != kAuthoringSourceFormatVersion &&
        container_version != kAppearanceFormatVersion &&
        container_version != kGradientFormatVersion &&
        container_version != kMultiPageFormatVersion &&
        container_version != kTextLayoutFormatVersion &&
        container_version != kRasterAssetFormatVersion &&
        container_version != kDocumentColorFormatVersion &&
        container_version != kPowerClipFormatVersion &&
        container_version != kPdfSourceFormatVersion &&
        container_version != kPdfSourcePreviewFormatVersion &&
        container_version != kPageDisplayUnitFormatVersion &&
        container_version != kPageColorSettingsFormatVersion &&
        container_version != kPageIccResourceFormatVersion &&
        container_version != kRasterPreviewColorBasisFormatVersion &&
        container_version != kPaintProfileBindingFormatVersion &&
        container_version != kPathFeatureSpecFormatVersion &&
        container_version != kRasterEditGraphFormatVersion &&
        container_version != kImportFidelityReportFormatVersion &&
        container_version != kTextCharacterPropertiesFormatVersion &&
        container_version != kLayerPropertiesFormatVersion &&
        container_version != kArtboardsFormatVersion &&
        container_version != kLegacyChunkedContainerFormatVersion &&
        container_version != kChunkedContainerFormatVersion)
      throw std::invalid_argument(
          "Unsupported native version; the requested schema is not supported "
          "by this reader");
    const auto flags = header.u32();
    const auto epoch = header.u64();
    const auto revision = header.u64();
    const auto stored_payload_bytes = header.u32();
    const auto crc = header.u32();
    if (epoch == 0)
      throw std::invalid_argument("Invalid native document epoch");
    if (container_crc_cancellable(data, length, cancel_requested) != crc)
      throw std::invalid_argument("Native header/payload CRC32 mismatch");
    std::vector<uint8_t> decoded_payload;
    std::span<const uint8_t> payload_span;
    if (container_version == kLegacyChunkedContainerFormatVersion ||
        container_version == kChunkedContainerFormatVersion) {
      if (flags != kNativeCompressionFlagChunked ||
          stored_payload_bytes != std::numeric_limits<uint32_t>::max() ||
          length < kChunkedContainerHeaderBytes)
        throw std::invalid_argument("Invalid PKDR chunked header");
      Reader extension(std::span<const uint8_t>(
          data + 36, length - 36), false, cancel_requested);
      const auto uncompressed_bytes = extension.u64();
      const auto chunk_count = extension.u64();
      if (uncompressed_bytes == 0 ||
          uncompressed_bytes > std::numeric_limits<size_t>::max() ||
          uncompressed_bytes > memory_budget)
        throw std::length_error(
            "Decoded native payload exceeds the current memory quota");
      const auto expected_chunks = uncompressed_bytes / kNativeChunkBytes +
          (uncompressed_bytes % kNativeChunkBytes == 0 ? 0 : 1);
      if (chunk_count != expected_chunks ||
          chunk_count > extension.remaining() / kChunkedContainerChunkBytes)
        throw std::invalid_argument("Invalid PKDR chunk count");
      decoded_payload.reserve(static_cast<size_t>(uncompressed_bytes));
      for (uint64_t chunk_index = 0; chunk_index < chunk_count; ++chunk_index) {
        if (cancel_requested &&
            cancel_requested->load(std::memory_order_acquire))
          throw std::runtime_error("Native load cancelled");
        const auto raw_size = extension.u32();
        const auto stored_size = extension.u32();
        const auto codec = extension.u8();
        const auto reserved0 = extension.u8();
        const auto reserved1 = extension.u8();
        const auto reserved2 = extension.u8();
        const auto chunk_crc = extension.u32();
        const auto remaining_raw = uncompressed_bytes - decoded_payload.size();
        const auto expected_raw = static_cast<uint32_t>(
            std::min<uint64_t>(kNativeChunkBytes, remaining_raw));
        const auto maximum_stored = static_cast<uint32_t>(
            compressBound(static_cast<uLong>(expected_raw)));
        if (raw_size != expected_raw || stored_size == 0 || codec > 1 ||
            reserved0 != 0 || reserved1 != 0 || reserved2 != 0 ||
            stored_size > maximum_stored ||
            stored_size > extension.remaining())
          throw std::invalid_argument("Invalid PKDR chunk record");
        const auto stored = extension.take(stored_size);
        const auto output_offset = decoded_payload.size();
        const auto cancelled = [cancel_requested] {
          return cancel_requested &&
              cancel_requested->load(std::memory_order_acquire);
        };
        if (codec == 0) {
          if (stored_size != raw_size)
            throw std::invalid_argument("Invalid stored PKDR chunk size");
          decoded_payload.insert(decoded_payload.end(), stored.begin(),
                                 stored.end());
        } else {
          decoded_payload.resize(output_offset + raw_size);
          auto output = std::span<uint8_t>(
              decoded_payload.data() + output_offset, raw_size);
          const auto status = inflate_native_chunk(stored, output, cancelled);
          if (status == NativeDecodeStatus::Cancelled)
            throw std::runtime_error("Native load cancelled");
          if (status != NativeDecodeStatus::Success)
            throw std::invalid_argument("Invalid PKDR DEFLATE chunk");
        }
        uint32_t actual_chunk_crc = 0;
        const auto crc_status = calculate_chunk_crc32(
            std::span<const uint8_t>(decoded_payload.data() + output_offset,
                                     raw_size),
            cancelled, actual_chunk_crc);
        if (crc_status == NativeDecodeStatus::Cancelled)
          throw std::runtime_error("Native load cancelled");
        if (crc_status != NativeDecodeStatus::Success ||
            actual_chunk_crc != chunk_crc)
          throw std::invalid_argument("PKDR chunk checksum mismatch");
      }
      if (decoded_payload.size() != uncompressed_bytes ||
          extension.remaining() != 0)
        throw std::invalid_argument("Invalid PKDR chunked payload length");
      payload_span = std::span<const uint8_t>(decoded_payload);
    } else {
      if (flags != 0)
        throw std::invalid_argument("Unsupported native feature flags");
      if (stored_payload_bytes != length - 36)
        throw std::invalid_argument("Invalid native payload size");
      payload_span = std::span<const uint8_t>(data + 36,
                                              stored_payload_bytes);
    }
    const auto version = container_version == kChunkedContainerFormatVersion
        ? kCurrentFormatVersion
        : container_version == kLegacyChunkedContainerFormatVersion
            ? kArtboardsFormatVersion : container_version;
    Reader reader(payload_span, version >= kGradientFormatVersion,
                  cancel_requested);
    const auto document_id = reader.str();
    if (document_id.empty())
      throw std::invalid_argument("Empty native document ID");
    if (version >= kAuthoringSourceFormatVersion) {
      const auto minimum_compatible = reader.u32();
      const auto schema_major = reader.u32();
      const auto schema_minor = reader.u32();
      if (minimum_compatible != version)
        throw std::invalid_argument("Unsupported native compatibility version");
      if (schema_major != kAuthoringPathSchemaMajor ||
          schema_minor > kAuthoringPathSchemaMinor)
        throw std::invalid_argument("Unsupported required authoring-path schema");
      if (version >= kAppearanceFormatVersion) {
        const auto appearance_major = reader.u32();
        const auto appearance_minor = reader.u32();
        if (appearance_major != kAppearanceSchemaVersion ||
            appearance_minor > kAppearanceSchemaMinor)
          throw std::invalid_argument("Unsupported required appearance schema");
      }
      if (version >= kGradientFormatVersion) {
        const auto gradient_major = reader.u32();
        const auto gradient_minor = reader.u32();
        if (gradient_major != kGradientSchemaMajor ||
            gradient_minor > kGradientSchemaMinor)
          throw std::invalid_argument("Unsupported required gradient schema");
      }
    }
    Page color_settings;
    if (version >= kDocumentColorFormatVersion) {
      color_settings.color_mode = static_cast<color::DocumentColorMode>(reader.u8());
      color_settings.color_profile_name = reader.str(256);
      const auto dpi = reader.u32();
      if (dpi != 72 && dpi != 150 && dpi != 300)
        throw std::invalid_argument("Invalid native raster effects DPI");
      color_settings.raster_dpi = static_cast<int32_t>(dpi);
      const auto settings_flags = reader.u8();
      if (settings_flags > 3) throw std::invalid_argument("Invalid native document color/DPI flags");
      color_settings.color_mode_explicit = (settings_flags & 1) != 0;
      color_settings.raster_dpi_explicit = (settings_flags & 2) != 0;
      validate_page(color_settings);
    }
    DocumentContentMode content_mode = DocumentContentMode::Editable;
    std::vector<std::shared_ptr<const PdfSourceAsset>> pdf_source_assets;
    std::vector<std::shared_ptr<const std::vector<uint8_t>>>
        raster_source_assets;
    std::vector<std::shared_ptr<const std::vector<uint32_t>>>
        raster_rgba8_assets;
    std::vector<std::shared_ptr<const std::vector<uint8_t>>>
        raster_rgba16_assets;
    if (version >= kPdfSourceFormatVersion) {
      const auto raw_mode = reader.u8();
      if (raw_mode > static_cast<uint8_t>(DocumentContentMode::PdfPrintLocked))
        throw std::invalid_argument("Invalid native document content mode");
      content_mode = static_cast<DocumentContentMode>(raw_mode);
      const auto asset_count = reader.count(10000, 76);
      pdf_source_assets.reserve(asset_count);
      for (uint32_t i = 0; i < asset_count; ++i) {
        auto source = std::make_shared<PdfSourceAsset>();
        source->sha256_hex = reader.str(64);
        source->page_count = reader.u32();
        auto bytes = reader.blob(max_blob_bytes);
        source->bytes = std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
        if (source->sha256_hex.size() != 64 || source->page_count == 0 ||
            source->bytes->empty())
          throw std::invalid_argument("Invalid embedded PDF source asset metadata");
        pdf_source_assets.push_back(std::move(source));
      }
    }
    if (version >= kRasterAssetTableFormatVersion) {
      const auto asset_count = reader.count(max_objects, 4);
      if (asset_count == 0)
        throw std::invalid_argument("Native raster asset table is empty");
      raster_source_assets.reserve(asset_count);
      for (uint32_t i = 0; i < asset_count; ++i) {
        auto bytes = reader.blob(
            static_cast<uint32_t>(RasterSourceMetadata::kMaxSourceBytes));
        if (bytes.empty())
          throw std::invalid_argument("Empty embedded raster source asset");
        raster_source_assets.push_back(
            std::make_shared<const std::vector<uint8_t>>(std::move(bytes)));
      }
      const auto rgba8_count = reader.count(max_objects, 4);
      raster_rgba8_assets.reserve(rgba8_count);
      for (uint32_t i = 0; i < rgba8_count; ++i) {
        const auto pixel_count = reader.count(max_image_pixels, sizeof(uint32_t));
        if (pixel_count == 0)
          throw std::invalid_argument("Empty embedded RGBA8 raster asset");
        auto pixels = std::make_shared<std::vector<uint32_t>>();
        pixels->reserve(pixel_count);
        for (uint32_t pixel = 0; pixel < pixel_count; ++pixel)
          pixels->push_back(reader.u32());
        raster_rgba8_assets.push_back(std::move(pixels));
      }
      const auto rgba16_count = reader.count(max_objects, 4);
      raster_rgba16_assets.reserve(rgba16_count);
      for (uint32_t i = 0; i < rgba16_count; ++i) {
        auto pixels = reader.blob(std::numeric_limits<uint32_t>::max());
        if (pixels.empty())
          throw std::invalid_argument("Empty embedded RGBA16 raster asset");
        raster_rgba16_assets.push_back(
            std::make_shared<const std::vector<uint8_t>>(std::move(pixels)));
      }
    }
    std::vector<uint32_t> page_profile_indices;
    std::vector<DocumentPage> pages;
    PageId active_page_id = kInvalidPageId;
    std::unordered_set<PageId> seen_page_ids;
    std::unordered_set<ObjectId> seen_object_ids;
    ObjectId maximum_object_id = 0;
    uint32_t remaining_elements = max_elements;
    uint32_t remaining_objects = max_objects;
    uint32_t remaining_layer_references = max_objects;
    uint32_t remaining_layers = 10000;
    size_t total_import_reports = 0;
    size_t total_import_issues = 0;
    size_t remaining_effects = kMaxEffectsPerDocument;
    size_t remaining_gradient_stops = kMaxDocumentGradientStops;

    auto read_page = [&](bool has_identity) {
      DocumentPage result;
      if (has_identity) {
        result.id = reader.u64();
        result.name = reader.str();
        if (result.id == kInvalidPageId || result.name.empty() ||
            !seen_page_ids.insert(result.id).second)
          throw std::invalid_argument("Invalid or duplicate native page identity");
      } else {
        result = make_document_page("Page 1");
        seen_page_ids.insert(result.id);
      }
      result.layout.width_mm = reader.f64();
      result.layout.height_mm = reader.f64();
      if (version >= kPageDisplayUnitFormatVersion) {
        const auto raw_unit = reader.u8();
        if (raw_unit > static_cast<uint8_t>(geometry::units::LengthUnit::Pica))
          throw std::invalid_argument("Invalid native page display length unit");
        result.layout.display_length_unit =
            static_cast<geometry::units::LengthUnit>(raw_unit);
        result.layout.display_length_unit_explicit = reader.flag();
      }
      if (version >= kPageColorSettingsFormatVersion) {
        const auto raw_mode = reader.u8();
        if (raw_mode > static_cast<uint8_t>(color::DocumentColorMode::Rgb))
          throw std::invalid_argument("Invalid native page color mode");
        result.layout.color_mode = static_cast<color::DocumentColorMode>(raw_mode);
        result.layout.color_profile_name = reader.str(256);
        const auto dpi = reader.u32();
        if (dpi != 72 && dpi != 150 && dpi != 300)
          throw std::invalid_argument("Invalid native page raster effects DPI");
        result.layout.raster_dpi = static_cast<int32_t>(dpi);
        const auto settings_flags = reader.u8();
        if (settings_flags > 3)
          throw std::invalid_argument("Invalid native page color/DPI flags");
        result.layout.color_mode_explicit = (settings_flags & 1) != 0;
        result.layout.raster_dpi_explicit = (settings_flags & 2) != 0;
      }
      uint32_t profile_resource_index = 0;
      if (version >= kPageIccResourceFormatVersion)
        profile_resource_index = reader.u32();
      if (version >= kPdfSourceFormatVersion && reader.flag()) {
        const auto asset_id = reader.u32();
        const auto source_page_index = reader.u32();
        const auto rotation = reader.u8();
        if (asset_id == 0 || asset_id > pdf_source_assets.size() || rotation > 3)
          throw std::invalid_argument("Invalid native PDF source page locator");
        result.pdf_source = PdfSourcePageReference{
            pdf_source_assets[asset_id - 1], source_page_index, rotation,
            0, 0, {}};
      }
      if (version >= kPdfSourcePreviewFormatVersion && reader.flag()) {
        if (!result.pdf_source)
          throw std::invalid_argument("Native PDF page preview has no source reference");
        const auto width = reader.u32();
        const auto height = reader.u32();
        const auto pixel_count = reader.count(max_image_pixels, sizeof(uint32_t));
        const uint64_t expected = static_cast<uint64_t>(width) * height;
        if (width == 0 || height == 0 || expected != pixel_count)
          throw std::invalid_argument("Invalid native PDF page preview dimensions");
        auto pixels = std::make_shared<std::vector<uint32_t>>();
        pixels->reserve(pixel_count);
        for (uint32_t pixel = 0; pixel < pixel_count; ++pixel)
          pixels->push_back(reader.u32());
        result.pdf_source->preview_width_px = width;
        result.pdf_source->preview_height_px = height;
        result.pdf_source->preview_rgba = std::move(pixels);
      }
      if (version >= kImportFidelityReportFormatVersion) {
        const auto report_count = reader.count(kMaxImportFidelityReports, 4);
        if (report_count > kMaxImportFidelityReports - total_import_reports)
          throw std::length_error("Too many import fidelity reports in native document");
        total_import_reports += report_count;
        result.import_fidelity_reports.reserve(report_count);
        for (uint32_t index = 0; index < report_count; ++index) {
          auto report = read_import_fidelity_report(reader);
          if (report.issues.size() > kMaxImportFidelityIssues - total_import_issues)
            throw std::length_error("Too many import fidelity issues in native document");
          total_import_issues += report.issues.size();
          result.import_fidelity_reports.push_back(std::move(report));
        }
      }
      if (version < kPageColorSettingsFormatVersion) {
        result.layout.color_mode = color_settings.color_mode;
        result.layout.color_profile_name = color_settings.color_profile_name;
        result.layout.raster_dpi = color_settings.raster_dpi;
        result.layout.color_mode_explicit = color_settings.color_mode_explicit;
        result.layout.raster_dpi_explicit = color_settings.raster_dpi_explicit;
      }
      validate_page(result.layout);
      const auto layer_count = reader.count(
          remaining_layers, version >= kLayerPropertiesFormatVersion ? 15 : 10);
      remaining_layers -= layer_count;
      result.layout.layers.reserve(layer_count);
      for (uint32_t i = 0; i < layer_count; ++i) {
        Layer layer;
        layer.name = reader.str();
        layer.visible = reader.flag();
        layer.locked = reader.flag();
        if (version >= kLayerPropertiesFormatVersion) {
          layer.printable = reader.flag();
          layer.color_rgba = reader.u32();
        }
        const auto reference_count = reader.count(remaining_layer_references, 8);
        remaining_layer_references -= reference_count;
        layer.object_ids.reserve(reference_count);
        for (uint32_t j = 0; j < reference_count; ++j)
          layer.object_ids.push_back(reader.u64());
        result.layout.layers.push_back(std::move(layer));
      }

      const auto object_count = reader.count(remaining_objects, 63);
      remaining_objects -= object_count;
      std::vector<ObjectVariant> decoded_objects;
      decoded_objects.reserve(object_count);
      for (uint32_t i = 0; i < object_count; ++i) {
        auto object = read_object(reader, remaining_elements,
                                  version >= kAuthoringSourceFormatVersion,
                                  version >= kPaintProfileBindingFormatVersion,
                                  version < kRasterAssetTableFormatVersion);
        if (version >= kAppearanceFormatVersion) {
          std::visit(
              [&](auto &value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, PathObject> ||
                              std::is_same_v<T, TextObject>)
                  value.appearance = read_appearance(
                      reader, remaining_effects,
                      version >= kPaintProfileBindingFormatVersion);
              },
              object);
        }
        if (version >= kGradientFormatVersion) {
          if (auto *path = std::get_if<PathObject>(&object))
            path->fill_gradient = read_gradient(
                reader, remaining_gradient_stops,
                version >= kPaintProfileBindingFormatVersion);
        }
        if (version >= kTextLayoutFormatVersion) {
          if (auto* text = std::get_if<TextObject>(&object)) {
            text->alignment = static_cast<typography::TextAlignment>(reader.u8());
            text->underline = reader.flag();
            text->frame_width_mm = reader.f64();
            text->line_spacing_ratio = reader.f64();
            if (version >= kTextCharacterPropertiesFormatVersion) {
              text->strikethrough = reader.flag();
              text->letter_spacing_mm = reader.f64();
            }
          }
        }
        if (version >= kRasterAssetFormatVersion) {
          if (auto* image = std::get_if<ImageObject>(&object)) {
            if (version >= kRasterAssetTableFormatVersion) {
              const auto pixel_index = reader.u32();
              if (pixel_index == 0 || pixel_index > raster_rgba8_assets.size())
                throw std::invalid_argument(
                    "RGBA8 raster asset reference is out of range");
              const auto expected_pixels =
                  static_cast<uint64_t>(image->width_px) *
                  static_cast<uint64_t>(image->height_px);
              if (expected_pixels > max_image_pixels ||
                  raster_rgba8_assets[pixel_index - 1]->size() !=
                      expected_pixels)
                throw std::invalid_argument(
                    "RGBA8 raster asset size does not match image dimensions");
              image->shared_pixels_rgba = raster_rgba8_assets[pixel_index - 1];
            }
            auto& source = image->source;
            source.media_type = reader.str(64);
            source.dpi_x = reader.f64();
            source.dpi_y = reader.f64();
            source.dpi_from_source = reader.flag();
            source.frame_index = reader.u32();
            source.frame_count = reader.u32();
            source.source_bits_per_pixel = reader.u32();
            const auto orientation = reader.u32();
            if (orientation == 0 || orientation > 8)
              throw std::invalid_argument("Invalid native raster orientation metadata");
            source.exif_orientation = static_cast<uint16_t>(orientation);
            source.orientation_applied = reader.flag();
            if (version >= kRasterAssetTableFormatVersion) {
              const auto source_index = reader.u32();
              if (source_index > raster_source_assets.size())
                throw std::invalid_argument(
                    "Raster source asset reference is out of range");
              if (source_index != 0)
                source.shared_original_bytes =
                    raster_source_assets[source_index - 1];
            } else {
              auto original_bytes = reader.blob(
                  static_cast<uint32_t>(RasterSourceMetadata::kMaxSourceBytes));
              if (!original_bytes.empty())
                source.shared_original_bytes =
                    std::make_shared<const std::vector<uint8_t>>(
                        std::move(original_bytes));
            }
            source.icc_profile = reader.blob(
                static_cast<uint32_t>(RasterSourceMetadata::kMaxIccProfileBytes));
            if (version >= kRasterPreviewColorBasisFormatVersion) {
              const auto color_basis = reader.u8();
              if (color_basis > static_cast<uint8_t>(RasterPixelColorBasis::EmbeddedProfileUnapplied))
                throw std::invalid_argument("Invalid native raster preview color basis");
              source.preview_pixel_color_basis =
                  static_cast<RasterPixelColorBasis>(color_basis);
            }
            if (version >= kRasterEditGraphFormatVersion)
              read_raster_asset_reference(
                  reader, *image,
                  version >= kRasterAssetTableFormatVersion,
                  raster_rgba16_assets);
          }
        }
        if (version >= kPowerClipFormatVersion) {
          if (auto* grp = std::get_if<GroupObject>(&object)) {
            if (reader.flag()) {
              ClipProperties clip;
              clip.mask_object_id = reader.u64();
              clip.lock_contents_to_frame = reader.flag();
              clip.preserve_frame_appearance = reader.flag();
              clip.fit_mode = static_cast<ClipContentFitMode>(reader.u8());
              clip.is_empty_frame = reader.flag();
              grp->clip = clip;
            }
          }
        }
        const auto id = std::visit([](const auto &value) { return value.id; }, object);
        if (id == kInvalidObjectId || !seen_object_ids.insert(id).second)
          throw std::invalid_argument("Duplicate native object ID");
        maximum_object_id = std::max(maximum_object_id, id);
        decoded_objects.push_back(std::move(object));
      }
      reserve_object_id(maximum_object_id);
      for (auto &object : decoded_objects) {
        if (version == kLegacyFormatVersion) {
          if (auto *path = std::get_if<PathObject>(&object)) {
            std::string migration_error;
            auto source = editable_path_from_path2d(path->path, &migration_error);
            if (!source)
              throw std::invalid_argument(
                  "Legacy v2 path object " + std::to_string(path->id) +
                  " cannot be migrated losslessly: " + migration_error);
            path->set_authoring_source(std::move(*source));
          }
        }
        const auto id = std::visit([](const auto &value) { return value.id; }, object);
        result.objects = result.objects.set(id, std::move(object));
      }
      if (version >= kArtboardsFormatVersion) {
        const auto count = reader.count(10000, 53);
        result.artboards.reserve(count);
        for (uint32_t index = 0; index < count; ++index) {
          Artboard artboard;
          artboard.id = reader.u64();
          artboard.owner_page_id = reader.u64();
          artboard.name = reader.str(1024);
          artboard.x_mm = reader.f64();
          artboard.y_mm = reader.f64();
          artboard.width_mm = reader.f64();
          artboard.height_mm = reader.f64();
          artboard.linked_to_page_bounds = reader.flag();
          result.artboards.push_back(std::move(artboard));
        }
      } else {
        result.artboards.clear();
        result.artboards.push_back(make_page_bounds_artboard(result.id, result.layout));
      }
      page_profile_indices.push_back(profile_resource_index);
      return result;
    };

    if (version >= kMultiPageFormatVersion) {
      const auto minimum_page_bytes = version >= kArtboardsFormatVersion
          ? 53 : version >= kImportFidelityReportFormatVersion
          ? 49 : version >= kPageIccResourceFormatVersion
          ? 45 : version >= kPageDisplayUnitFormatVersion
          ? 41 : version >= kPdfSourcePreviewFormatVersion
          ? 39 : version >= kPdfSourceFormatVersion ? 38 : 37;
      const auto page_count = reader.count(10000, minimum_page_bytes);
      if (page_count == 0) throw std::invalid_argument("Native document has no pages");
      active_page_id = reader.u64();
      pages.reserve(page_count);
      for (uint32_t i = 0; i < page_count; ++i)
        pages.push_back(read_page(true));
    } else {
      pages.push_back(read_page(false));
      active_page_id = pages.front().id;
    }
    if (version >= kPageIccResourceFormatVersion) {
      const auto resource_count = reader.count(
          static_cast<uint32_t>(kMaxDocumentIccResourceCount), 13);
      std::vector<std::shared_ptr<const color::ProfileResource>> profile_resources;
      profile_resources.reserve(resource_count);
      std::unordered_set<std::string> seen_profile_keys;
      size_t total_icc_bytes = 0;
      for (uint32_t i = 0; i < resource_count; ++i) {
        auto name = reader.str(256);
        const auto raw_space = reader.u8();
        const bool is_supported_resource_space =
            raw_space == static_cast<uint8_t>(color::ColorSpace::Rgb) ||
            raw_space == static_cast<uint8_t>(color::ColorSpace::Cmyk) ||
            (version >= kPaintProfileBindingFormatVersion &&
             raw_space == static_cast<uint8_t>(color::ColorSpace::Grayscale));
        if (!is_supported_resource_space)
          throw std::invalid_argument("Unsupported native ICC color space");
        const auto space = static_cast<color::ColorSpace>(raw_space);
        auto expected_digest = reader.str(64);
        if (expected_digest.size() != 64)
          throw std::invalid_argument("Invalid native ICC digest length");
        auto bytes = reader.blob(
            static_cast<uint32_t>(color::ProfileResource::kMaxProfileBytes));
        if (bytes.size() > kMaxDocumentIccBytes - total_icc_bytes)
          throw std::length_error("Native ICC resources exceed the document byte budget");
        total_icc_bytes += bytes.size();
        auto loaded = color::ProfileResource::create(
            std::move(name), space, std::move(bytes));
        if (!loaded || loaded.resource->identity().checksum_sha256 != expected_digest)
          throw std::invalid_argument("Invalid native ICC resource or digest mismatch");
        const auto key = profile_resource_key(loaded.resource->identity());
        if (!seen_profile_keys.insert(key).second)
          throw std::invalid_argument("Duplicate native ICC resource identity");
        profile_resources.push_back(std::move(loaded.resource));
      }
      if (page_profile_indices.size() != pages.size())
        throw std::logic_error("Native ICC page references are incomplete");
      std::vector<bool> referenced_resources(profile_resources.size(), false);
      for (size_t i = 0; i < pages.size(); ++i) {
        const auto index = page_profile_indices[i];
        if (index == 0) continue;
        if (index > profile_resources.size())
          throw std::invalid_argument("Native page references an absent ICC resource");
        referenced_resources[index - 1] = true;
        pages[i].layout.color_profile_resource = profile_resources[index - 1];
        validate_page(pages[i].layout);
      }
      auto resolve_color = [&](color::ColorValue& value) {
        const auto index = value.pending_profile_resource_index();
        value.set_pending_profile_resource_index(0);
        if (index == 0) return;
        if (index > profile_resources.size())
          throw std::invalid_argument("Native paint references an absent ICC resource");
        const auto& resource = profile_resources[index - 1];
        if (!value.set_profile_resource(resource) || !value.is_valid())
          throw std::invalid_argument("Native paint ICC color space does not match its value");
        referenced_resources[index - 1] = true;
      };
      if (version >= kPaintProfileBindingFormatVersion) {
        auto resolve_appearance = [&](Appearance& appearance) {
          for (auto& node : appearance.effects)
            if (auto* shadow = std::get_if<DropShadowEffect>(&node.parameters))
              resolve_color(shadow->color);
        };
        for (auto& page : pages) {
          for (const auto& [id, stored_object] : page.objects.all()) {
            auto object = stored_object;
            if (auto* path = std::get_if<PathObject>(&object)) {
              resolve_color(path->fill);
              resolve_color(path->stroke);
              resolve_appearance(path->appearance);
              if (path->fill_gradient) {
                auto gradient = std::make_shared<GradientPaint>(*path->fill_gradient);
                for (auto& stop : gradient->stops)
                  resolve_color(stop.color);
                path->fill_gradient = std::move(gradient);
              }
            } else if (auto* text = std::get_if<TextObject>(&object)) {
              resolve_color(text->fill);
              resolve_appearance(text->appearance);
            }
            page.objects = page.objects.set(id, std::move(object));
          }
        }
      }
      if (version >= kPathFeatureSpecFormatVersion) {
        struct DecodedPathFeature {
          std::string spec;
          std::optional<PathContourPaints> paints;
        };
        const auto feature_count = reader.count(max_objects, 13);
        std::unordered_map<ObjectId, DecodedPathFeature> path_feature_specs;
        path_feature_specs.reserve(feature_count);
        size_t total_feature_bytes = 0;
        for (uint32_t i = 0; i < feature_count; ++i) {
          const auto id = reader.u64();
          if (id == kInvalidObjectId)
            throw std::invalid_argument("Native path feature has an invalid object ID");
          auto spec = reader.str(static_cast<uint32_t>(kMaxPathFeatureSpecBytes));
          if (spec.empty())
            throw std::invalid_argument("Native path feature specification is empty");
          if (spec.size() > kMaxDocumentPathFeatureSpecBytes - total_feature_bytes)
            throw std::length_error("Native path feature specifications exceed document budget");
          total_feature_bytes += spec.size();
          if (spec.starts_with("dimension:")) {
            std::string reason;
            if (!deserialize_dimension_spec(spec, &reason))
              throw std::invalid_argument("Invalid native dimension feature: " + reason);
          }
          DecodedPathFeature feature{std::move(spec), std::nullopt};
          if (reader.flag()) {
            PathContourPaints paints;
            paints.outline = read_color(reader, true);
            paints.fill = read_color(reader, true);
            resolve_color(paints.outline);
            resolve_color(paints.fill);
            feature.paints = std::move(paints);
          }
          if (!path_feature_specs.emplace(id, std::move(feature)).second)
            throw std::invalid_argument("Duplicate native path feature object ID");
        }
        size_t applied_features = 0;
        for (auto& page : pages) {
          for (const auto& [id, stored_object] : page.objects.all()) {
            const auto feature = path_feature_specs.find(id);
            if (feature == path_feature_specs.end()) continue;
            auto object = stored_object;
            auto* path = std::get_if<PathObject>(&object);
            if (!path)
              throw std::invalid_argument("Native path feature references a non-path object");
            path->contour_spec = std::move(feature->second.spec);
            path->contour_paints = std::move(feature->second.paints);
            page.objects = page.objects.set(id, std::move(object));
            ++applied_features;
          }
        }
        if (applied_features != path_feature_specs.size())
          throw std::invalid_argument("Native path feature references an absent object");
      }
      if (std::find(referenced_resources.begin(), referenced_resources.end(), false) !=
          referenced_resources.end())
        throw std::invalid_argument("Native ICC dictionary contains an unreferenced resource");
    }
    if (reader.remaining() != 0)
      throw std::invalid_argument("Trailing native payload bytes");

    auto result = std::make_shared<DocumentSnapshot>(
        epoch, revision, std::move(pages), active_page_id, document_id, content_mode);
    uint64_t maximum_stable_id = 0;
    for (const auto &page : result->pages()) {
      if (cancel_requested &&
          cancel_requested->load(std::memory_order_acquire))
        throw std::runtime_error("Native load cancelled");
      for (const auto &[id, object] : page.objects.all()) {
        maximum_stable_id = std::max(maximum_stable_id, id);
        const Appearance *appearance = nullptr;
        if (const auto *path = std::get_if<PathObject>(&object)) {
          appearance = &path->appearance;
          if (path->fill_gradient) {
            maximum_stable_id = std::max(maximum_stable_id, path->fill_gradient->id);
            for (const auto &stop : path->fill_gradient->stops)
              maximum_stable_id = std::max(maximum_stable_id, stop.id);
          }
          if (path->authoring_source)
            for (const auto &subpath : path->authoring_source->subpaths) {
              maximum_stable_id = std::max(maximum_stable_id, subpath.id.value());
              for (const auto &node : subpath.nodes)
                maximum_stable_id = std::max(maximum_stable_id, node.id.value());
              for (const auto &segment : subpath.segments)
                maximum_stable_id = std::max(maximum_stable_id, segment.id.value());
            }
        } else if (const auto *text = std::get_if<TextObject>(&object)) {
          appearance = &text->appearance;
        }
        if (appearance)
          for (const auto &effect : appearance->effects)
            maximum_stable_id = std::max(maximum_stable_id, effect.id);
      }
    }
    reserve_object_id(maximum_stable_id);
    return result;
  } catch (const std::exception &error) {
    if (out_error)
      *out_error = error.what();
    return nullptr;
  }
}

} // namespace pikdraw::document
