#pragma once

#include "pikdraw/interop/capabilities/format_capabilities.hpp"
#include "pikdraw/interop/export/export_report.hpp"

#include <string>
#include <vector>

namespace pikdraw::desktop::detail {

struct SvgPageFidelity {
    document::PageId page_id{document::kInvalidPageId};
    std::string page_name;
    std::vector<interop::FidelityIssue> issues;
};

void append_svg_fidelity_diagnostics(
    interop::ExportJobReport& report,
    const std::vector<SvgPageFidelity>& pages);

} // namespace pikdraw::desktop::detail
