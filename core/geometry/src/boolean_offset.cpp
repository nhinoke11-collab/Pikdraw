#include "pikdraw/geometry/boolean_offset.hpp"
#include <stdexcept>

namespace pikdraw::geometry {
namespace {
void validate_rectangle(const Rect2D& rect) {
    if (!rect.is_finite() || rect.min_x>rect.max_x || rect.min_y>rect.max_y)
        throw std::invalid_argument("Invalid finite rectangle bounds");
}
}

std::optional<Rect2D> RectangleBooleanEngine::intersect(
    const Rect2D& a, const Rect2D& b, TopologyDiagnostic* diag
) {
    validate_rectangle(a); validate_rectangle(b);
    if (diag) {
        diag->code = TopologyDiagnosticCode::Success;
        diag->message = "OK";
        diag->estimated_error_mm = 0.0;
    }
    return a.intersect(b);
}

std::vector<Rect2D> RectangleBooleanEngine::subtract(
    const Rect2D& a, const Rect2D& b, TopologyDiagnostic* diag
) {
    validate_rectangle(a); validate_rectangle(b);
    if (diag) {
        diag->code = TopologyDiagnosticCode::Success;
        diag->message = "OK";
        diag->estimated_error_mm = 0.0;
    }

    auto inter_opt = a.intersect(b);
    if (!inter_opt.has_value()) {
        return {a}; // Disjoint
    }
    const Rect2D inter = *inter_opt;
    if (inter.min_x <= a.min_x && inter.max_x >= a.max_x &&
        inter.min_y <= a.min_y && inter.max_y >= a.max_y) {
        return {}; // Entirely subtracted
    }

    std::vector<Rect2D> result;
    // Top slice
    if (inter.min_y > a.min_y) {
        result.emplace_back(a.min_x, a.min_y, a.max_x, inter.min_y);
    }
    // Bottom slice
    if (inter.max_y < a.max_y) {
        result.emplace_back(a.min_x, inter.max_y, a.max_x, a.max_y);
    }
    // Left slice (between inter.min_y and inter.max_y)
    if (inter.min_x > a.min_x) {
        result.emplace_back(a.min_x, inter.min_y, inter.min_x, inter.max_y);
    }
    // Right slice (between inter.min_y and inter.max_y)
    if (inter.max_x < a.max_x) {
        result.emplace_back(inter.max_x, inter.min_y, a.max_x, inter.max_y);
    }
    return result;
}

Rect2D RectangleBooleanEngine::offset(
    const Rect2D& a, double delta_mm, TopologyDiagnostic* diag
) {
    validate_rectangle(a);
    if (!std::isfinite(delta_mm)) throw std::invalid_argument("Nonfinite offset");
    if (diag) {
        diag->code = TopologyDiagnosticCode::Success;
        diag->message = "OK";
        diag->estimated_error_mm = 0.0;
    }
    double nx0 = a.min_x - delta_mm;
    double ny0 = a.min_y - delta_mm;
    double nx1 = a.max_x + delta_mm;
    double ny1 = a.max_y + delta_mm;
    if (nx0 > nx1 || ny0 > ny1) {
        if (diag) { diag->code=TopologyDiagnosticCode::DegenerateGeometry; diag->message="Offset collapses rectangle"; }
        throw std::domain_error("Offset collapses rectangle; no output");
    }
    Rect2D result(nx0, ny0, nx1, ny1); validate_rectangle(result);
    return result;
}

TopologyDiagnostic evaluate_general_path_boolean(
    const SourcePath& a,
    const SourcePath& b,
    SourcePath& result_out
) {
    (void)a;
    (void)b;
    (void)result_out; // Unsupported operation cannot erase caller-owned output.
    // Explicit diagnostic for unsupported non-manifold complex curve boolean without external heavy engine
    TopologyDiagnostic diag;
    diag.code = TopologyDiagnosticCode::UnsupportedTopology;
    diag.message = "General path Boolean is unsupported; algorithm and topology corpus still require evaluation within spike B";
    diag.estimated_error_mm = 0.0;
    return diag;
}

} // namespace pikdraw::geometry
