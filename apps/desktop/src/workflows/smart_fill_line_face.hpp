#pragma once
#include "pikdraw/desktop/workflows/cut_geometry_job.hpp"
#include "pikdraw/geometry/curves/path.hpp"

namespace pikdraw::desktop::detail {
struct LineFaceResult {
    CutGeometryStatus status{CutGeometryStatus::Invalid};
    geometry::Path2D path;
    const char* message{"Could not resolve the enclosed region"};
};
LineFaceResult resolve_line_face(const std::vector<geometry::Path2D>& paths,
                                 geometry::Point2D point, std::stop_token stop);
}
