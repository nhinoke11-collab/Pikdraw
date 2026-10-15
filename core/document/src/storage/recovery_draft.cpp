#include "byte_codec.hpp"
#include "pikdraw/document/storage/recovery_draft.hpp"

namespace pikdraw::document {
using namespace storage_detail;
namespace {
constexpr uint8_t magic[8] = {'P', 'K', 'D', 'R', 'D', 'R', 'F', 'T'};
constexpr uint32_t format_version = 1;

uint32_t read_crc(const uint8_t *data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}
} // namespace

std::vector<uint8_t>
RecoveryDraftFormat::serialize(const RecoveryDraftRecord &record) {
  if (record.document_id.empty() || record.document_id.size() > 512 ||
      record.document_epoch == 0 ||
      record.schema_version != kRecoveryDraftSchemaVersion ||
      record.payload.size() > kRecoveryDraftPayloadLimit)
    throw std::invalid_argument("Recovery draft metadata or payload exceeds its schema");
  Writer writer;
  for (auto byte : magic)
    writer.u8(byte);
  writer.u32(format_version);
  writer.u32(record.schema_version);
  writer.str(record.document_id);
  writer.u64(record.document_epoch);
  writer.u64(record.base_revision);
  writer.u32(record.base_document_crc32);
  writer.count(record.payload.size(),
               static_cast<uint32_t>(kRecoveryDraftPayloadLimit));
  writer.append(record.payload);
  writer.u32(NativeFormat::calculate_crc32(writer.bytes.data(),
                                           writer.bytes.size()));
  return std::move(writer.bytes);
}

std::optional<RecoveryDraftRecord> RecoveryDraftFormat::deserialize(
    const uint8_t *data, size_t length, std::string *out_error) {
  if (out_error)
    out_error->clear();
  try {
    constexpr size_t min_bytes = 49;
    constexpr size_t max_bytes = 8 + 4 + 4 + 4 + 512 + 8 + 8 + 4 + 4 +
                                 kRecoveryDraftPayloadLimit + 4;
    if (!data || length < min_bytes || length > max_bytes)
      throw std::length_error("Recovery draft size exceeds its allowed range");
    if (NativeFormat::calculate_crc32(data, length - 4) !=
        read_crc(data + length - 4))
      throw std::invalid_argument("Recovery draft checksum mismatch");
    Reader reader(std::span<const uint8_t>(data, length - 4));
    for (auto byte : magic)
      if (reader.u8() != byte)
        throw std::invalid_argument("Invalid recovery draft magic");
    if (reader.u32() != format_version)
      throw std::invalid_argument("Unsupported recovery draft format version");
    RecoveryDraftRecord record;
    record.schema_version = reader.u32();
    if (record.schema_version != kRecoveryDraftSchemaVersion)
      throw std::invalid_argument("Unsupported recovery draft schema version");
    record.document_id = reader.str(512);
    record.document_epoch = reader.u64();
    record.base_revision = reader.u64();
    record.base_document_crc32 = reader.u32();
    const auto payload_size = reader.count(
        static_cast<uint32_t>(kRecoveryDraftPayloadLimit));
    if (record.document_id.empty() || record.document_id.size() > 512 ||
        record.document_epoch == 0 || payload_size != reader.remaining())
      throw std::invalid_argument("Invalid recovery draft identity or length");
    record.payload.reserve(payload_size);
    for (uint32_t i = 0; i < payload_size; ++i)
      record.payload.push_back(reader.u8());
    return record;
  } catch (const std::exception &error) {
    if (out_error)
      *out_error = error.what();
    return std::nullopt;
  }
}
} // namespace pikdraw::document
