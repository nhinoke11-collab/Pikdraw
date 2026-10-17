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
    assert(!FormatCapabilities::is_import_supported(FormatType::Png));
    assert(!FormatCapabilities::is_import_supported(FormatType::Jpeg));
    assert(!FormatCapabilities::is_import_supported(FormatType::Pdf));
    assert(!FormatCapabilities::is_import_supported(FormatType::Cdr));
    assert(!FormatCapabilities::is_import_supported(FormatType::Ai));
    assert(!FormatCapabilities::is_import_supported(FormatType::Eps));

    assert(FormatCapabilities::is_worker_import_profile_supported(
        FormatType::Svg, "svg-hierarchy-v2"));
    assert(!FormatCapabilities::is_worker_import_profile_supported(
        FormatType::Dxf, "dxf-full-v1"));

    assert(FormatCapabilities::is_export_supported(FormatType::Svg));
    assert(!FormatCapabilities::is_export_supported(FormatType::Dxf));
    assert(!FormatCapabilities::is_export_supported(FormatType::Pdf));
    assert(!FormatCapabilities::is_export_supported(FormatType::Png));
    assert(!FormatCapabilities::is_export_supported(FormatType::Jpeg));
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

    for (const auto extension : {".pdf", ".ai", ".cdr", ".eps"}) {
        const auto entry = std::find_if(matrix.begin(), matrix.end(), [&](const auto &item) {
            return item.extension == extension;
        });
        assert(entry != matrix.end());
        assert(entry->import_status == FormatCapabilityStatus::UnsupportedReject);
        assert(entry->export_status == FormatCapabilityStatus::UnsupportedReject);
        assert(!FormatCapabilityRegistry::IsFormatSupported(extension));
    }
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
