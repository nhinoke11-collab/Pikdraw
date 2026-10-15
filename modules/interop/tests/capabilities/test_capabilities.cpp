#include "pikdraw/interop/capabilities/format_capabilities.hpp"
#include <cassert>
#include <iostream>

using namespace pikdraw::interop;

void test_format_capabilities() {
    assert(FormatCapabilities::is_import_supported(FormatType::Svg));
    assert(FormatCapabilities::is_import_supported(FormatType::Png));
    assert(FormatCapabilities::is_import_supported(FormatType::Jpeg));

    // Proprietary formats without parser sandboxes must be unsupported
    assert(!FormatCapabilities::is_import_supported(FormatType::Cdr));
    assert(!FormatCapabilities::is_import_supported(FormatType::Ai));

    assert(FormatCapabilities::is_export_supported(FormatType::Svg));
    assert(FormatCapabilities::is_export_supported(FormatType::Pdf));
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
    test_fidelity_loss_report();
    std::cout << "[test_capabilities] All tests passed!\n";
    return 0;
}
