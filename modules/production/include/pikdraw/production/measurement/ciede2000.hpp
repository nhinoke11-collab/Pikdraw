#pragma once

namespace pikdraw::production::measurement {

struct LabValue {
    double l{0.0};
    double a{0.0};
    double b{0.0};
};

/// CIEDE2000 for D50-referenced CIELAB coordinates. This numerical helper is
/// not a claim of standards-body certification or instrument qualification.
[[nodiscard]] double delta_e_2000(const LabValue& first,
                                  const LabValue& second) noexcept;

} // namespace pikdraw::production::measurement
