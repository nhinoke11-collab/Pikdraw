#pragma once

#include "pikdraw/interop/capabilities/format_capabilities.hpp"
#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include <string>

namespace pikdraw::interop {

struct SvgExportOptions {
    bool include_xml_header{true};
    bool pretty_print{true};
    double scale{1.0}; // 1.0 = mm directly in SVG user units
};

struct SvgExportResult {
    bool success{false};
    std::string svg;
    FidelityLossReport report;
    std::string error_message;
};

/// Exporter writing DocumentSnapshot to standard SVG XML format
class SvgExporter {
public:
    [[nodiscard]] static SvgExportResult export_with_report(
        const document::DocumentSnapshot& snapshot,
        const SvgExportOptions& options = SvgExportOptions{});

    [[nodiscard]] static std::string export_to_string(
        const document::DocumentSnapshot& snapshot,
        const SvgExportOptions& options = SvgExportOptions{});

    [[nodiscard]] static bool export_to_file(
        const std::string& path,
        const document::DocumentSnapshot& snapshot,
        const SvgExportOptions& options = SvgExportOptions{});
};

} // namespace pikdraw::interop
