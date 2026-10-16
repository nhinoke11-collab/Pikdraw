#include "pikdraw/vector/interaction/tool_session.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <utility>

namespace pikdraw::vector::interaction {
namespace {
constexpr std::array<uint8_t, 4> kCheckpointMagic{'P', 'D', 'T', 'S'};
constexpr uint32_t kCheckpointVersion = 1;
constexpr size_t kMaxToolIdBytes = 128;

void append_u32(std::vector<uint8_t> &bytes, uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8)
    bytes.push_back(static_cast<uint8_t>(value >> shift));
}

bool read_u32(const std::vector<uint8_t> &bytes, size_t &offset,
              uint32_t &value) {
  if (offset > bytes.size() || bytes.size() - offset < 4)
    return false;
  value = 0;
  for (unsigned shift = 0; shift < 32; shift += 8)
    value |= static_cast<uint32_t>(bytes[offset++]) << shift;
  return true;
}

std::vector<uint8_t> encode_checkpoint(const std::string &tool_id,
                                       const std::vector<uint8_t> &draft,
                                       bool can_finish) {
  if (tool_id.empty() || tool_id.size() > kMaxToolIdBytes ||
      tool_id.size() > std::numeric_limits<uint32_t>::max() ||
      draft.size() > std::numeric_limits<uint32_t>::max())
    throw std::length_error("Tool draft exceeds checkpoint limits");
  std::vector<uint8_t> bytes(kCheckpointMagic.begin(), kCheckpointMagic.end());
  append_u32(bytes, kCheckpointVersion);
  bytes.push_back(can_finish ? 1 : 0);
  append_u32(bytes, static_cast<uint32_t>(tool_id.size()));
  append_u32(bytes, static_cast<uint32_t>(draft.size()));
  bytes.insert(bytes.end(), tool_id.begin(), tool_id.end());
  bytes.insert(bytes.end(), draft.begin(), draft.end());
  if (bytes.size() > document::kRecoveryDraftPayloadLimit)
    throw std::length_error("Tool draft checkpoint exceeds storage limit");
  return bytes;
}

bool decode_checkpoint(const std::vector<uint8_t> &bytes, std::string &tool_id,
                      std::vector<uint8_t> &draft, bool &can_finish,
                      std::string *out_error) {
  const auto fail = [&](const char *message) {
    if (out_error)
      *out_error = message;
    return false;
  };
  if (bytes.size() < 17 || bytes.size() > document::kRecoveryDraftPayloadLimit)
    return fail("Invalid tool draft checkpoint size");
  if (!std::equal(kCheckpointMagic.begin(), kCheckpointMagic.end(),
                  bytes.begin()))
    return fail("Invalid tool draft checkpoint magic");
  size_t offset = kCheckpointMagic.size();
  uint32_t version = 0;
  if (!read_u32(bytes, offset, version) || version != kCheckpointVersion)
    return fail("Unsupported tool draft checkpoint version");
  const uint8_t finish_flag = bytes[offset++];
  if (finish_flag > 1)
    return fail("Invalid tool draft finish flag");
  uint32_t tool_bytes = 0;
  uint32_t draft_bytes = 0;
  if (!read_u32(bytes, offset, tool_bytes) ||
      !read_u32(bytes, offset, draft_bytes) || tool_bytes == 0 ||
      tool_bytes > kMaxToolIdBytes ||
      static_cast<size_t>(tool_bytes) > bytes.size() - offset ||
      static_cast<size_t>(draft_bytes) !=
          bytes.size() - offset - tool_bytes)
    return fail("Invalid tool draft checkpoint lengths");
  tool_id.assign(reinterpret_cast<const char *>(bytes.data() + offset),
                 tool_bytes);
  offset += tool_bytes;
  draft.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end());
  can_finish = finish_flag != 0;
  if (out_error)
    out_error->clear();
  return true;
}
} // namespace

document::RecoveryDraftRecord ToolSession::recovery_record(
    uint32_t base_document_crc32) const {
  if (!active() || !base_snapshot_ || gesture_active_ ||
      (state_ != ToolSessionState::AwaitFirstPoint &&
       state_ != ToolSessionState::PreviewSegment) ||
      draft_payload_.empty())
    throw std::invalid_argument(
        "Only a confirmed, idle draft can be checkpointed");
  auto payload = encode_checkpoint(tool_id_, draft_payload_, can_finish_);
  if (payload.size() > limits_.max_checkpoint_bytes)
    throw std::length_error("Tool draft checkpoint exceeds its configured limit");
  return document::RecoveryDraftRecord{base_snapshot_->document_id(),
                                       base_snapshot_->epoch(),
                                       base_snapshot_->revision(),
                                       base_document_crc32,
                                       document::kRecoveryDraftSchemaVersion,
                                       std::move(payload)};
}

document::SaveResult ToolSession::write_recovery_checkpoint(
    const std::string &path, uint32_t base_document_crc32,
    const document::SaveOptions &options) const {
  try {
    return document::SaveRecoveryService::atomic_write_recovery_draft(
        path, recovery_record(base_document_crc32), options);
  } catch (const std::exception &error) {
    return {false, path, 0, 0, error.what()};
  }
}

ToolSessionResult ToolSession::stage_recovery(
    const document::RecoveryDraftRecord &record,
    document::SnapshotPtr current, uint32_t expected_base_crc32,
    std::string *out_error) {
  if (out_error)
    out_error->clear();
  if (state_ != ToolSessionState::Idle)
    return {ToolSessionResultCode::NeedsResolution,
            "Resolve the current tool session before loading a recovery draft"};
  if (!current || record.document_epoch == 0 ||
      current->document_id() != record.document_id ||
      current->revision() != record.base_revision ||
      record.base_document_crc32 != expected_base_crc32) {
    if (out_error)
      *out_error =
          "Recovery draft identity, revision or base checksum does not match";
    return {ToolSessionResultCode::Stale,
            "Recovery draft does not match the current document"};
  }
  if (record.schema_version != document::kRecoveryDraftSchemaVersion ||
      record.payload.size() > limits_.max_checkpoint_bytes) {
    if (out_error)
      *out_error = "Recovery draft schema or payload limit is unsupported";
    return {ToolSessionResultCode::InvalidInput,
            "Recovery draft schema or payload limit is unsupported"};
  }
  PendingRecovery pending;
  if (!decode_checkpoint(record.payload, pending.tool_id, pending.payload,
                         pending.can_finish, out_error))
    return {ToolSessionResultCode::InvalidInput,
            out_error ? *out_error : "Invalid tool recovery payload"};
  if (pending.payload.size() > limits_.max_draft_bytes) {
    if (out_error)
      *out_error = "Recovered draft exceeds the current tool session budget";
    return {ToolSessionResultCode::BudgetExceeded,
            "Recovered draft exceeds the current tool session budget"};
  }
  if (session_generation_ == std::numeric_limits<uint64_t>::max())
    return {ToolSessionResultCode::InvalidInput,
            "Tool session generation is exhausted"};
  ++session_generation_;
  ++preview_generation_;
  base_snapshot_ = std::move(current);
  pending_recovery_ = std::move(pending);
  state_ = ToolSessionState::RecoveryPending;
  return {ToolSessionResultCode::Applied,
          "Recovery draft is staged and awaits explicit user acceptance"};
}

ToolSessionResult ToolSession::stage_recovery_file(
    const std::string &path, document::SnapshotPtr current,
    uint32_t expected_base_crc32, std::string *out_error) {
  auto record = document::SaveRecoveryService::load_recovery_draft(path,
                                                                   out_error);
  if (!record)
    return {ToolSessionResultCode::InvalidInput,
            out_error ? *out_error : "Could not load tool recovery draft"};
  return stage_recovery(*record, std::move(current), expected_base_crc32,
                        out_error);
}

ToolSessionResult ToolSession::accept_recovery() {
  if (state_ != ToolSessionState::RecoveryPending || !pending_recovery_ ||
      !base_snapshot_)
    return {ToolSessionResultCode::Ignored, "No recovery draft is awaiting"};
  if (pending_recovery_->payload.empty()) {
    discard_recovery();
    return {ToolSessionResultCode::InvalidInput,
            "Empty recovery payload was discarded"};
  }
  tool_id_ = std::move(pending_recovery_->tool_id);
  draft_payload_ = std::move(pending_recovery_->payload);
  can_finish_ = pending_recovery_->can_finish;
  pending_recovery_.reset();
  undo_history_.clear();
  redo_history_.clear();
  preview_payload_.clear();
  state_ = ToolSessionState::PreviewSegment;
  ++preview_generation_;
  return {ToolSessionResultCode::Applied,
          "Recovered draft is active; document remains unchanged"};
}

void ToolSession::discard_recovery() noexcept { cancel(); }

} // namespace pikdraw::vector::interaction
