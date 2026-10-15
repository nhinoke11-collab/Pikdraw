#include "byte_codec.hpp"
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
  validate_document_structure(snapshot.page(), snapshot.objects());
  Writer payload;
  payload.str(snapshot.document_id());
  payload.u32(kMinimumCompatibleVersion);
  payload.u32(kAuthoringPathSchemaMajor);
  payload.u32(kAuthoringPathSchemaMinor);
  uint64_t maximum_stable_id = 0;
  for (const auto &[id, object] : snapshot.objects().all()) {
    maximum_stable_id = std::max(maximum_stable_id, id);
    if (const auto *path = std::get_if<PathObject>(&object);
        path && path->authoring_source) {
      for (const auto &subpath : path->authoring_source->subpaths) {
        maximum_stable_id = std::max(maximum_stable_id, subpath.id.value());
        for (const auto &node : subpath.nodes)
          maximum_stable_id = std::max(maximum_stable_id, node.id.value());
        for (const auto &segment : subpath.segments)
          maximum_stable_id = std::max(maximum_stable_id, segment.id.value());
      }
    }
  }
  reserve_object_id(maximum_stable_id);
  uint64_t next_migration_id = maximum_stable_id ==
                                      std::numeric_limits<uint64_t>::max()
                                  ? 0
                                  : maximum_stable_id + 1;
  std::unordered_map<ObjectId, EditablePath> migrated_sources;
  for (const auto &[id, object] : snapshot.objects().all()) {
    const auto *path = std::get_if<PathObject>(&object);
    if (!path || path->authoring_source)
      continue;
    std::string error;
    auto source = editable_path_from_path2d(path->path, &error);
    if (!source)
      throw std::invalid_argument("Path object " + std::to_string(id) +
                                  " cannot be migrated for v3: " + error);
    assign_stable_source_ids(*source, next_migration_id);
    migrated_sources.emplace(id, std::move(*source));
  }
  const auto &page = snapshot.page();
  payload.f64(page.width_mm);
  payload.f64(page.height_mm);
  payload.count(page.layers.size(), 10000);
  size_t layer_references = 0;
  for (const auto &layer : page.layers) {
    payload.str(layer.name);
    payload.flag(layer.visible);
    payload.flag(layer.locked);
    payload.count(layer.object_ids.size(), max_objects);
    layer_references += layer.object_ids.size();
    if (layer_references > max_objects)
      throw std::length_error("Too many layer references");
    for (auto id : layer.object_ids)
      payload.u64(id);
  }
  const auto objects = snapshot.objects().all();
  payload.count(objects.size(), max_objects);
  size_t elements = 0;
  for (const auto &[id, object] : objects) {
    validate_object_source(id, object);
    if (const auto *path = std::get_if<PathObject>(&object))
      elements += path->path.size();
    if (elements > max_elements)
      throw std::length_error("Too many total path elements");
    const EditablePath *source_override = nullptr;
    if (const auto *path = std::get_if<PathObject>(&object);
        path && !path->authoring_source)
      source_override = &migrated_sources.at(id);
    write_object(payload, object, true, source_override);
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
    if (version != kLegacyFormatVersion && version != kCurrentFormatVersion)
      throw std::invalid_argument("Unsupported native version; v1 is not "
                                  "silently migrated and v3 requires a v3 reader");
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
    Reader reader(std::span<const uint8_t>(data + 36, payload_bytes));
    const auto document_id = reader.str();
    if (document_id.empty())
      throw std::invalid_argument("Empty native document ID");
    if (version == kCurrentFormatVersion) {
      const auto minimum_compatible = reader.u32();
      const auto schema_major = reader.u32();
      const auto schema_minor = reader.u32();
      if (minimum_compatible > kCurrentFormatVersion ||
          minimum_compatible < kMinimumCompatibleVersion)
        throw std::invalid_argument("Unsupported native compatibility version");
      if (schema_major != kAuthoringPathSchemaMajor ||
          schema_minor > kAuthoringPathSchemaMinor)
        throw std::invalid_argument("Unsupported required authoring-path schema");
    }
    Page page;
    page.width_mm = reader.f64();
    page.height_mm = reader.f64();
    validate_page(page);
    const auto layer_count = reader.count(10000, 10);
    page.layers.reserve(layer_count);
    size_t layer_references = 0;
    for (uint32_t i = 0; i < layer_count; ++i) {
      Layer layer;
      layer.name = reader.str();
      layer.visible = reader.flag();
      layer.locked = reader.flag();
      const auto count = reader.count(max_objects, 8);
      layer_references += count;
      if (layer_references > max_objects)
        throw std::length_error("Too many layer references");
      layer.object_ids.reserve(count);
      for (uint32_t j = 0; j < count; ++j)
        layer.object_ids.push_back(reader.u64());
      page.layers.push_back(std::move(layer));
    }
    const auto object_count = reader.count(max_objects, 63);
    std::vector<ObjectVariant> decoded_objects;
    decoded_objects.reserve(object_count);
    std::unordered_set<ObjectId> seen_object_ids;
    ObjectId maximum_object_id = 0;
    uint32_t remaining_elements = max_elements;
    for (uint32_t i = 0; i < object_count; ++i) {
      auto object = read_object(reader, remaining_elements,
                                version == kCurrentFormatVersion);
      const auto id =
          std::visit([](const auto &value) { return value.id; }, object);
      if (id == kInvalidObjectId || !seen_object_ids.insert(id).second)
        throw std::invalid_argument("Duplicate native object ID");
      maximum_object_id = std::max(maximum_object_id, id);
      decoded_objects.push_back(std::move(object));
    }
    if (reader.remaining() != 0)
      throw std::invalid_argument("Trailing native payload bytes");
    reserve_object_id(maximum_object_id);
    PersistentObjectMap objects;
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
      const auto id =
          std::visit([](const auto &value) { return value.id; }, object);
      objects = objects.set(id, std::move(object));
    }
    auto result = std::make_shared<DocumentSnapshot>(
        epoch, revision, std::move(page), std::move(objects), document_id);
    if (!result->objects().empty())
      reserve_object_id((*result->objects().all().rbegin()).first);
    return result;
  } catch (const std::exception &error) {
    if (out_error)
      *out_error = error.what();
    return nullptr;
  }
}

} // namespace pikdraw::document
