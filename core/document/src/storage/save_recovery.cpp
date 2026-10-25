#include "byte_codec.hpp"
#include "file_io.hpp"
#include "resource_budget.hpp"
#include <atomic>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace pikdraw::document {
using namespace storage_detail;
namespace {
std::atomic<uint64_t> next_temp{0};
constexpr size_t kRecoveryCandidateLimit = 128;
constexpr size_t kRecoveryScanEntryLimit = 4096;

std::string recovery_timestamp(std::filesystem::file_time_type file_time) {
  const auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
      std::chrono::file_clock::to_sys(file_time));
  const auto raw = std::chrono::system_clock::to_time_t(system_time);
  std::tm local{};
#ifdef _WIN32
  if (localtime_s(&local, &raw) != 0)
    return {};
#else
  if (!localtime_r(&raw, &local))
    return {};
#endif
  std::ostringstream formatted;
  formatted << std::put_time(&local, "%Y-%m-%d %H:%M");
  return formatted.str();
}

RecoveryInfo describe_checkpoint(const std::filesystem::path &path,
                                const DocumentSnapshot &snapshot,
                                std::filesystem::file_time_type modified) {
  return RecoveryInfo{path_utf8(path), snapshot.document_id(), snapshot.epoch(),
                      snapshot.revision(), snapshot.object_count(),
                      snapshot.page().width_mm, snapshot.page().height_mm,
                      std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                          std::chrono::file_clock::to_sys(modified)),
                      recovery_timestamp(modified)};
}

std::string temp_suffix() {
#ifdef _WIN32
  const auto pid = GetCurrentProcessId();
#else
  const auto pid = ::getpid();
#endif
  return ".tmp." + std::to_string(pid) + "." +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()) +
         "." + std::to_string(++next_temp);
}

std::string recovery_prefix(const std::string &document_id) {
  if (document_id.empty() || document_id.size() > 512)
    throw std::invalid_argument("Invalid recovery document ID");
  // The filename is only a bounded lookup hint. The serialized document ID
  // remains authoritative and is checked before returning or deleting a file.
  uint64_t hash = 14695981039346656037ull;
  for (const auto byte : document_id)
    hash = (hash ^ static_cast<uint8_t>(byte)) * 1099511628211ull;
  constexpr char digits[] = "0123456789abcdef";
  std::string result = "recovery_0000000000000000_";
  for (int index = 0; index < 16; ++index) {
    const auto shift = static_cast<unsigned>((15 - index) * 4);
    result[9 + index] = digits[(hash >> shift) & 0xfu];
  }
  return result;
}
} // namespace

SaveResult SaveRecoveryService::atomic_save(const std::string &target_path,
                                            const DocumentSnapshot &snapshot,
                                            const SaveOptions &options) {
  SaveResult result;
  result.path = target_path;
  result.saved_epoch = snapshot.epoch();
  result.saved_revision = snapshot.revision();
  std::filesystem::path temporary;
  bool created = false;
  try {
    const auto target = utf8_path(target_path);
    if (target.empty() || target.filename().empty())
      throw std::invalid_argument("Empty native save filename");
    temporary = utf8_path(target_path + temp_suffix());
    write_and_flush_exclusive_stream(temporary,
        [&snapshot](const StreamWriteCallback& write,
                    const StreamPatchCallback& patch) {
          NativeFormat::serialize_to(snapshot, write, patch);
        }, options, created);
    inject_failure(options, SaveFaultPoint::BeforeReplace);
    atomic_replace(temporary, target);
    result.success = true;
  } catch (const std::exception &error) {
    result.error_message = error.what();
    if (created) {
      std::error_code cleanup_error;
      std::filesystem::remove(temporary, cleanup_error);
      if (cleanup_error)
        result.error_message +=
            "; temporary cleanup failed: " + cleanup_error.message();
    }
  }
  return result;
}

std::shared_ptr<DocumentSnapshot>
SaveRecoveryService::load(const std::string &path, std::string *out_error,
                          const std::atomic_bool *cancel_requested) {
  if (out_error)
    out_error->clear();
  try {
    if (cancel_requested &&
        cancel_requested->load(std::memory_order_acquire))
      throw std::runtime_error("Native load cancelled");
    std::ifstream input(utf8_path(path), std::ios::binary | std::ios::ate);
    if (!input)
      throw std::runtime_error("Cannot open native file");
    const auto size = input.tellg();
    if (size < 36)
      throw std::length_error("Native file is shorter than its header");
    const auto size_bytes = static_cast<uint64_t>(
        static_cast<std::streamoff>(size));
    const auto read_budget = native_read_memory_budget_bytes();
    if (read_budget == 0 || size_bytes > read_budget ||
        size_bytes > std::numeric_limits<size_t>::max() ||
        size_bytes > static_cast<uint64_t>(
            std::numeric_limits<std::streamsize>::max()))
      throw std::length_error(
          "Native file exceeds the current available-memory quota");
    input.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size_bytes));
    constexpr size_t kReadChunkBytes = 1024u * 1024u;
    for (size_t offset = 0; offset < bytes.size();) {
      if (cancel_requested &&
          cancel_requested->load(std::memory_order_acquire))
        throw std::runtime_error("Native load cancelled");
      const auto count = std::min(kReadChunkBytes, bytes.size() - offset);
      if (!input.read(reinterpret_cast<char *>(bytes.data() + offset),
                      static_cast<std::streamsize>(count)))
        throw std::runtime_error("Native file read incomplete");
      offset += count;
    }
    return NativeFormat::deserialize(bytes.data(), bytes.size(), out_error,
                                     cancel_requested);
  } catch (const std::exception &error) {
    if (out_error)
      *out_error = error.what();
    return nullptr;
  }
}

bool SaveRecoveryService::write_recovery_checkpoint(
    const std::string &recovery_dir, const std::string &document_id,
    const DocumentSnapshot &snapshot, std::string *out_error) {
  if (out_error)
    out_error->clear();
  try {
    if (snapshot.document_id() != document_id)
      throw std::invalid_argument(
          "Recovery document ID does not match snapshot");
    const auto prefix = recovery_prefix(document_id);
    const auto directory = utf8_path(recovery_dir);
    std::filesystem::create_directories(directory);
    const auto time =
        std::chrono::system_clock::now().time_since_epoch().count();
    const auto filename = prefix + std::to_string(time) + "_" +
                          std::to_string(++next_temp) + ".pkdr";
    const auto saved =
        atomic_save(path_utf8(directory / utf8_path(filename)), snapshot);
    if (out_error && !saved.success)
      *out_error = saved.error_message;
    return saved.success;
  } catch (const std::exception &error) {
    if (out_error)
      *out_error = error.what();
    return false;
  } catch (...) {
    if (out_error)
      *out_error = "Unknown recovery checkpoint failure";
    return false;
  }
}

std::optional<RecoveryInfo>
SaveRecoveryService::find_latest_checkpoint(const std::string &recovery_dir,
                                            const std::string &document_id,
                                            const std::atomic_bool *cancel_requested) {
  try {
    const auto prefix = recovery_prefix(document_id);
    const auto directory = utf8_path(recovery_dir);
    if (!std::filesystem::is_directory(directory))
      return std::nullopt;
    std::optional<RecoveryInfo> latest;
    std::filesystem::file_time_type latest_time{};
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
      if (entry.is_symlink() || !entry.is_regular_file())
        continue;
      const auto filename = path_utf8(entry.path().filename());
      if (!filename.starts_with(prefix) || entry.path().extension() != ".pkdr")
        continue;
      const auto snapshot = load(path_utf8(entry.path()), nullptr,
                                 cancel_requested);
      if (cancel_requested &&
          cancel_requested->load(std::memory_order_acquire)) {
        return std::nullopt;
      }
      if (!snapshot || snapshot->document_id() != document_id)
        continue;
      const auto time = entry.last_write_time();
      if (!latest || time > latest_time ||
          (time == latest_time && filename >
              path_utf8(utf8_path(latest->path).filename()))) {
        latest_time = time;
        latest = describe_checkpoint(entry.path(), *snapshot, time);
      }
    }
    return latest;
  } catch (...) {
    return std::nullopt;
  }
}

std::vector<RecoveryInfo> SaveRecoveryService::list_recovery_checkpoints(
    const std::string &recovery_root, bool *out_truncated,
    const std::atomic_bool *cancel_requested) {
  if (out_truncated)
    *out_truncated = false;
  std::vector<RecoveryInfo> candidates;
  if (cancel_requested && cancel_requested->load(std::memory_order_acquire)) {
    if (out_truncated)
      *out_truncated = true;
    return candidates;
  }
  try {
    const auto root = utf8_path(recovery_root);
    const auto root_status = std::filesystem::symlink_status(root);
    if (std::filesystem::is_symlink(root_status) ||
        !std::filesystem::is_directory(root_status))
      return candidates;

    size_t scanned = 0;
    bool scan_limit_reached = false;
    bool scan_cancelled = false;
    const auto inspect_file = [&](const std::filesystem::directory_entry &entry) {
      std::error_code error;
      const auto status = entry.symlink_status(error);
      if (error || std::filesystem::is_symlink(status) ||
          !std::filesystem::is_regular_file(status) ||
          entry.path().extension() != ".pkdr")
        return;
      const auto snapshot = load(path_utf8(entry.path()), nullptr,
                                 cancel_requested);
      if (cancel_requested &&
          cancel_requested->load(std::memory_order_acquire)) {
        scan_cancelled = true;
        return;
      }
      if (!snapshot || snapshot->document_id().empty())
        return;
      const auto modified = entry.last_write_time(error);
      if (error)
        return;
      candidates.push_back(describe_checkpoint(entry.path(), *snapshot, modified));
    };

    std::error_code error;
    for (std::filesystem::directory_iterator it(
             root, std::filesystem::directory_options::skip_permission_denied,
             error), end;
         !error && it != end; it.increment(error)) {
      if (cancel_requested && cancel_requested->load(std::memory_order_acquire)) {
        scan_cancelled = true;
        break;
      }
      if (scanned >= kRecoveryScanEntryLimit) {
        scan_limit_reached = true;
        break;
      }
      ++scanned;
      const auto entry = *it;
      const auto status = entry.symlink_status(error);
      if (error) {
        error.clear();
        continue;
      }
      if (std::filesystem::is_symlink(status))
        continue;
      if (std::filesystem::is_regular_file(status)) {
        inspect_file(entry);
      } else if (std::filesystem::is_directory(status)) {
        std::error_code child_error;
        for (std::filesystem::directory_iterator child(
                 entry.path(),
                 std::filesystem::directory_options::skip_permission_denied,
                 child_error), child_end;
             !child_error && child != child_end; child.increment(child_error)) {
          if (cancel_requested && cancel_requested->load(std::memory_order_acquire)) {
            scan_cancelled = true;
            break;
          }
          if (scanned >= kRecoveryScanEntryLimit) {
            scan_limit_reached = true;
            break;
          }
          ++scanned;
          inspect_file(*child);
          if (scan_limit_reached)
            break;
        }
      }
      if (scan_limit_reached || scan_cancelled)
        break;
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const RecoveryInfo &left, const RecoveryInfo &right) {
                if (left.modified_at != right.modified_at)
                  return left.modified_at > right.modified_at;
                return left.path > right.path;
              });
    const bool truncated = scan_limit_reached || scan_cancelled ||
                           candidates.size() > kRecoveryCandidateLimit;
    if (scan_cancelled)
      candidates.clear();
    if (candidates.size() > kRecoveryCandidateLimit)
      candidates.resize(kRecoveryCandidateLimit);
    if (out_truncated)
      *out_truncated = truncated;
  } catch (...) {
    candidates.clear();
  }
  return candidates;
}

bool SaveRecoveryService::discard_recovery_checkpoint(
    const std::string &recovery_root, const std::string &checkpoint_path,
    std::string *out_error) {
  if (out_error)
    out_error->clear();
  try {
    const auto root = std::filesystem::absolute(utf8_path(recovery_root)).lexically_normal();
    const auto target = std::filesystem::absolute(utf8_path(checkpoint_path)).lexically_normal();
    const auto relative = target.lexically_relative(root);
    if (relative.empty() || relative.is_absolute() || relative.extension() != ".pkdr" ||
        std::any_of(relative.begin(), relative.end(), [](const auto &part) {
          return part == "..";
        }))
      throw std::invalid_argument("Checkpoint is outside the recovery folder");
    const auto component_count = static_cast<size_t>(
        std::distance(relative.begin(), relative.end()));
    if (component_count < 1 || component_count > 2)
      throw std::invalid_argument("Checkpoint path is not a recovery entry");

    const auto root_status = std::filesystem::symlink_status(root);
    const auto target_status = std::filesystem::symlink_status(target);
    if (std::filesystem::is_symlink(root_status) ||
        std::filesystem::is_symlink(target_status) ||
        !std::filesystem::is_regular_file(target_status))
      throw std::invalid_argument("Checkpoint is not a regular recovery file");
    if (component_count == 2) {
      const auto folder_status = std::filesystem::symlink_status(target.parent_path());
      if (std::filesystem::is_symlink(folder_status) ||
          !std::filesystem::is_directory(folder_status))
        throw std::invalid_argument("Checkpoint folder is not valid");
    }
    const auto snapshot = load(path_utf8(target));
    if (!snapshot || snapshot->document_id().empty())
      throw std::invalid_argument("Checkpoint is no longer valid");
    if (!std::filesystem::remove(target))
      throw std::runtime_error("Checkpoint could not be removed");
    return true;
  } catch (const std::exception &error) {
    if (out_error)
      *out_error = error.what();
    return false;
  } catch (...) {
    if (out_error)
      *out_error = "Unknown recovery checkpoint removal failure";
    return false;
  }
}

void SaveRecoveryService::cleanup_checkpoints(const std::string &recovery_dir,
                                              const std::string &document_id) {
  try {
    const auto prefix = recovery_prefix(document_id);
    const auto directory = utf8_path(recovery_dir);
    if (!std::filesystem::is_directory(directory))
      return;
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
      if (entry.is_symlink() || !entry.is_regular_file())
        continue;
      if (!path_utf8(entry.path().filename()).starts_with(prefix) ||
          entry.path().extension() != ".pkdr")
        continue;
      const auto snapshot = load(path_utf8(entry.path()));
      if (!snapshot || snapshot->document_id() != document_id)
        continue;
      std::error_code error;
      std::filesystem::remove(entry.path(), error);
    }
  } catch (...) {
  }
}

SaveResult SaveRecoveryService::atomic_write_recovery_draft(
    const std::string &path, const RecoveryDraftRecord &draft,
    const SaveOptions &options) {
  SaveResult result;
  result.path = path;
  result.saved_epoch = draft.document_epoch;
  result.saved_revision = draft.base_revision;
  std::filesystem::path temporary;
  bool created = false;
  try {
    const auto bytes = RecoveryDraftFormat::serialize(draft);
    const auto target = utf8_path(path);
    if (target.empty() || target.filename().empty())
      throw std::invalid_argument("Empty recovery draft filename");
    temporary = utf8_path(path + temp_suffix());
    write_and_flush_exclusive(temporary, bytes, options, created);
    inject_failure(options, SaveFaultPoint::BeforeReplace);
    atomic_replace(temporary, target);
    result.success = true;
  } catch (const std::exception &error) {
    result.error_message = error.what();
    if (created) {
      std::error_code cleanup_error;
      std::filesystem::remove(temporary, cleanup_error);
      if (cleanup_error)
        result.error_message +=
            "; temporary cleanup failed: " + cleanup_error.message();
    }
  }
  return result;
}

std::optional<RecoveryDraftRecord> SaveRecoveryService::load_recovery_draft(
    const std::string &path, std::string *out_error) {
  if (out_error)
    out_error->clear();
  try {
    std::ifstream input(utf8_path(path), std::ios::binary | std::ios::ate);
    if (!input)
      throw std::runtime_error("Cannot open recovery draft");
    const auto size = input.tellg();
    constexpr auto max_size = static_cast<std::streamoff>(
        kRecoveryDraftPayloadLimit + 8 + 4 + 4 + 4 + 512 + 8 + 8 + 4 + 4 + 4);
    if (size < 49 || size > max_size)
      throw std::length_error("Recovery draft size exceeds its allowed range");
    input.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!input.read(reinterpret_cast<char *>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size())))
      throw std::runtime_error("Recovery draft read incomplete");
    auto draft = RecoveryDraftFormat::deserialize(bytes.data(), bytes.size(),
                                                   out_error);
    if (!draft && out_error && out_error->empty())
      *out_error = "Invalid recovery draft";
    return draft;
  } catch (const std::exception &error) {
    if (out_error)
      *out_error = error.what();
    return std::nullopt;
  }
}
} // namespace pikdraw::document
