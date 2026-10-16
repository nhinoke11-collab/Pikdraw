#pragma once

#include "pikdraw/vector/tools/line/path_drawing_tool.hpp"

namespace pikdraw::vector::tools::line::detail {

struct PathObjectBuildResult {
    document::PathObject object;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error.empty();
    }
};

[[nodiscard]] PathObjectBuildResult build_path_object(
    PathDrawingKind kind, const std::vector<geometry::Point2D>& points,
    bool closed);

} // namespace pikdraw::vector::tools::line::detail
