#pragma once

#include "pikdraw/document/annotations/dimension_object.hpp"
#include "pikdraw/document/model/object_id.hpp"
#include "pikdraw/geometry/dimensions/dimension_geometry.hpp"
#include "pikdraw/geometry/primitives/point.hpp"
#include "pikdraw/geometry/primitives/bounds.hpp"
#include "pikdraw/geometry/primitives/transform.hpp"
#include "pikdraw/color/values/color_value.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pikdraw::document {

enum class AnnotationKind : uint8_t {
    LeaderNote,
    PlainNote,
    Balloon,
    RevisionCloud
};

struct AnnotationAnchor {
    ObjectId source_object_id{kInvalidObjectId};
    geometry::Point2D anchor_pt{0.0, 0.0};
    AssociationState state{AssociationState::Fixed};
    std::optional<uint64_t> element_id{std::nullopt};
    double parameter{0.0};

    friend bool operator==(const AnnotationAnchor&, const AnnotationAnchor&) = default;
};

struct AnnotationStyle {
    double text_size_mm{2.5};        // Default 2.5 mm on sheet (PikDraw standard preset)
    double line_width_mm{0.25};      // Default 0.25 mm line
    double arrow_size_mm{2.0};       // Default 2.0 mm arrowhead
    geometry::ArrowType arrow_type{geometry::ArrowType::ClosedFilled};
    color::ColorValue color{color::RGBColor{0.1, 0.1, 0.1, 1.0}};
    std::string font_family{"Segoe UI"};
    bool has_landing{true};
    double landing_length_mm{4.0};   // Horizontal landing bar length
    double gap_mm{1.0};              // Gap between landing and text

    static AnnotationStyle make_default() noexcept { return AnnotationStyle{}; }
};

struct AnnotationObject {
    ObjectId id{kInvalidObjectId};
    std::string name{"Leader Note"};
    AnnotationKind kind{AnnotationKind::LeaderNote};
    AnnotationAnchor anchor;
    std::vector<geometry::Point2D> leader_points; // Points along leader line
    geometry::Point2D landing_point{0.0, 0.0};
    geometry::Point2D text_point{0.0, 0.0};
    std::string text;
    AnnotationStyle style{AnnotationStyle::make_default()};
    geometry::Transform2D transform{geometry::Transform2D::identity()};
    bool visible{true};
    bool locked{false};

    [[nodiscard]] geometry::Rect2D bounds() const;
};

enum class DetailViewShape : uint8_t {
    Circle = 0,
    Rectangle = 1
};

struct DetailViewObject {
    ObjectId id{kInvalidObjectId};
    std::string name{"Detail View"};
    ObjectId marker_id{kInvalidObjectId};
    ObjectId label_id{kInvalidObjectId};
    uint64_t source_page_id{1};
    geometry::Rect2D source_region{0.0, 0.0, 20.0, 20.0}; // Circle center & extent or Rect bounds
    geometry::Rect2D frame_rect{40.0, 40.0, 60.0, 60.0};   // Target placed frame
    double scale_ratio{2.0};                              // e.g. 2.0 = 2:1, 3.0 = 3:1
    DetailViewShape shape{DetailViewShape::Circle};
    bool show_connector{true};
    std::string label{"DETAIL A"};
    double line_width_mm{0.25};
    color::ColorValue color{color::RGBColor{0.1, 0.1, 0.1, 1.0}};
    double text_size_mm{2.5};
    geometry::Transform2D transform{geometry::Transform2D::identity()};
    bool visible{true};
    bool locked{false};

    [[nodiscard]] geometry::Rect2D bounds() const;
};

[[nodiscard]] std::string serialize_annotation_spec(const AnnotationObject& ann);
[[nodiscard]] std::optional<AnnotationObject> deserialize_annotation_spec(
    std::string_view spec, std::string* error_reason = nullptr);

[[nodiscard]] std::string serialize_detail_view_spec(const DetailViewObject& dv);
[[nodiscard]] std::optional<DetailViewObject> deserialize_detail_view_spec(
    std::string_view spec, std::string* error_reason = nullptr);

class AnnotationRegistry {
public:
    static AnnotationRegistry& instance();
    void set(ObjectId id, AnnotationObject ann);
    void remove(ObjectId id);
    [[nodiscard]] std::optional<AnnotationObject> get(ObjectId id) const;
    [[nodiscard]] bool contains(ObjectId id) const;
    void clear();

private:
    std::unordered_map<ObjectId, AnnotationObject> annotations_;
};

class DetailViewRegistry {
public:
    static DetailViewRegistry& instance();
    void set(ObjectId id, DetailViewObject dv);
    void remove(ObjectId id);
    [[nodiscard]] std::optional<DetailViewObject> get(ObjectId id) const;
    [[nodiscard]] bool contains(ObjectId id) const;
    void clear();

private:
    std::unordered_map<ObjectId, DetailViewObject> views_;
};

} // namespace pikdraw::document
