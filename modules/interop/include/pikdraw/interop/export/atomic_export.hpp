#pragma once

#include "pikdraw/interop/export/export_job.hpp"
#include "pikdraw/interop/export/export_report.hpp"

#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

namespace pikdraw::interop {

using ExportSerializeCallback = std::function<bool(
    const ExportOutputTarget&, std::ostream&, std::string& diagnostic)>;
using ExportValidateCallback = std::function<bool(
    const ExportOutputTarget&, std::istream& staged_output, uint64_t byte_size,
    std::string& diagnostic)>;
using ExportCancellationCallback = std::function<bool(uint64_t generation)>;
using ExportPublishGateCallback = std::function<bool(
    uint64_t generation, const std::function<bool()>& publish)>;
using ExportProgressCallback = std::function<void(
    size_t target_index, size_t target_count, uint64_t bytes_written,
    uint64_t byte_limit)>;

struct AtomicExportOptions {
    std::string writer_build;
    uint64_t max_output_bytes_per_file{0};
    std::vector<std::string> allowed_output_extensions;
    bool allow_replace_existing{false};
    std::vector<std::string> protected_source_paths;
    ExportSerializeCallback serialize;
    ExportValidateCallback validate;
    ExportCancellationCallback is_cancelled;
    /// Serialize cancellation/replacement against the final rename. Invoke publish only
    /// while this generation is still current, holding the host's job-state gate.
    ExportPublishGateCallback publish_if_current;
    /// Called on the export thread in bounded byte increments; implementations should post
    /// progress to the UI queue and return quickly.
    ExportProgressCallback on_progress;
};

struct AtomicExportResult {
    ExportJobReport report;
    ExportReportValidation report_validation;

    [[nodiscard]] bool has_valid_report() const noexcept {
        return static_cast<bool>(report_validation);
    }
};

/// Stream each output to an exclusive same-directory temp file, validate its read-only
/// staged stream, hash it, then atomically publish that individual output. Earlier successful
/// batch members stay committed if a later member fails or the job is cancelled.
[[nodiscard]] AtomicExportResult execute_atomic_export(
    const ExportJob& job, const AtomicExportOptions& options);

} // namespace pikdraw::interop
