#include "byte_codec.hpp"
#include "file_io.hpp"
#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
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
    const auto bytes = NativeFormat::serialize(snapshot);
    const auto target = utf8_path(target_path);
    if (target.empty() || target.filename().empty())
      throw std::invalid_argument("Empty native save filename");
    temporary = utf8_path(target_path + temp_suffix());
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

std::shared_ptr<DocumentSnapshot>
SaveRecoveryService::load(const std::string &path, std::string *out_error) {
  if (out_error)
    out_error->clear();
  try {
    std::ifstream input(utf8_path(path), std::ios::binary | std::ios::ate);
    if (!input)
      throw std::runtime_error("Cannot open native file");
    const auto size = input.tellg();
    if (size < 36 || size > static_cast<std::streamoff>(max_file_bytes))
      throw std::length_error("Native file size exceeds allowed range");
    input.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!input.read(reinterpret_cast<char *>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size())))
      throw std::runtime_error("Native file read incomplete");
    return NativeFormat::deserialize(bytes.data(), bytes.size(), out_error);
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
                                            const std::string &document_id) {
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
      const auto snapshot = load(path_utf8(entry.path()));
      if (!snapshot || snapshot->document_id() != document_id)
        continue;
      const auto time = entry.last_write_time();
      if (!latest || time > latest_time ||
          (time == latest_time && filename > latest->timestamp)) {
        latest_time = time;
        latest = RecoveryInfo{path_utf8(entry.path()), snapshot->epoch(),
                              snapshot->revision(), filename};
      }
    }
    return latest;
  } catch (...) {
    return std::nullopt;
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
