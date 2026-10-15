#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pikdraw::document {

inline constexpr uint32_t kRecoveryDraftSchemaVersion = 1;
inline constexpr size_t kRecoveryDraftPayloadLimit = 16u * 1024u * 1024u;

/// A bounded in-progress tool record. Its payload is not a committed document.
struct RecoveryDraftRecord {
  std::string document_id;
  uint64_t document_epoch{0};
  uint64_t base_revision{0};
  uint32_t base_document_crc32{0};
  uint32_t schema_version{kRecoveryDraftSchemaVersion};
  std::vector<uint8_t> payload;
};

class RecoveryDraftFormat {
public:
  [[nodiscard]] static std::vector<uint8_t>
  serialize(const RecoveryDraftRecord &record);
  [[nodiscard]] static std::optional<RecoveryDraftRecord>
  deserialize(const uint8_t *data, size_t length,
              std::string *out_error = nullptr);
};

} // namespace pikdraw::document
