#include "native_format_raster.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace pikdraw::document::storage_detail {

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
    else if (std::holds_alternative<RasterEraserParams>(node.parameters))
      kind = static_cast<uint32_t>(RasterEditKind::RasterEraser);
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
      } else if constexpr (std::is_same_v<T, RasterEraserParams>) {
        if (parameters.points.empty() ||
            parameters.points.size() > RasterEraserParams::kMaxPoints)
          throw std::invalid_argument("Invalid raster eraser point count");
        writer.f64(parameters.diameter_px);
        writer.count(parameters.points.size(), RasterEraserParams::kMaxPoints);
        for (const auto& point : parameters.points) {
          writer.f64(point.x);
          writer.f64(point.y);
        }
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
          std::holds_alternative<RasterColorBalanceParams>(node.parameters) ||
          std::holds_alternative<RasterChannelMixerParams>(node.parameters);
      if (!pointwise || mask.id == 0 || mask.width_px == 0 ||
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
    case static_cast<uint32_t>(RasterEditKind::RasterEraser): {
      if (schema_version < 18)
        throw std::invalid_argument("Raster Eraser nodes require graph schema v18");
      RasterEraserParams eraser;
      eraser.diameter_px = reader.f64();
      const auto point_count = reader.count(RasterEraserParams::kMaxPoints, 16);
      eraser.points.reserve(point_count);
      for (uint32_t point = 0; point < point_count; ++point)
        eraser.points.push_back({reader.f64(), reader.f64()});
      node.parameters = std::move(eraser);
      break;
    }
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
      if (std::holds_alternative<RasterCloneStampParams>(node.parameters) ||
          std::holds_alternative<RasterEraserParams>(node.parameters))
        throw std::invalid_argument("Clone Stamp and Raster Eraser nodes do not support adjustment masks");
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

} // namespace pikdraw::document::storage_detail
