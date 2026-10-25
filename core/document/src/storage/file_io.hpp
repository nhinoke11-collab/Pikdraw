#pragma once
#include "pikdraw/document/storage/save_recovery.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>

namespace pikdraw::document::storage_detail {
using StreamWriteCallback = std::function<void(std::span<const uint8_t>)>;
using StreamPatchCallback =
    std::function<void(uint64_t, std::span<const uint8_t>)>;
using StreamProducer = std::function<void(const StreamWriteCallback&,
                                          const StreamPatchCallback&)>;

inline std::filesystem::path utf8_path(const std::string &value) {
  return std::filesystem::path(std::u8string(value.begin(), value.end()));
}
inline std::string path_utf8(const std::filesystem::path &value) {
  const auto bytes = value.u8string();
  return {bytes.begin(), bytes.end()};
}
void write_and_flush_exclusive(const std::filesystem::path &path,
                               std::span<const uint8_t> bytes,
                               const SaveOptions &options, bool &created);
void write_and_flush_exclusive_stream(const std::filesystem::path &path,
                                      const StreamProducer& producer,
                                      const SaveOptions &options,
                                      bool &created);
void atomic_replace(const std::filesystem::path &temporary,
                    const std::filesystem::path &target);
void inject_failure(const SaveOptions &options, SaveFaultPoint stage);
} // namespace pikdraw::document::storage_detail
