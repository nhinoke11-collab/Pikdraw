#pragma once

#include "pikdraw/document/annotations/dimension_object.hpp"
#include "pikdraw/document/snapshots/document_snapshot.hpp"
#include "pikdraw/geometry/curves/path.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"

#include <string>
#include <vector>

namespace pikdraw::document {

class Document;

struct DimensionLayout {
    std::vector<geometry::Path2D> lines;
    std::vector<geometry::Path2D> arrows;
    std::string formatted_text;
    geometry::Point2D text_position;
    double text_rotation_rad{0.0};
    geometry::Rect2D text_bounds;
    geometry::Rect2D overall_bounds;
    AssociationState association_state{AssociationState::Valid};
    double raw_value{0.0};
    bool valid{false};
};

DimensionLayout evaluate_dimension_layout(const DimensionObject& dim, const DocumentSnapshot& snapshot);
DimensionLayout evaluate_dimension_layout(const DimensionObject& dim, const Document& doc);

} // namespace pikdraw::document
