#pragma once

#include "pikdraw/document/annotations/annotation_object.hpp"
#include "pikdraw/document/model/object.hpp"
#include "pikdraw/geometry/curves/path.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"
#include "pikdraw/geometry/primitives/transform.hpp"

#include <string>

namespace pikdraw::document {

struct AnnotationLayout {
    geometry::Path2D leader_path;
    geometry::Path2D arrow_path;
    geometry::Rect2D text_box;
    geometry::Point2D text_origin;
    std::string formatted_text;
    double text_size_mm{2.5};
    geometry::Rect2D bounds;
};

struct DetailViewLayout {
    geometry::Path2D marker_path;
    geometry::Path2D frame_path;
    geometry::Path2D connector_path;
    geometry::Rect2D label_box;
    geometry::Point2D label_origin;
    std::string formatted_label;
    geometry::Transform2D source_to_frame_transform{geometry::Transform2D::identity()};
    geometry::Rect2D bounds;
};

[[nodiscard]] AnnotationLayout evaluate_annotation_layout(const AnnotationObject& ann);

[[nodiscard]] DetailViewLayout evaluate_detail_view_layout(const DetailViewObject& dv);

[[nodiscard]] PathObject annotation_to_path_object(
    const AnnotationObject& ann, const AnnotationLayout& layout);

[[nodiscard]] PathObject detail_view_to_path_object(
    const DetailViewObject& dv, const DetailViewLayout& layout);

} // namespace pikdraw::document
