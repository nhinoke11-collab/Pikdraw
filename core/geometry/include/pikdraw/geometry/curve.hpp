#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include "pikdraw/geometry/point.hpp"
#include "pikdraw/geometry/rect.hpp"
#include "pikdraw/geometry/transform.hpp"

namespace pikdraw::geometry {

enum class SegmentType {
    MoveTo,
    LineTo,
    CubicTo,
    Close
};

struct PathSegment {
    SegmentType type{SegmentType::MoveTo};
    Point2D p0{};
    Point2D p1{};
    Point2D p2{};
    Point2D p3{};

    static PathSegment make_move_to(const Point2D& p) {
        PathSegment s;
        s.type = SegmentType::MoveTo;
        s.p0 = p;
        return s;
    }

    static PathSegment make_line_to(const Point2D& p) {
        PathSegment s;
        s.type = SegmentType::LineTo;
        s.p1 = p;
        return s;
    }

    static PathSegment make_cubic_to(const Point2D& cp1, const Point2D& cp2, const Point2D& end) {
        PathSegment s;
        s.type = SegmentType::CubicTo;
        s.p1 = cp1;
        s.p2 = cp2;
        s.p3 = end;
        return s;
    }

    static PathSegment make_close() {
        PathSegment s;
        s.type = SegmentType::Close;
        return s;
    }
};

// Preserves exact source curves without destructive polygonization
class SourcePath {
public:
    std::vector<PathSegment> segments;

    void move_to(const Point2D& p) { segments.push_back(PathSegment::make_move_to(p)); }
    void line_to(const Point2D& p) { segments.push_back(PathSegment::make_line_to(p)); }
    void cubic_to(const Point2D& cp1, const Point2D& cp2, const Point2D& end) {
        segments.push_back(PathSegment::make_cubic_to(cp1, cp2, end));
    }
    void close() { segments.push_back(PathSegment::make_close()); }

    [[nodiscard]] Rect2D compute_bounds() const;
    [[nodiscard]] SourcePath transformed(const Transform2D& t) const;
};

// Manufacturing approximations are separate from source curve models
struct ManufacturingApproximation {
    std::vector<Point2D> polyline;
    double tolerance_mm{0.01};
    bool is_source_curve{false};
};

[[nodiscard]] ManufacturingApproximation discretize_for_manufacturing(
    const SourcePath& source,
    double tolerance_mm = 0.01
);

} // namespace pikdraw::geometry
