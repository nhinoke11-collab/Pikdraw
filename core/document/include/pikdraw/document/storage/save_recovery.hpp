#pragma once

#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include "pikdraw/document/storage/recovery_draft.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pikdraw::document {

struct SaveResult {
  bool success{false};
  std::string path;
  uint64_t saved_epoch{0};
  uint64_t saved_revision{0};
  std::string error_message;
};

struct RecoveryInfo {
  std::string path;
  uint64_t epoch{0};
  uint64_t revision{0};
  std::string timestamp;
};

enum class SaveFaultPoint { None, AfterWrite, BeforeFlush, BeforeReplace };
struct SaveOptions {
  // Deterministic fault injection for recovery tests. Production callers leave
  // None.
  SaveFaultPoint fault{SaveFaultPoint::None};
};

/// Service managing atomic file writes, crash recovery, and checkpoints
class SaveRecoveryService {
public:
  /// Atomically saves snapshot to disk using temp-file and atomic replace
  [[nodiscard]] static SaveResult atomic_save(const std::string &target_path,
                                              const DocumentSnapshot &snapshot,
                                              const SaveOptions &options = {});

  /// Loads document snapshot from file, verifying integrity
  [[nodiscard]] static std::shared_ptr<DocumentSnapshot>
  load(const std::string &path, std::string *out_error = nullptr);

  /// Saves an autosave/recovery checkpoint with provenance
  [[nodiscard]] static bool write_recovery_checkpoint(
      const std::string &recovery_dir, const std::string &doc_id,
      const DocumentSnapshot &snapshot, std::string *out_error = nullptr);

  /// Finds the newest valid recovery checkpoint for a document
  [[nodiscard]] static std::optional<RecoveryInfo>
  find_latest_checkpoint(const std::string &recovery_dir,
                         const std::string &doc_id);

  /// Removes recovery checkpoints after clean save
  static void cleanup_checkpoints(const std::string &recovery_dir,
                                  const std::string &doc_id);

  /// Atomically stores an in-progress tool draft as a distinct bounded record.
  [[nodiscard]] static SaveResult atomic_write_recovery_draft(
      const std::string &path, const RecoveryDraftRecord &draft,
      const SaveOptions &options = {});

  /// Reads a draft record; it never applies the draft to a document.
  [[nodiscard]] static std::optional<RecoveryDraftRecord>
  load_recovery_draft(const std::string &path,
                      std::string *out_error = nullptr);
};

} // namespace pikdraw::document
