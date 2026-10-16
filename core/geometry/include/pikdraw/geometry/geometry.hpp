#pragma once

#include "pikdraw/geometry/primitives/point.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"
#include "pikdraw/geometry/primitives/transform.hpp"
#include "pikdraw/geometry/primitives/validation.hpp"
#include "pikdraw/geometry/curves/curve.hpp"
#include "pikdraw/geometry/curves/path.hpp"
#include "pikdraw/geometry/flattening/flattening.hpp"
#include "pikdraw/geometry/operations/boolean.hpp"
#include "pikdraw/geometry/operations/offset.hpp"
#include "pikdraw/geometry/operations/align.hpp"

namespace pikdraw::geometry {

inline constexpr double kMaxCanvasDimensionMm = 40000.0; // 40 meters (40,000 mm) canvas workspace limit
inline constexpr double kMaxObjectDimensionMm = 40000.0; // 40 meters (40,000 mm) max object dimension

} // namespace pikdraw::geometry
