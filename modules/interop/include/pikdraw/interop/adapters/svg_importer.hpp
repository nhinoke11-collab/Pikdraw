#pragma once

#include "pikdraw/interop/capabilities/format_capabilities.hpp"
#include "pikdraw/document/model/object.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <limits>

namespace pikdraw::interop {

struct SvgImportResult {
    bool success{false};
    document::Page page;
    std::vector<document::ObjectVariant> objects;
    std::vector<std::string> source_ids;
    std::vector<std::string> layer_source_ids;
    FidelityLossReport report;
    std::string error_message;
};

/// Bounded SVG 1.1 static subset. Unsupported constructs reject the whole document.
class SvgImporter {
public:
    static constexpr size_t kMaxSourceBytes = std::numeric_limits<size_t>::max();
    static constexpr size_t kMaxObjects = 10000;
    static constexpr size_t kMaxPathElements = 6000;
    static constexpr double kMaxPageMillimeters = 1000000.0;

    [[nodiscard]] static SvgImportResult import_from_string(std::string_view svg_xml);
    [[nodiscard]] static SvgImportResult import_from_file(const std::string& path);
};

} // namespace pikdraw::interop
