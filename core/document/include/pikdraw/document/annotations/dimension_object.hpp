#pragma once

#include "pikdraw/document/annotations/dimension_style.hpp"
#include "pikdraw/document/model/object_id.hpp"
#include "pikdraw/geometry/dimensions/dimension_geometry.hpp"
#include "pikdraw/geometry/primitives/point.hpp"
#include "pikdraw/geometry/primitives/transform.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pikdraw::document {

enum class DimensionKind : uint8_t {
    Linear,
    Aligned,
    Angular,
    Radius,
    Diameter,
    ArcLength,
    Ordinate,
    MultiLeader
};

enum class AssociationState : uint8_t {
    Valid,
    Partial,
    Broken,
    Fixed // non-associative world points
};

enum class SourceRefKind : uint8_t {
    Node,
    Segment,
    Center,
    ExplicitPoint
};

struct SourceReference {
    ObjectId source_object_id{kInvalidObjectId};
    SourceRefKind ref_kind{SourceRefKind::ExplicitPoint};
    std::optional<uint64_t> element_id;
    geometry::Point2D explicit_point{0.0, 0.0};
    friend bool operator==(const SourceReference&, const SourceReference&) = default;
};

struct DimensionObject {
    ObjectId id{kInvalidObjectId};
    std::string name{"Dimension"};
    DimensionKind kind{DimensionKind::Aligned};
    std::vector<SourceReference> references;
    geometry::Point2D placement_point{0.0, 0.0};
    DimensionStyle style{DimensionStyle::make_default()};
    std::string note_template; // e.g. "<>" token or text override
    bool flip_arrows{false};
    geometry::LinearOrientation linear_orientation{geometry::LinearOrientation::Aligned};
    geometry::OrdinateAxis ordinate_axis{geometry::OrdinateAxis::X};
    geometry::Point2D datum_origin{0.0, 0.0};
    geometry::Transform2D transform{geometry::Transform2D::identity()};
    bool visible{true};
    bool locked{false};
    AssociationState association_state{AssociationState::Valid};

    [[nodiscard]] geometry::Rect2D bounds() const;
};

[[nodiscard]] std::string serialize_dimension_spec(const DimensionObject& dim);
[[nodiscard]] std::optional<DimensionObject> deserialize_dimension_spec(std::string_view spec);

} // namespace pikdraw::document
