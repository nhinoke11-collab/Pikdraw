#pragma once

#include "pikdraw/interop/capabilities/format_capabilities.hpp"
#include "pikdraw/document/model/object.hpp"
#include <string>
#include <vector>

namespace pikdraw::interop {

struct SvgImportResult {
    bool success{false};
    document::Page page;
    std::vector<document::ObjectVariant> objects;
    FidelityLossReport report;
    std::string error_message;
};

/// Importer for the Milestone M1 SVG subset
class SvgImporter {
public:
    [[nodiscard]] static SvgImportResult import_from_string(const std::string& svg_xml);
    [[nodiscard]] static SvgImportResult import_from_file(const std::string& path);
};

} // namespace pikdraw::interop
