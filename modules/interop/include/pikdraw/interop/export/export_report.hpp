#pragma once

#include "pikdraw/document/model/document_page.hpp"
#include "pikdraw/document/model/object_id.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pikdraw::interop {

enum class ExportJobStatus {
    Complete,
    CompleteWithDiagnostics,
    Rejected,
    Cancelled,
    Timeout,
    WorkerFailed,
    ResourceLimit,
    WriteFailed
};

enum class ExportAppearanceStatus {
    VerifiedForCatalog,
    Partial,
    Unknown,
    NotAssessed
};

enum class ExportStructureStatus {
    PreservedForCatalog,
    Converted,
    Flattened,
    Partial,
    Unknown,
    NotApplicable
};

enum class ExportProductionStatus {
    VerifiedForProfile,
    Blocked,
    NotAssessed
};

enum class ExportFieldStatus {
    Preserved,
    Converted,
    OmittedByChoice,
    Unsupported,
    Unknown
};

enum class ExportFileStatus {
    Committed,
    Failed,
    Skipped,
    Cancelled
};

enum class ExportDiagnosticSeverity {
    Info,
    Warning,
    Error
};

struct ExportFieldReceipt {
    std::string field;
    ExportFieldStatus status{ExportFieldStatus::Unknown};
};

struct ExportDiagnostic {
    std::string code;
    ExportDiagnosticSeverity severity{ExportDiagnosticSeverity::Warning};
    std::optional<document::PageId> page_id;
    std::optional<document::ObjectId> object_id;
    std::string message;
};

struct ExportFileReceipt {
    std::string path;
    uint64_t byte_size{0};
    std::string sha256_hex;
    uint64_t snapshot_revision{0};
    std::vector<document::PageId> page_ids;
    std::vector<document::ObjectId> object_ids;
    ExportFileStatus status{ExportFileStatus::Failed};
    std::optional<document::ArtboardId> artboard_id;
    std::optional<geometry::Rect2D> bounds;
};

/// Structured result for one job; paths are for the UI receipt and must be redacted in logs.
struct ExportJobReport {
    uint32_t schema_version{1};
    std::string job_id;
    std::string document_id;
    uint64_t snapshot_revision{0};
    std::string adapter_id;
    std::string writer_build;
    std::string output_version;
    std::string profile_id;
    uint32_t options_schema_version{1};
    ExportJobStatus status{ExportJobStatus::Rejected};
    ExportAppearanceStatus appearance{ExportAppearanceStatus::NotAssessed};
    std::string appearance_catalog_id;
    ExportStructureStatus structure{ExportStructureStatus::Unknown};
    std::string structure_catalog_id;
    ExportProductionStatus production{ExportProductionStatus::NotAssessed};
    std::string production_profile_id;
    std::vector<ExportFieldReceipt> source_metadata;
    std::vector<ExportFileReceipt> files;
    std::vector<ExportDiagnostic> diagnostics;
};

enum class ExportReportError {
    None,
    InvalidIdentity,
    InvalidCapabilityIdentity,
    InvalidStatus,
    InvalidDiagnostic,
    InvalidFileReceipt,
    InconsistentCompletion
};

struct ExportReportValidation {
    ExportReportError error{ExportReportError::None};
    std::string message;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == ExportReportError::None;
    }
};

[[nodiscard]] ExportReportValidation validate_export_job_report(
    const ExportJobReport& report);

} // namespace pikdraw::interop
