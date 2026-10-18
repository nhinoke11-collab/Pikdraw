#pragma once

#include "pikdraw/interop/export/atomic_export.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace pikdraw::interop::export_detail {

enum class AtomicFileOutcome {
    Committed,
    Cancelled,
    Rejected,
    ResourceLimit,
    Failed
};

struct AtomicFileResult {
    AtomicFileOutcome outcome{AtomicFileOutcome::Failed};
    uint64_t byte_size{0};
    std::string sha256_hex;
    std::string diagnostic_code;
    std::string message;
};

#ifdef _WIN32
[[nodiscard]] bool same_path_case_insensitive(const std::filesystem::path& left,
                                              const std::filesystem::path& right);
[[nodiscard]] bool output_extension_allowed(
    const std::string& target, const std::vector<std::string>& allowed_extensions);
[[nodiscard]] bool canonicalize_output_path(const std::string& utf8,
                                            std::filesystem::path& output,
                                            std::string& diagnostic);
[[nodiscard]] bool is_protected_source(
    const std::filesystem::path& output,
    const std::vector<std::string>& protected_sources,
    std::string& diagnostic);
[[nodiscard]] AtomicFileResult write_atomic_output(
    const ExportOutputTarget& target, const std::filesystem::path& output_path,
    const AtomicExportOptions& options, uint64_t cancellation_generation,
    size_t target_index, size_t target_count);
#endif

} // namespace pikdraw::interop::export_detail
