#pragma once

#include "../raster/path_raster.hpp"
#include <string>

namespace pikdraw::rendering::path_adapter {

/// Builds a bounded screen-space LOD from immutable authoring curves.
[[nodiscard]] bool flatten_authoring_path(
    const document::PathObject &object, const Camera2D &camera,
    std::vector<raster::Contour> &contours, std::string *out_error = nullptr);

} // namespace pikdraw::rendering::path_adapter
