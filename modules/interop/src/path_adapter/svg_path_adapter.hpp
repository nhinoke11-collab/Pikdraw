#pragma once

#include "pikdraw/document/model/editable_path.hpp"
#include <optional>
#include <string>

namespace pikdraw::interop::adapters {

/// Converts supported SVG path geometry without flattening curves.
class SvgPathAdapter {
public:
  [[nodiscard]] static std::optional<document::EditablePath> from_path2d(
      const geometry::Path2D &path, std::string *out_issue = nullptr);
  [[nodiscard]] static std::optional<std::string> to_path_data(
      const document::EditablePath &path, std::string *out_issue = nullptr);
};

} // namespace pikdraw::interop::adapters
