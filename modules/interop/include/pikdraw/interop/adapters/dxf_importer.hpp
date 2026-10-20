#pragma once

#include "pikdraw/document/model/object.hpp"
#include "pikdraw/interop/capabilities/format_capabilities.hpp"

#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace pikdraw::interop {

struct DxfImportResult {
  bool success{false};
  document::Page page;
  std::vector<document::ObjectVariant> objects;
  std::vector<std::string> source_ids;
  std::vector<std::string> layer_source_ids;
  FidelityLossReport report;
  std::string error_message;
};

/// Fail-closed ASCII DXF 2D profile. Unsupported source data rejects the whole
/// drawing.
class DxfImporter final {
public:
  static constexpr size_t kMaxSourceBytes = std::numeric_limits<size_t>::max();
  static constexpr size_t kMaxPairs = 500000;
  static constexpr size_t kMaxObjects = 10000;
  static constexpr size_t kMaxPathElements = 6000;
  static constexpr double kMaxCoordinateMillimeters = 1000000000.0;
  static constexpr double kMaxPageMillimeters = 1000000.0;

  [[nodiscard]] static DxfImportResult
  import_from_string(std::string_view source);
};

} // namespace pikdraw::interop
