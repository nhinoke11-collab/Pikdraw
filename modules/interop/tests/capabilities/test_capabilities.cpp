#include "pikdraw/interop/capabilities/format_capabilities.hpp"
#include "pikdraw/interop/format_capability_matrix.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>

using namespace pikdraw::interop;

void test_format_capabilities() {
    assert(FormatCapabilities::is_import_supported(FormatType::Svg));
    assert(FormatCapabilities::is_import_supported(FormatType::Dxf));
    assert(FormatCapabilities::is_worker_import_profile_supported(
        FormatType::Dxf, "dxf-entities-v1"));
    assert(FormatCapabilities::is_import_supported(FormatType::Png));
    assert(FormatCapabilities::is_import_supported(FormatType::Jpeg));
    assert(FormatCapabilities::is_import_supported(FormatType::Tiff));
    assert(FormatCapabilities::is_import_supported(FormatType::Pdf));
    assert(!FormatCapabilities::is_worker_import_profile_supported(
        FormatType::Pdf, "pdf-page-vector-v1"));
    assert(FormatCapabilities::is_worker_import_profile_supported(
        FormatType::Pdf, "pdf-page-vector-v2"));
    assert(FormatCapabilities::is_worker_import_profile_supported(
        FormatType::Pdf, "pdf-page-set-v3"));
    assert(FormatCapabilities::is_import_supported(FormatType::Cdr));
    assert(FormatCapabilities::is_worker_import_profile_supported(
        FormatType::Cdr, "cdr-libcdr-svg-v2"));
    assert(FormatCapabilities::is_import_supported(FormatType::Ai));
    assert(!FormatCapabilities::is_worker_import_profile_supported(
        FormatType::Ai, "ai-pdf-compatible-vector-v1"));
    assert(FormatCapabilities::is_worker_import_profile_supported(
        FormatType::Ai, "ai-pdf-compatible-vector-v2"));
    assert(FormatCapabilities::is_worker_import_profile_supported(
        FormatType::Ai, "ai-pdf-compatible-page-set-v3"));
    assert(FormatCapabilities::is_import_supported(FormatType::Eps));
    assert(FormatCapabilities::is_worker_import_profile_supported(
        FormatType::Eps, "eps-path-v1"));
    assert(!FormatCapabilities::is_worker_import_profile_supported(
        FormatType::Eps, "eps-full-v1"));

    assert(FormatCapabilities::is_worker_import_profile_supported(
        FormatType::Svg, "svg-hierarchy-v3"));
    assert(!FormatCapabilities::is_worker_import_profile_supported(
        FormatType::Cdr, "cdr-preview-v1"));
    assert(!FormatCapabilities::is_worker_import_profile_supported(
        FormatType::Pdf, "pdf-ai-preview-v1"));
    assert(!FormatCapabilities::is_worker_import_profile_supported(
        FormatType::Ai, "pdf-ai-preview-v1"));
    assert(FormatCapabilities::is_worker_import_profile_supported(
        FormatType::Png, "raster-rgba8-v1"));
    assert(FormatCapabilities::is_worker_import_profile_supported(
        FormatType::Jpeg, "raster-rgba8-v1"));
    assert(FormatCapabilities::is_worker_import_profile_supported(
        FormatType::Tiff, "raster-rgba8-v1"));
    assert(FormatCapabilities::is_worker_import_profile_supported(
        FormatType::Bmp, "raster-rgba8-v1"));
    assert(!FormatCapabilities::is_worker_import_profile_supported(
        FormatType::Dxf, "dxf-full-v1"));

    assert(FormatCapabilities::is_export_supported(FormatType::Svg));
    assert(!FormatCapabilities::is_export_supported(FormatType::Dxf));
    assert(!FormatCapabilities::is_export_supported(FormatType::Pdf));
    assert(!FormatCapabilities::is_export_supported(FormatType::Png));
    assert(!FormatCapabilities::is_export_supported(FormatType::Jpeg));
    assert(!FormatCapabilities::is_export_supported(FormatType::Tiff));
    assert(!FormatCapabilities::is_export_supported(FormatType::Bmp));
    assert(!FormatCapabilities::is_export_supported(FormatType::Cdr));
    assert(!FormatCapabilities::is_export_supported(FormatType::Ai));
    assert(!FormatCapabilities::is_export_supported(FormatType::Eps));
}

void test_capability_matrix_is_explicit_about_opacity_and_unsupported_formats() {
    const auto matrix = FormatCapabilityRegistry::GetMatrix();
    const auto svg = std::find_if(matrix.begin(), matrix.end(), [](const auto &entry) {
        return entry.extension == ".svg";
    });
    assert(svg != matrix.end());
    assert(svg->export_status == FormatCapabilityStatus::SupportedPrototype);
    assert(svg->notes.find("opacity") != std::string::npos);
    assert(svg->notes.find("effect nodes") != std::string::npos);
    assert(svg->notes.find("non-Normal blend") != std::string::npos);

    for (const auto extension : {".cdr"}) {
        const auto entry = std::find_if(matrix.begin(), matrix.end(), [&](const auto &item) {
            return item.extension == extension;
        });
        assert(entry != matrix.end());
        assert(entry->import_status == FormatCapabilityStatus::SupportedPrototype);
        assert(entry->export_status == FormatCapabilityStatus::UnsupportedReject);
        assert(FormatCapabilityRegistry::IsFormatSupported(extension));
    }
    const auto eps = std::find_if(matrix.begin(), matrix.end(), [](const auto& entry) {
        return entry.extension == ".eps";
    });
    assert(eps != matrix.end());
    assert(eps->import_status == FormatCapabilityStatus::SupportedPrototype);
    assert(eps->export_status == FormatCapabilityStatus::UnsupportedReject);
    assert(FormatCapabilityRegistry::IsFormatSupported(".eps"));
    assert(eps->notes.find("unsupported operators reject") != std::string::npos);
    for (const auto extension : {".pdf", ".ai"}) {
        const auto entry = std::find_if(matrix.begin(), matrix.end(), [&](const auto& item) {
            return item.extension == extension;
        });
        assert(entry != matrix.end());
        assert(entry->import_status == FormatCapabilityStatus::SupportedPrototype);
        assert(entry->export_status == FormatCapabilityStatus::UnsupportedReject);
        assert(FormatCapabilityRegistry::IsFormatSupported(extension));
        assert(entry->notes.find("editable") != std::string::npos);
    }
    const auto cdr_diagnostic = FormatCapabilityRegistry::GetDiagnosticForFormat(".cdr");
    assert(cdr_diagnostic.find("thumbnail") != std::string::npos);
    assert(cdr_diagnostic.find("ImageObjects") != std::string::npos);
    assert(FormatCapabilityRegistry::GetDiagnosticForFormat(".pdf").find("editable") != std::string::npos);
    for (const auto extension : {".png", ".jpg", ".tif"}) {
        const auto entry = std::find_if(matrix.begin(), matrix.end(), [&](const auto& item) {
            return item.extension == extension;
        });
        assert(entry != matrix.end());
        assert(entry->import_status == FormatCapabilityStatus::SupportedPrototype);
        assert(entry->export_status == FormatCapabilityStatus::UnsupportedReject);
        assert(entry->notes.find("RGBA8") != std::string::npos);
        assert(entry->notes.find("90% whole-system physical RAM") != std::string::npos);
        assert(entry->notes.find("4M pixels") == std::string::npos);
    }
    const auto png = std::find_if(matrix.begin(), matrix.end(), [](const auto& entry) {
        return entry.extension == ".png";
    });
    assert(png != matrix.end());
    assert(png->notes.find("tRNS transparency") != std::string::npos);
}

void test_fidelity_loss_report() {
    FidelityLossReport report;
    assert(report.is_clean());
    assert(!report.has_unsupported());

    report.add_issue(FidelitySeverity::Warning, "meshGradient", "Smooth Mesh", "Approximated via linear gradient");
    assert(!report.is_clean());
    assert(!report.has_unsupported());
    assert(report.issue_count() == 1);

    report.add_issue(FidelitySeverity::Unsupported, "feTurbulence", "SVG Filter", "Filters not supported in M1 baseline");
    assert(report.has_unsupported());
    assert(report.issue_count() == 2);
}

int main() {
    std::cout << "[test_capabilities] Running interop capabilities tests...\n";
    test_format_capabilities();
    test_capability_matrix_is_explicit_about_opacity_and_unsupported_formats();
    test_fidelity_loss_report();
    std::cout << "[test_capabilities] All tests passed!\n";
    return 0;
}
