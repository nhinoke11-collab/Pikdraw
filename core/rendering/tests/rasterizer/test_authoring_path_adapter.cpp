#include "../../src/path_adapter/authoring_path_adapter.hpp"
#include "pikdraw/document/model/path_adapter.hpp"
#include <stdexcept>

using namespace pikdraw;

int main() {
  document::PathObject object;
  document::EditablePath source;
  document::EditableSubpath subpath;
  subpath.id = document::SubpathId{81001};
  subpath.nodes = {document::PathNode{document::NodeId{81002}, {100, 0}},
                   document::PathNode{document::NodeId{81003}, {0, 100}}};
  document::EditableSegment segment;
  segment.id = document::SegmentId{81004};
  segment.kind = document::PathSegmentKind::EllipticArc;
  segment.arc = {{0, 0}, {100, 0}, {0, 100}, 0, 1.57079632679489661923};
  subpath.segments.push_back(segment);
  source.subpaths.push_back(std::move(subpath));
  object.set_authoring_source(std::move(source));

  rendering::Camera2D camera;
  camera.set_center({50, 50});
  camera.set_zoom(1.0);
  std::vector<rendering::raster::Contour> contours;
  std::string error;
  if (!rendering::path_adapter::flatten_authoring_path(object, camera,
                                                        contours, &error))
    throw std::runtime_error(error);
  if (contours.size() != 1 || contours.front().local.size() < 8 ||
      contours.front().closed)
    throw std::runtime_error("analytic arc did not produce a screen-space LOD");

  auto stale = object;
  stale.path.line_to({5, 5});
  if (rendering::path_adapter::flatten_authoring_path(stale, camera, contours,
                                                       &error) ||
      error.empty())
    throw std::runtime_error("stale renderer adapter was accepted");
}
