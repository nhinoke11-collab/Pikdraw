#include "svg_path_adapter.hpp"
#include "pikdraw/document/model/path_adapter.hpp"
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace pikdraw::interop::adapters {
std::optional<document::EditablePath> SvgPathAdapter::from_path2d(
    const geometry::Path2D &path, std::string *out_issue) {
  auto source = document::editable_path_from_path2d(path, out_issue);
  return source;
}

std::optional<std::string> SvgPathAdapter::to_path_data(
    const document::EditablePath &source, std::string *out_issue) {
  if (out_issue)
    out_issue->clear();
  try {
    const auto path = document::editable_path_to_path2d(source);
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (const auto &element : path.elements()) {
      switch (element.verb) {
      case geometry::PathVerb::MoveTo:
        stream << "M " << element.pt.x << ' ' << element.pt.y << ' ';
        break;
      case geometry::PathVerb::LineTo:
        stream << "L " << element.pt.x << ' ' << element.pt.y << ' ';
        break;
      case geometry::PathVerb::QuadTo:
        stream << "Q " << element.ctrl1.x << ' ' << element.ctrl1.y << ' '
               << element.pt.x << ' ' << element.pt.y << ' ';
        break;
      case geometry::PathVerb::CubicTo:
        stream << "C " << element.ctrl1.x << ' ' << element.ctrl1.y << ' '
               << element.ctrl2.x << ' ' << element.ctrl2.y << ' '
               << element.pt.x << ' ' << element.pt.y << ' ';
        break;
      case geometry::PathVerb::ArcTo:
        stream << "A " << element.rx << ' ' << element.ry << ' '
               << element.rotation_deg << ' ' << (element.large_arc ? 1 : 0)
               << ' ' << (element.sweep ? 1 : 0) << ' ' << element.pt.x << ' '
               << element.pt.y << ' ';
        break;
      case geometry::PathVerb::Close:
        stream << "Z ";
        break;
      }
    }
    return stream.str();
  } catch (const std::exception &error) {
    if (out_issue)
      *out_issue = error.what();
    return std::nullopt;
  }
}
} // namespace pikdraw::interop::adapters
