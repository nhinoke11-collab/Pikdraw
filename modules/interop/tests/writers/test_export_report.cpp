#include "pikdraw/interop/export/export_report.hpp"

#include <cassert>
#include <iostream>

using namespace pikdraw::document;
using namespace pikdraw::interop;

namespace {

ExportJobReport complete_report() {
    ExportJobReport report;
    report.job_id = "job-1";
    report.document_id = "document-1";
    report.snapshot_revision = 8;
    report.adapter_id = "svg-static-v1";
    report.writer_build = "pikdraw-test-build";
    report.output_version = "1.1";
    report.profile_id = "design";
    report.status = ExportJobStatus::Complete;
    report.files.push_back({"C:/exports/drawing.svg", 128, std::string(64, 'a'), 8,
                            {7}, {42}, ExportFileStatus::Committed});
    return report;
}

void test_complete_receipt_keeps_claims_scoped() {
    auto report = complete_report();
    assert(validate_export_job_report(report));
    assert(report.appearance == ExportAppearanceStatus::NotAssessed);
    assert(report.structure == ExportStructureStatus::Unknown);
    assert(report.production == ExportProductionStatus::NotAssessed);
}

void test_artboard_receipt_requires_and_preserves_bounds() {
    auto report = complete_report();
    report.files.front().artboard_id = 9;
    report.files.front().bounds = pikdraw::geometry::Rect2D{25, 30, 100, 80};
    assert(validate_export_job_report(report));
    assert(report.files.front().artboard_id == 9);

    report.files.front().bounds.reset();
    const auto rejected = validate_export_job_report(report);
    assert(!rejected && rejected.error == ExportReportError::InvalidFileReceipt);
}

void test_complete_with_diagnostics_records_loss() {
    auto report = complete_report();
    report.status = ExportJobStatus::CompleteWithDiagnostics;
    report.appearance = ExportAppearanceStatus::Partial;
    report.diagnostics.push_back({"SVG-FONT-OUTLINED", ExportDiagnosticSeverity::Warning,
                                  7, 42, "Text was converted to outlines for this profile."});
    report.source_metadata.push_back({"font.family", ExportFieldStatus::Converted});
    assert(validate_export_job_report(report));
}

void test_rejects_missing_digest_and_revision_mixing() {
    auto report = complete_report();
    report.files.front().sha256_hex.clear();
    auto result = validate_export_job_report(report);
    assert(!result && result.error == ExportReportError::InvalidFileReceipt);

    report = complete_report();
    report.files.front().snapshot_revision = 9;
    result = validate_export_job_report(report);
    assert(!result && result.error == ExportReportError::InvalidFileReceipt);
}

void test_rejects_claims_without_evidence_and_false_complete_state() {
    auto report = complete_report();
    report.appearance = ExportAppearanceStatus::VerifiedForCatalog;
    auto result = validate_export_job_report(report);
    assert(!result && result.error == ExportReportError::InvalidCapabilityIdentity);

    report = complete_report();
    report.files.clear();
    result = validate_export_job_report(report);
    assert(!result && result.error == ExportReportError::InconsistentCompletion);

    report = complete_report();
    report.status = ExportJobStatus::CompleteWithDiagnostics;
    result = validate_export_job_report(report);
    assert(!result && result.error == ExportReportError::InconsistentCompletion);
}

} // namespace

int main() {
    test_complete_receipt_keeps_claims_scoped();
    test_artboard_receipt_requires_and_preserves_bounds();
    test_complete_with_diagnostics_records_loss();
    test_rejects_missing_digest_and_revision_mixing();
    test_rejects_claims_without_evidence_and_false_complete_state();
    std::cout << "All export report tests passed.\n";
    return 0;
}
