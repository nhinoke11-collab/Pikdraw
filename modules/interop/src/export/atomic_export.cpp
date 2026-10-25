#include "pikdraw/interop/export/atomic_export.hpp"
#include "atomic_export_file.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace pikdraw::interop {
namespace {

void add_diagnostic(ExportJobReport& report, std::string code,
                    ExportDiagnosticSeverity severity, std::string message) {
    report.diagnostics.push_back(
        {std::move(code), severity, std::nullopt, std::nullopt, std::move(message)});
}

ExportJobReport make_report(const ExportJob& job, const AtomicExportOptions& options) {
    ExportJobReport report;
    report.job_id = job.job_id();
    report.document_id = job.document_id();
    report.snapshot_revision = job.snapshot_revision();
    report.adapter_id = job.adapter_id();
    report.writer_build = options.writer_build.empty() ? "unavailable" : options.writer_build;
    report.output_version = job.output_version();
    report.profile_id = job.profile_id();
    report.options_schema_version = job.options_schema_version();
    report.appearance = ExportAppearanceStatus::NotAssessed;
    report.structure = ExportStructureStatus::Unknown;
    report.production = ExportProductionStatus::NotAssessed;
    report.files.reserve(job.output_targets().size());
    for (const auto& target : job.output_targets()) {
        ExportFileReceipt receipt{target.path, 0, {}, job.snapshot_revision(),
                                  target.page_ids, target.object_ids,
                                  ExportFileStatus::Skipped};
        receipt.artboard_id = target.artboard_id;
        receipt.bounds = target.bounds;
        report.files.push_back(std::move(receipt));
    }
    return report;
}

bool valid_extension_policy(const std::vector<std::string>& extensions) {
    if (extensions.empty() || extensions.size() > 16) return false;
    std::vector<std::string> normalized;
    normalized.reserve(extensions.size());
    for (const auto& extension : extensions) {
        if (extension.size() < 2 || extension.size() > 16 || extension.front() != '.')
            return false;
        std::string key;
        key.reserve(extension.size() - 1);
        for (size_t index = 1; index < extension.size(); ++index) {
            const unsigned char value = static_cast<unsigned char>(extension[index]);
            if (!((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
                  (value >= '0' && value <= '9'))) return false;
            key.push_back(static_cast<char>(std::tolower(value)));
        }
        if (std::find(normalized.begin(), normalized.end(), key) != normalized.end()) return false;
        normalized.push_back(std::move(key));
    }
    return true;
}

bool cancellation_requested(const AtomicExportOptions& options, uint64_t generation,
                            bool& callback_failed) {
    if (!options.is_cancelled) return false;
    try {
        return options.is_cancelled(generation);
    } catch (...) {
        callback_failed = true;
        return false;
    }
}

} // namespace

AtomicExportResult execute_atomic_export(const ExportJob& job,
                                         const AtomicExportOptions& options) {
    AtomicExportResult result;
    result.report = make_report(job, options);
    auto& report = result.report;

    if (options.writer_build.empty() || options.max_output_bytes_per_file == 0 ||
        !valid_extension_policy(options.allowed_output_extensions) ||
        !options.serialize || !options.validate || !options.publish_if_current ||
        job.output_targets().empty()) {
        report.status = ExportJobStatus::Rejected;
        add_diagnostic(report, "EXPORT_EXECUTOR_CONFIGURATION_INVALID",
                       ExportDiagnosticSeverity::Error,
                       "The writer, validator, build ID, byte budget, extension policy and publish gate are required.");
        result.report_validation = validate_export_job_report(report);
        return result;
    }

#ifdef _WIN32
    std::vector<std::filesystem::path> planned;
    planned.reserve(job.output_targets().size());
    std::string diagnostic;
    for (size_t index = 0; index < job.output_targets().size(); ++index) {
        const auto& requested = job.output_targets()[index].path;
        if (!export_detail::output_extension_allowed(requested,
                                                     options.allowed_output_extensions)) {
            report.files[index].status = ExportFileStatus::Failed;
            report.status = ExportJobStatus::Rejected;
            add_diagnostic(report, "EXPORT_OUTPUT_EXTENSION_MISMATCH",
                           ExportDiagnosticSeverity::Error,
                           "The output file extension does not match the selected writer profile.");
            result.report_validation = validate_export_job_report(report);
            return result;
        }
        std::filesystem::path target;
        if (!export_detail::canonicalize_output_path(requested, target, diagnostic)) {
            report.files[index].status = ExportFileStatus::Failed;
            report.status = ExportJobStatus::Rejected;
            add_diagnostic(report, "EXPORT_OUTPUT_PREFLIGHT_REJECTED",
                           ExportDiagnosticSeverity::Error, std::move(diagnostic));
            result.report_validation = validate_export_job_report(report);
            return result;
        }
        if (export_detail::is_protected_source(target, options.protected_source_paths,
                                              diagnostic)) {
            report.files[index].status = ExportFileStatus::Failed;
            report.status = ExportJobStatus::Rejected;
            add_diagnostic(report, "EXPORT_SOURCE_OVERWRITE_BLOCKED",
                           ExportDiagnosticSeverity::Error, std::move(diagnostic));
            result.report_validation = validate_export_job_report(report);
            return result;
        }
        std::error_code error;
        const bool exists = std::filesystem::exists(target, error);
        if (error || (exists && !options.allow_replace_existing)) {
            report.files[index].status = ExportFileStatus::Failed;
            report.status = ExportJobStatus::Rejected;
            add_diagnostic(report, exists ? "EXPORT_OVERWRITE_NOT_CONFIRMED" :
                                            "EXPORT_OUTPUT_PREFLIGHT_REJECTED",
                           ExportDiagnosticSeverity::Error,
                           exists ? "An output already exists and overwrite was not confirmed." :
                                    "The output target could not be inspected.");
            result.report_validation = validate_export_job_report(report);
            return result;
        }
        for (const auto& prior : planned) {
            if (export_detail::same_path_case_insensitive(prior, target)) {
                report.files[index].status = ExportFileStatus::Failed;
                report.status = ExportJobStatus::Rejected;
                add_diagnostic(report, "EXPORT_OUTPUT_TARGET_COLLISION",
                               ExportDiagnosticSeverity::Error,
                               "Two batch outputs resolve to the same destination.");
                result.report_validation = validate_export_job_report(report);
                return result;
            }
        }
        planned.push_back(std::move(target));
    }

    report.status = ExportJobStatus::Complete;
    for (size_t index = 0; index < planned.size(); ++index) {
        bool callback_failed = false;
        if (cancellation_requested(options, job.cancellation_generation(), callback_failed)) {
            for (size_t pending = index; pending < report.files.size(); ++pending)
                report.files[pending].status = ExportFileStatus::Cancelled;
            report.status = ExportJobStatus::Cancelled;
            add_diagnostic(report, "EXPORT_CANCELLED", ExportDiagnosticSeverity::Info,
                           "Export was cancelled before all outputs were committed.");
            break;
        }
        if (callback_failed) {
            report.files[index].status = ExportFileStatus::Failed;
            report.status = ExportJobStatus::WriteFailed;
            add_diagnostic(report, "EXPORT_CANCELLATION_CHECK_FAILED",
                           ExportDiagnosticSeverity::Error,
                           "The export cancellation state could not be checked.");
            break;
        }

        auto file = export_detail::write_atomic_output(
            job.output_targets()[index], planned[index], options,
            job.cancellation_generation(), index, planned.size());
        auto& receipt = report.files[index];
        if (file.outcome == export_detail::AtomicFileOutcome::Committed) {
            receipt.status = ExportFileStatus::Committed;
            receipt.byte_size = file.byte_size;
            receipt.sha256_hex = std::move(file.sha256_hex);
            receipt.path = job.output_targets()[index].path;
            continue;
        }
        if (file.outcome == export_detail::AtomicFileOutcome::Cancelled) {
            receipt.status = ExportFileStatus::Cancelled;
            for (size_t pending = index + 1; pending < report.files.size(); ++pending)
                report.files[pending].status = ExportFileStatus::Cancelled;
            report.status = ExportJobStatus::Cancelled;
            add_diagnostic(report, std::move(file.diagnostic_code),
                           ExportDiagnosticSeverity::Info, std::move(file.message));
            break;
        }

        receipt.status = ExportFileStatus::Failed;
        if (file.outcome == export_detail::AtomicFileOutcome::ResourceLimit)
            report.status = ExportJobStatus::ResourceLimit;
        else if (file.outcome == export_detail::AtomicFileOutcome::Rejected)
            report.status = ExportJobStatus::Rejected;
        else
            report.status = ExportJobStatus::WriteFailed;
        add_diagnostic(report, std::move(file.diagnostic_code),
                       ExportDiagnosticSeverity::Error, std::move(file.message));
        break;
    }
#else
    report.status = ExportJobStatus::Rejected;
    add_diagnostic(report, "EXPORT_PLATFORM_UNSUPPORTED", ExportDiagnosticSeverity::Error,
                   "Atomic export is available only in the supported Windows desktop build.");
#endif

    result.report_validation = validate_export_job_report(report);
    return result;
}

} // namespace pikdraw::interop
