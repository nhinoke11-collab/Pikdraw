#include "pikdraw/interop/export/export_report.hpp"

#include <cctype>
#include <unordered_set>

namespace pikdraw::interop {
namespace {

ExportReportValidation reject(ExportReportError error, const char* message) {
    return {error, message};
}

bool valid_job_status(ExportJobStatus status) noexcept {
    switch (status) {
        case ExportJobStatus::Complete:
        case ExportJobStatus::CompleteWithDiagnostics:
        case ExportJobStatus::Rejected:
        case ExportJobStatus::Cancelled:
        case ExportJobStatus::Timeout:
        case ExportJobStatus::WorkerFailed:
        case ExportJobStatus::ResourceLimit:
        case ExportJobStatus::WriteFailed:
            return true;
    }
    return false;
}

bool valid_field_status(ExportFieldStatus status) noexcept {
    switch (status) {
        case ExportFieldStatus::Preserved:
        case ExportFieldStatus::Converted:
        case ExportFieldStatus::OmittedByChoice:
        case ExportFieldStatus::Unsupported:
        case ExportFieldStatus::Unknown:
            return true;
    }
    return false;
}

bool valid_appearance_status(ExportAppearanceStatus status) noexcept {
    switch (status) {
        case ExportAppearanceStatus::VerifiedForCatalog:
        case ExportAppearanceStatus::Partial:
        case ExportAppearanceStatus::Unknown:
        case ExportAppearanceStatus::NotAssessed:
            return true;
    }
    return false;
}

bool valid_structure_status(ExportStructureStatus status) noexcept {
    switch (status) {
        case ExportStructureStatus::PreservedForCatalog:
        case ExportStructureStatus::Converted:
        case ExportStructureStatus::Flattened:
        case ExportStructureStatus::Partial:
        case ExportStructureStatus::Unknown:
        case ExportStructureStatus::NotApplicable:
            return true;
    }
    return false;
}

bool valid_production_status(ExportProductionStatus status) noexcept {
    switch (status) {
        case ExportProductionStatus::VerifiedForProfile:
        case ExportProductionStatus::Blocked:
        case ExportProductionStatus::NotAssessed:
            return true;
    }
    return false;
}

bool valid_file_status(ExportFileStatus status) noexcept {
    switch (status) {
        case ExportFileStatus::Committed:
        case ExportFileStatus::Failed:
        case ExportFileStatus::Skipped:
        case ExportFileStatus::Cancelled:
            return true;
    }
    return false;
}

bool valid_severity(ExportDiagnosticSeverity severity) noexcept {
    switch (severity) {
        case ExportDiagnosticSeverity::Info:
        case ExportDiagnosticSeverity::Warning:
        case ExportDiagnosticSeverity::Error:
            return true;
    }
    return false;
}

bool is_sha256_hex(const std::string& value) noexcept {
    if (value.size() != 64) return false;
    for (const unsigned char character : value)
        if (!std::isxdigit(character)) return false;
    return true;
}

template <typename Id>
bool has_unique_nonzero_ids(const std::vector<Id>& ids) {
    std::unordered_set<uint64_t> seen;
    for (const uint64_t id : ids)
        if (id == 0 || !seen.insert(id).second) return false;
    return true;
}

} // namespace

ExportReportValidation validate_export_job_report(const ExportJobReport& report) {
    if (report.schema_version == 0 || report.job_id.empty() || report.document_id.empty())
        return reject(ExportReportError::InvalidIdentity,
                      "The report schema, job ID and document ID are required.");
    if (report.adapter_id.empty() || report.writer_build.empty() ||
        report.output_version.empty() || report.profile_id.empty() ||
        report.options_schema_version == 0)
        return reject(ExportReportError::InvalidCapabilityIdentity,
                      "The writer build, format profile and options schema must be identified.");
    if (!valid_job_status(report.status) ||
        !valid_appearance_status(report.appearance) ||
        !valid_structure_status(report.structure) ||
        !valid_production_status(report.production))
        return reject(ExportReportError::InvalidStatus, "The report has an unknown job status.");
    if ((report.appearance == ExportAppearanceStatus::VerifiedForCatalog &&
         report.appearance_catalog_id.empty()) ||
        (report.structure == ExportStructureStatus::PreservedForCatalog &&
         report.structure_catalog_id.empty()) ||
        (report.production == ExportProductionStatus::VerifiedForProfile &&
         report.production_profile_id.empty()))
        return reject(ExportReportError::InvalidCapabilityIdentity,
                      "A verified axis must name the catalog or production profile that was checked.");

    std::unordered_set<std::string> metadata_fields;
    for (const auto& field : report.source_metadata) {
        if (field.field.empty() || !valid_field_status(field.status) ||
            !metadata_fields.insert(field.field).second)
            return reject(ExportReportError::InvalidCapabilityIdentity,
                          "Source metadata fields must be named, unique and have a known status.");
    }

    for (const auto& diagnostic : report.diagnostics) {
        if (diagnostic.code.empty() || !valid_severity(diagnostic.severity) ||
            diagnostic.message.empty() ||
            (diagnostic.page_id && *diagnostic.page_id == document::kInvalidPageId) ||
            (diagnostic.object_id && *diagnostic.object_id == document::kInvalidObjectId))
            return reject(ExportReportError::InvalidDiagnostic,
                          "Diagnostics require a stable code, severity and user-readable message.");
    }

    size_t committed_files = 0;
    for (const auto& file : report.files) {
        if (!valid_file_status(file.status) || file.path.empty() ||
            file.snapshot_revision != report.snapshot_revision ||
            !has_unique_nonzero_ids(file.page_ids) ||
            !has_unique_nonzero_ids(file.object_ids))
            return reject(ExportReportError::InvalidFileReceipt,
                          "File receipts need a path, unique scope IDs and the job snapshot revision.");
        if (file.status == ExportFileStatus::Committed) {
            if (file.byte_size == 0 || !is_sha256_hex(file.sha256_hex))
                return reject(ExportReportError::InvalidFileReceipt,
                              "Committed output needs a nonzero size and a SHA-256 checksum.");
            ++committed_files;
        } else if (!file.sha256_hex.empty()) {
            return reject(ExportReportError::InvalidFileReceipt,
                          "An uncommitted output cannot carry a final checksum.");
        }
    }

    if (report.status == ExportJobStatus::Complete) {
        if (report.files.empty() || committed_files != report.files.size() ||
            !report.diagnostics.empty())
            return reject(ExportReportError::InconsistentCompletion,
                          "Complete requires committed outputs and no diagnostics.");
    } else if (report.status == ExportJobStatus::CompleteWithDiagnostics) {
        if (committed_files == 0 || report.diagnostics.empty())
            return reject(ExportReportError::InconsistentCompletion,
                          "Completion with diagnostics requires at least one committed file and a diagnostic.");
    }
    return {};
}

} // namespace pikdraw::interop
