#pragma once

#include "pikdraw/interop/adapters/svg_importer.hpp"

#include <cstddef>
#include <span>
#include <string_view>

namespace pikdraw::interop {

[[nodiscard]] bool ParseSvgNumber(std::string_view input, double& value, size_t& consumed);
[[nodiscard]] bool ParseSvgNumberList(std::string_view input, std::span<double> values);
[[nodiscard]] bool ParseSvgPath(std::string_view data, const geometry::Transform2D& transform,
                                geometry::Path2D& output, size_t maximum);

} // namespace pikdraw::interop
