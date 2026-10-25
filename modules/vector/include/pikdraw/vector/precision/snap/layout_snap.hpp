#pragma once

#include "pikdraw/geometry/primitives/bounds.hpp"
#include "pikdraw/geometry/primitives/point.hpp"
#include "pikdraw/document/model/object_id.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace pikdraw::vector::precision::snap {

enum class LayoutSnapRelation : uint8_t { Alignment, Contact, EqualSpacing };
enum class LayoutSnapReferenceKind : uint8_t { Object, Page, Artboard, Guideline };

struct LayoutSnapKey {
    LayoutSnapRelation relation{LayoutSnapRelation::Alignment};
    bool vertical_axis{false};
    document::ObjectId reference_id{document::kInvalidObjectId};
    document::ObjectId secondary_id{document::kInvalidObjectId};
    uint8_t moving_feature{0};
    uint8_t reference_feature{0};
    LayoutSnapReferenceKind reference_kind{LayoutSnapReferenceKind::Object};
    friend bool operator==(const LayoutSnapKey&, const LayoutSnapKey&) = default;
};

struct LayoutSnapReference {
    document::ObjectId id{document::kInvalidObjectId};
    geometry::Rect2D bounds;
    LayoutSnapReferenceKind kind{LayoutSnapReferenceKind::Object};
};

struct LayoutSnapLine {
    uint64_t id{0};
    bool vertical_axis{false};
    double coordinate_mm{0.0};
    double start_mm{0.0};
    double end_mm{0.0};
};

struct LayoutSnapGuide {
    LayoutSnapKey key;
    double coordinate_mm{0.0};
    double start_mm{0.0};
    double end_mm{0.0};
    double spacing_mm{0.0};
};

struct LayoutSnapRequest {
    geometry::Rect2D moving_bounds;
    geometry::Point2D raw_delta;
    std::vector<LayoutSnapReference> references;
    std::vector<LayoutSnapLine> guidelines;
    std::array<std::optional<LayoutSnapKey>, 2> previous_acquisition;
    double dip_per_mm_x{1.0};
    double dip_per_mm_y{1.0};
    double acquire_radius_dip{8.0};
    double release_radius_dip{12.0};
    // Bit i allows moving feature i (low edge, center, high edge) to acquire.
    // Resize adapters use this to keep the fixed anchor out of the candidate set.
    uint8_t moving_feature_mask_x{0x07};
    uint8_t moving_feature_mask_y{0x07};
    bool alignment_enabled{true};
    bool spacing_enabled{true};
    bool objects_enabled{true};
    bool page_enabled{true};
    bool guidelines_enabled{true};
};

struct LayoutSnapSolution {
    geometry::Point2D accepted_delta;
    std::array<std::optional<LayoutSnapKey>, 2> acquisition;
    std::vector<LayoutSnapGuide> guides;
    bool snapped{false};
};

/// Solves screen-tolerance layout alignment and equal-gap placement for one moving bounds.
/// References are expected to exclude the moved selection roots and their descendants.
[[nodiscard]] LayoutSnapSolution solve_layout_snap(const LayoutSnapRequest& request);

} // namespace pikdraw::vector::precision::snap
