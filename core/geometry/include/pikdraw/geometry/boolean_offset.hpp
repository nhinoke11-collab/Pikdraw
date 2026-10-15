#pragma once

#include <vector>
#include <string>
#include <optional>
#include "pikdraw/geometry/rect.hpp"
#include "pikdraw/geometry/curve.hpp"

namespace pikdraw::geometry {

enum class TopologyDiagnosticCode {
    Success = 0,
    UnsupportedTopology,
    SelfIntersectionDetected,
    DegenerateGeometry,
    ExceedsErrorBudget
};

struct TopologyDiagnostic {
    TopologyDiagnosticCode code{TopologyDiagnosticCode::Success};
    std::string message;
    double estimated_error_mm{0.0};

    [[nodiscard]] bool ok() const {
        return code == TopologyDiagnosticCode::Success;
    }
};

// Robust analytic subset for orthogonal rectangles
class RectangleBooleanEngine {
public:
    [[nodiscard]] static std::optional<Rect2D> intersect(
        const Rect2D& a, const Rect2D& b, TopologyDiagnostic* diag = nullptr
    );

    [[nodiscard]] static std::vector<Rect2D> subtract(
        const Rect2D& a, const Rect2D& b, TopologyDiagnostic* diag = nullptr
    );

    [[nodiscard]] static Rect2D offset(
        const Rect2D& a, double delta_mm, TopologyDiagnostic* diag = nullptr
    );
};

// General candidate function with explicit diagnostic for unsupported complex topology
[[nodiscard]] TopologyDiagnostic evaluate_general_path_boolean(
    const SourcePath& a,
    const SourcePath& b,
    SourcePath& result_out
);

} // namespace pikdraw::geometry
