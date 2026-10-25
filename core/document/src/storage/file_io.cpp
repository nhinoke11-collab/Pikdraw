#include "file_io.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace pikdraw::document::storage_detail {
void inject_failure(const SaveOptions &options, SaveFaultPoint stage) {
  if (options.fault == stage)
    throw std::runtime_error("Injected native save failure");
}

#ifdef _WIN32
namespace {
void win_error(const char *stage) {
  throw std::system_error(static_cast<int>(GetLastError()),
                          std::system_category(), stage);
}
struct FileHandle {
  HANDLE value;
  ~FileHandle() {
    if (value != INVALID_HANDLE_VALUE)
      CloseHandle(value);
  }
};
} // namespace
void write_and_flush_exclusive(const std::filesystem::path &path,
                               std::span<const uint8_t> bytes,
                               const SaveOptions &options, bool &created) {
  created = false;
  FileHandle file{CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
  if (file.value == INVALID_HANDLE_VALUE)
    win_error("Create exclusive native save temp");
  created = true;
  size_t offset = 0;
  while (offset < bytes.size()) {
    const auto count = static_cast<DWORD>(
        std::min<size_t>(1024 * 1024, bytes.size() - offset));
    DWORD written = 0;
    if (!WriteFile(file.value, bytes.data() + offset, count, &written, nullptr))
      win_error("Write native save temp");
    if (written == 0)
      throw std::runtime_error("Native save write made no progress");
    offset += written;
  }
  inject_failure(options, SaveFaultPoint::AfterWrite);
  inject_failure(options, SaveFaultPoint::BeforeFlush);
  if (!FlushFileBuffers(file.value))
    win_error("Flush native save temp");
  const auto handle = file.value;
  file.value = INVALID_HANDLE_VALUE;
  if (!CloseHandle(handle))
    win_error("Close native save temp");
}

void write_and_flush_exclusive_stream(const std::filesystem::path &path,
                                      const StreamProducer& producer,
                                      const SaveOptions &options,
                                      bool &created) {
  created = false;
  if (!producer)
    throw std::invalid_argument("Native stream producer is required");
  FileHandle file{CreateFileW(path.c_str(), GENERIC_WRITE | GENERIC_READ, 0,
                              nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                              nullptr)};
  if (file.value == INVALID_HANDLE_VALUE)
    win_error("Create exclusive native save temp");
  created = true;
  uint64_t write_offset = 0;
  const StreamWriteCallback write = [&](std::span<const uint8_t> bytes) {
    size_t offset = 0;
    while (offset < bytes.size()) {
      const auto count = static_cast<DWORD>(
          std::min<size_t>(1024 * 1024, bytes.size() - offset));
      DWORD written = 0;
      if (!WriteFile(file.value, bytes.data() + offset, count, &written,
                     nullptr))
        win_error("Write native save temp stream");
      if (written == 0)
        throw std::runtime_error("Native save stream made no progress");
      if (written > std::numeric_limits<uint64_t>::max() - write_offset)
        throw std::overflow_error("Native save stream offset overflow");
      offset += written;
      write_offset += written;
    }
  };
  const StreamPatchCallback patch = [&](uint64_t offset,
                                        std::span<const uint8_t> bytes) {
    if (offset > write_offset || bytes.size() > write_offset - offset ||
        offset > static_cast<uint64_t>(std::numeric_limits<LONGLONG>::max()) ||
        write_offset >
            static_cast<uint64_t>(std::numeric_limits<LONGLONG>::max()))
      throw std::out_of_range("Native save stream patch is outside written data");
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(file.value, position, nullptr, FILE_BEGIN))
      win_error("Seek native save temp patch");
    size_t patched = 0;
    while (patched < bytes.size()) {
      const auto count = static_cast<DWORD>(
          std::min<size_t>(1024 * 1024, bytes.size() - patched));
      DWORD written = 0;
      if (!WriteFile(file.value, bytes.data() + patched, count, &written,
                     nullptr))
        win_error("Patch native save temp");
      if (written == 0)
        throw std::runtime_error("Native save patch made no progress");
      patched += written;
    }
    position.QuadPart = static_cast<LONGLONG>(write_offset);
    if (!SetFilePointerEx(file.value, position, nullptr, FILE_BEGIN))
      win_error("Restore native save stream position");
  };
  producer(write, patch);
  inject_failure(options, SaveFaultPoint::AfterWrite);
  inject_failure(options, SaveFaultPoint::BeforeFlush);
  if (!FlushFileBuffers(file.value))
    win_error("Flush native save temp");
  const auto handle = file.value;
  file.value = INVALID_HANDLE_VALUE;
  if (!CloseHandle(handle))
    win_error("Close native save temp");
}

void atomic_replace(const std::filesystem::path &temporary,
                    const std::filesystem::path &target) {
  const auto attributes = GetFileAttributesW(target.c_str());
  if (attributes != INVALID_FILE_ATTRIBUTES) {
    if (!ReplaceFileW(target.c_str(), temporary.c_str(), nullptr,
                      REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr))
      win_error("Replace native file");
  } else {
    const auto error = GetLastError();
    if (error != ERROR_FILE_NOT_FOUND)
      win_error("Inspect native target");
    // No replace-existing flag: a newly created competing file must not be
    // overwritten.
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH))
      win_error("Publish new native file");
  }
}
#else
namespace {
struct FileDescriptor {
  int value;
  ~FileDescriptor() {
    if (value >= 0)
      ::close(value);
  }
};
} // namespace
void write_and_flush_exclusive(const std::filesystem::path &path,
                               std::span<const uint8_t> bytes,
                               const SaveOptions &options, bool &created) {
  created = false;
  FileDescriptor file{::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600)};
  if (file.value < 0)
    throw std::system_error(errno, std::generic_category(), "Create save temp");
  created = true;
  size_t offset = 0;
  while (offset < bytes.size()) {
    const auto written =
        ::write(file.value, bytes.data() + offset, bytes.size() - offset);
    if (written < 0 && errno == EINTR)
      continue;
    if (written <= 0)
      throw std::system_error(errno, std::generic_category(),
                              "Write save temp");
    offset += static_cast<size_t>(written);
  }
  inject_failure(options, SaveFaultPoint::AfterWrite);
  inject_failure(options, SaveFaultPoint::BeforeFlush);
  if (::fsync(file.value) != 0)
    throw std::system_error(errno, std::generic_category(), "Flush save temp");
}
void write_and_flush_exclusive_stream(const std::filesystem::path &path,
                                      const StreamProducer& producer,
                                      const SaveOptions &options,
                                      bool &created) {
  created = false;
  if (!producer)
    throw std::invalid_argument("Native stream producer is required");
  FileDescriptor file{::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600)};
  if (file.value < 0)
    throw std::system_error(errno, std::generic_category(), "Create save temp");
  created = true;
  uint64_t write_offset = 0;
  const StreamWriteCallback write = [&](std::span<const uint8_t> bytes) {
    size_t offset = 0;
    while (offset < bytes.size()) {
      const auto count = (std::min)(size_t{1024 * 1024}, bytes.size() - offset);
      const auto written = ::write(file.value, bytes.data() + offset, count);
      if (written < 0 && errno == EINTR)
        continue;
      if (written <= 0)
        throw std::system_error(errno, std::generic_category(),
                                "Write save temp stream");
      if (static_cast<uint64_t>(written) >
          std::numeric_limits<uint64_t>::max() - write_offset)
        throw std::overflow_error("Native save stream offset overflow");
      offset += static_cast<size_t>(written);
      write_offset += static_cast<uint64_t>(written);
    }
  };
  const StreamPatchCallback patch = [&](uint64_t offset,
                                        std::span<const uint8_t> bytes) {
    if (offset > write_offset || bytes.size() > write_offset - offset ||
        offset > static_cast<uint64_t>(std::numeric_limits<off_t>::max()))
      throw std::out_of_range("Native save stream patch is outside written data");
    size_t patched = 0;
    while (patched < bytes.size()) {
      const auto current = offset + patched;
      if (current > static_cast<uint64_t>(std::numeric_limits<off_t>::max()))
        throw std::out_of_range("Native save stream patch offset is too large");
      const auto count = (std::min)(size_t{1024 * 1024}, bytes.size() - patched);
      const auto written = ::pwrite(file.value, bytes.data() + patched, count,
                                    static_cast<off_t>(current));
      if (written < 0 && errno == EINTR)
        continue;
      if (written <= 0)
        throw std::system_error(errno, std::generic_category(),
                                "Patch save temp stream");
      patched += static_cast<size_t>(written);
    }
  };
  producer(write, patch);
  inject_failure(options, SaveFaultPoint::AfterWrite);
  inject_failure(options, SaveFaultPoint::BeforeFlush);
  if (::fsync(file.value) != 0)
    throw std::system_error(errno, std::generic_category(), "Flush save temp");
}
void atomic_replace(const std::filesystem::path &temporary,
                    const std::filesystem::path &target) {
  std::filesystem::rename(temporary, target);
  const auto parent = target.parent_path().empty() ? std::filesystem::path(".")
                                                   : target.parent_path();
  FileDescriptor directory{::open(parent.c_str(), O_RDONLY | O_DIRECTORY)};
  if (directory.value < 0 || ::fsync(directory.value) != 0)
    throw std::system_error(errno, std::generic_category(),
                            "Sync renamed native directory");
}
#endif
} // namespace pikdraw::document::storage_detail
