#include "byte_codec.hpp"
#include "pikdraw/document/commands/color_commands.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace pikdraw::document {
using namespace storage_detail;
namespace {
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
  if (snapshot.document_id().empty())
    throw std::invalid_argument("Snapshot has no persistent document ID");
  if (snapshot.page_count() == 0 || snapshot.page_count() > 10000)
    throw std::length_error("Document page count exceeds native format budget");
  Writer payload(true);
  payload.str(snapshot.document_id());
  payload.u32(kCurrentFormatVersion);
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
  payload.count(snapshot.page_count(), 10000);
  payload.u64(snapshot.active_page_id());

  uint64_t maximum_stable_id = 0;
  for (const auto &page : snapshot.pages()) {
    validate_document_structure(page.layout, page.objects);
    for (const auto &[id, object] : page.objects.all()) {
      if (page.layout.color_mode_explicit && !object_colors_match_mode(object, page.layout.color_mode))
        throw std::invalid_argument("Native explicit document mode conflicts with source process colors");
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
  for (const auto &document_page : snapshot.pages()) {
    payload.u64(document_page.id);
    payload.str(document_page.name);
    const auto &page = document_page.layout;
    payload.f64(page.width_mm);
    payload.f64(page.height_mm);
    if (page.layers.size() > 10000 - total_layers)
      throw std::length_error("Too many layers across document pages");
    total_layers += page.layers.size();
    payload.count(page.layers.size(), 10000);
    for (const auto &layer : page.layers) {
      payload.str(layer.name);
      payload.flag(layer.visible);
      payload.flag(layer.locked);
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
      write_object(payload, object, true, source_override, true, true);
      if (const auto* text = std::get_if<TextObject>(&object)) {
        payload.u8(static_cast<uint8_t>(text->alignment));
        payload.flag(text->underline);
        payload.f64(text->frame_width_mm);
        payload.f64(text->line_spacing_ratio);
      } else if (const auto* image = std::get_if<ImageObject>(&object)) {
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
        payload.blob(source.original_bytes,
            static_cast<uint32_t>(RasterSourceMetadata::kMaxSourceBytes));
        payload.blob(source.icc_profile,
            static_cast<uint32_t>(RasterSourceMetadata::kMaxIccProfileBytes));
      }
    }
  }
  Writer container;
  for (const auto byte : kPikDrawMagic)
    container.u8(byte);
  container.u32(kCurrentFormatVersion);
  container.u32(0);
  container.u64(snapshot.epoch());
  container.u64(snapshot.revision());
  container.u32(static_cast<uint32_t>(payload.bytes.size()));
  container.u32(0);
  container.append(payload.bytes);
  const auto crc =
      container_crc(container.bytes.data(), container.bytes.size());
  for (unsigned i = 0; i < 4; ++i)
    container.bytes[32 + i] = static_cast<uint8_t>(crc >> (i * 8));
  return std::move(container.bytes);
}

std::shared_ptr<DocumentSnapshot>
NativeFormat::deserialize(const uint8_t *data, size_t length,
                          std::string *out_error) {
  if (out_error)
    out_error->clear();
  try {
    if (!data || length < 36 || length > max_file_bytes)
      throw std::length_error("Native file has invalid size (budget 256 MiB)");
    Reader header(std::span<const uint8_t>(data, 36));
    for (const auto byte : kPikDrawMagic)
      if (header.u8() != byte)
        throw std::invalid_argument("Invalid PKDR magic");
    const auto version = header.u32();
    if (version != kLegacyFormatVersion &&
        version != kAuthoringSourceFormatVersion &&
        version != kAppearanceFormatVersion &&
        version != kGradientFormatVersion &&
        version != kMultiPageFormatVersion &&
        version != kTextLayoutFormatVersion &&
        version != kRasterAssetFormatVersion &&
        version != kCurrentFormatVersion)
      throw std::invalid_argument(
          "Unsupported native version; v1 is not silently migrated and "
          "versions v2-v9 require a compatible reader");
    if (header.u32() != 0)
      throw std::invalid_argument("Unsupported native feature flags");
    const auto epoch = header.u64();
    const auto revision = header.u64();
    const auto payload_bytes = header.u32();
    const auto crc = header.u32();
    if (epoch == 0 || payload_bytes != length - 36)
      throw std::invalid_argument("Invalid native epoch or payload size");
    if (container_crc(data, length) != crc)
      throw std::invalid_argument("Native header/payload CRC32 mismatch");
    Reader reader(std::span<const uint8_t>(data + 36, payload_bytes),
                  version >= kGradientFormatVersion);
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
    std::vector<DocumentPage> pages;
    PageId active_page_id = kInvalidPageId;
    std::unordered_set<PageId> seen_page_ids;
    std::unordered_set<ObjectId> seen_object_ids;
    ObjectId maximum_object_id = 0;
    uint32_t remaining_elements = max_elements;
    uint32_t remaining_objects = max_objects;
    uint32_t remaining_layer_references = max_objects;
    uint32_t remaining_layers = 10000;
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
      result.layout.color_mode = color_settings.color_mode;
      result.layout.color_profile_name = color_settings.color_profile_name;
      result.layout.raster_dpi = color_settings.raster_dpi;
      result.layout.color_mode_explicit = color_settings.color_mode_explicit;
      result.layout.raster_dpi_explicit = color_settings.raster_dpi_explicit;
      validate_page(result.layout);
      const auto layer_count = reader.count(remaining_layers, 10);
      remaining_layers -= layer_count;
      result.layout.layers.reserve(layer_count);
      for (uint32_t i = 0; i < layer_count; ++i) {
        Layer layer;
        layer.name = reader.str();
        layer.visible = reader.flag();
        layer.locked = reader.flag();
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
                                  version >= kAuthoringSourceFormatVersion);
        if (version >= kAppearanceFormatVersion) {
          std::visit(
              [&](auto &value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, PathObject> ||
                              std::is_same_v<T, TextObject>)
                  value.appearance = read_appearance(reader, remaining_effects);
              },
              object);
        }
        if (version >= kGradientFormatVersion) {
          if (auto *path = std::get_if<PathObject>(&object))
            path->fill_gradient = read_gradient(reader, remaining_gradient_stops);
        }
        if (version >= kTextLayoutFormatVersion) {
          if (auto* text = std::get_if<TextObject>(&object)) {
            text->alignment = static_cast<typography::TextAlignment>(reader.u8());
            text->underline = reader.flag();
            text->frame_width_mm = reader.f64();
            text->line_spacing_ratio = reader.f64();
          }
        }
        if (version >= kRasterAssetFormatVersion) {
          if (auto* image = std::get_if<ImageObject>(&object)) {
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
            source.original_bytes = reader.blob(
                static_cast<uint32_t>(RasterSourceMetadata::kMaxSourceBytes));
            source.icc_profile = reader.blob(
                static_cast<uint32_t>(RasterSourceMetadata::kMaxIccProfileBytes));
          }
        }
        if (color_settings.color_mode_explicit && !object_colors_match_mode(object, color_settings.color_mode))
          throw std::invalid_argument("Native explicit document mode conflicts with source process colors");
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
      return result;
    };

    if (version >= kMultiPageFormatVersion) {
      const auto page_count = reader.count(10000, 37);
      if (page_count == 0) throw std::invalid_argument("Native document has no pages");
      active_page_id = reader.u64();
      pages.reserve(page_count);
      for (uint32_t i = 0; i < page_count; ++i)
        pages.push_back(read_page(true));
    } else {
      pages.push_back(read_page(false));
      active_page_id = pages.front().id;
    }
    if (reader.remaining() != 0)
      throw std::invalid_argument("Trailing native payload bytes");

    auto result = std::make_shared<DocumentSnapshot>(
        epoch, revision, std::move(pages), active_page_id, document_id);
    uint64_t maximum_stable_id = 0;
    for (const auto &page : result->pages()) {
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
