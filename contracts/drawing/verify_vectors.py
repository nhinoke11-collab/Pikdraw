"""Check reference-vector structure and independent analytic expectations.

This validates the contract's math and fixture integrity. It does not test the PikDraw application
or make these vectors an automatically enforced repository gate.
"""

from __future__ import annotations

import json
import math
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parent


def load_json(name: str) -> dict[str, Any]:
    with (ROOT / name).open(encoding="utf-8") as source:
        value = json.load(source)
    if not isinstance(value, dict):
        raise ValueError(f"{name} must contain a JSON object")
    return value


def close(actual: float, expected: float, tolerance: float, label: str) -> None:
    if not math.isfinite(actual) or abs(actual - expected) > tolerance:
        raise AssertionError(
            f"{label}: actual={actual:.16g}, expected={expected:.16g}, tol={tolerance:.3g}"
        )


def close_points(actual: list[float], expected: list[float], tolerance: float, label: str) -> None:
    if len(actual) != len(expected):
        raise AssertionError(f"{label}: vector length differs")
    for index, (a_value, e_value) in enumerate(zip(actual, expected)):
        close(a_value, e_value, tolerance, f"{label}[{index}]")


def normalize_degrees(value: float) -> float:
    result = value % 360.0
    return 0.0 if math.isclose(result, 360.0, abs_tol=1e-12) else result


def quantize_degrees(angle: float, step: float, previous: float | None) -> float:
    candidates = [normalize_degrees(index * step) for index in range(round(360.0 / step))]

    def distance(candidate: float) -> float:
        return abs((normalize_degrees(angle - candidate) + 180.0) % 360.0 - 180.0)

    distances = [distance(candidate) for candidate in candidates]
    nearest = min(distances)
    tied = [candidate for candidate, delta in zip(candidates, distances)
            if math.isclose(delta, nearest, abs_tol=1e-10)]
    if previous is not None:
        previous = normalize_degrees(previous)
        if previous in tied:
            return previous
    return min(tied)


def check_numeric(vectors: list[dict[str, Any]], semantics: dict[str, Any]) -> int:
    passed = 0
    factors = semantics["numeric_input"]["exact_unit_factors_mm"]
    for vector in vectors:
        case_id = vector["id"]
        tolerance = float(vector.get("absolute_tolerance_mm", 1e-12))
        if case_id == "relative_polar_45":
            distance = 250.0
            radians = math.radians(45.0)
            start = vector["start_mm"]
            actual = [start[0] + distance * math.cos(radians),
                      start[1] + distance * math.sin(radians)]
            close_points(actual, vector["expected_mm"], tolerance, case_id)
            passed += 1
        elif case_id == "relative_cartesian":
            close_points([10.0 + 20.0, 20.0 + 15.0], vector["expected_mm"], tolerance, case_id)
            passed += 1
        elif case_id == "absolute_world":
            close_points([120.0, 80.0], vector["expected_mm"], tolerance, case_id)
            passed += 1
        elif case_id == "inch_exact_factor":
            close(factors["in"], vector["expected_mm"], tolerance, case_id)
            passed += 1
        elif case_id == "point_exact_factor":
            close(72.0 * factors["pt"], vector["expected_mm"], tolerance, case_id)
            passed += 1
        elif case_id == "typed_length_expression":
            close(2.0 * 125.0, vector["expected_mm"], tolerance, case_id)
            passed += 1
        elif case_id == "parenthesized_length_expression":
            close((100.0 + 20.0) / 2.0, vector["expected_mm"], tolerance, case_id)
            passed += 1
        elif case_id == "comma_decimal_locale":
            close(0.5 * factors["in"], vector["expected_mm"], tolerance, case_id)
            passed += 1
        elif case_id == "coordinate_semicolon_avoids_decimal_comma_ambiguity":
            close_points([20.5, 15.25], vector["expected_delta_mm"], tolerance, case_id)
            passed += 1
        elif "input_angle_deg" in vector and "expected_angle_deg" in vector and "step_deg" in vector:
            actual = quantize_degrees(
                vector["input_angle_deg"], vector["step_deg"], vector["previous_angle_deg"]
            )
            close(actual, vector["expected_angle_deg"], 1e-10, case_id)
            passed += 1
        elif vector.get("mode") == "ortho" or case_id == "explicit_ortho_survives_snap_off":
            actual = normalize_degrees(round(vector["input_angle_deg"] / 90.0) * 90.0)
            close(actual, vector["expected_angle_deg"], 1e-10, case_id)
            passed += 1
    return passed


def check_shapes(vectors: list[dict[str, Any]]) -> int:
    passed = 0
    for vector in vectors:
        case_id = vector["id"]
        tolerance = float(vector.get("absolute_tolerance_mm", 1e-10))
        if case_id == "rectangle_from_center":
            cx, cy = vector["anchor_mm"]
            px, py = vector["raw_corner_mm"]
            bounds = [cx - abs(px - cx), cy - abs(py - cy),
                      cx + abs(px - cx), cy + abs(py - cy)]
            close_points(bounds, vector["expected_bounds_mm"], tolerance, case_id)
            passed += 1
        elif case_id in {"polygon_center_vertex_up_n4_r10", "polygon_center_edge_up_n4_r10"}:
            count = vector["N"]
            center_x, center_y = vector["center_mm"]
            radius = vector["R_mm"]
            phase = math.pi / 2.0 + vector["rotation_deg"] * math.pi / 180.0
            if vector["orientation"] == "edge_up":
                phase += math.pi / count
            vertices = [[center_x + radius * math.cos(phase + 2.0 * math.pi * index / count),
                         center_y + radius * math.sin(phase + 2.0 * math.pi * index / count)]
                        for index in range(count)]
            expected = vector["expected_vertices_mm"]
            for index, (actual, target) in enumerate(zip(vertices, expected)):
                close_points(actual, target, tolerance, f"{case_id}[{index}]")
            passed += 1
        elif case_id == "polygon_radius_side_oracles":
            radius = vector["R_mm"]
            for sides_text, expected in vector["expected_by_N"].items():
                sides = int(sides_text)
                close(2.0 * radius * math.sin(math.pi / sides), expected["side_mm"], tolerance,
                      f"{case_id}.N{sides}.side")
                close(radius * math.cos(math.pi / sides), expected["inradius_mm"], tolerance,
                      f"{case_id}.N{sides}.inradius")
            passed += 1
    return passed


def check_arcs(vectors: list[dict[str, Any]]) -> int:
    passed = 0
    for vector in vectors:
        if vector["id"] not in {"quarter_circle_midpoint", "reflected_arc_keeps_parameter"}:
            continue
        if "affine" in vector:
            center = vector["center_mm"]
            u = vector["U_mm"]
            v = vector["V_mm"]
            matrix = vector["affine"]
            old_center = center
            old_u = u
            old_v = v
            center = [matrix[0][0] * old_center[0] + matrix[0][1] * old_center[1] + matrix[0][2],
                      matrix[1][0] * old_center[0] + matrix[1][1] * old_center[1] + matrix[1][2]]
            u = [matrix[0][0] * old_u[0] + matrix[0][1] * old_u[1],
                 matrix[1][0] * old_u[0] + matrix[1][1] * old_u[1]]
            v = [matrix[0][0] * old_v[0] + matrix[0][1] * old_v[1],
                 matrix[1][0] * old_v[0] + matrix[1][1] * old_v[1]]
            close_points(center, vector["expected_center_mm"], 1e-12, vector["id"] + ".center")
            close_points(u, vector["expected_U_mm"], 1e-12, vector["id"] + ".U")
            close_points(v, vector["expected_V_mm"], 1e-12, vector["id"] + ".V")
        else:
            center = vector["center_mm"]
            u = vector["U_mm"]
            v = vector["V_mm"]
        angle = vector["alpha0_rad"] + vector["parameter_t"] * vector["sweep_rad"]
        point = [center[0] + u[0] * math.cos(angle) + v[0] * math.sin(angle),
                 center[1] + u[1] * math.cos(angle) + v[1] * math.sin(angle)]
        close_points(point, vector["expected_mm"], vector["absolute_tolerance_mm"], vector["id"])
        passed += 1
    return passed


def main() -> int:
    semantics = load_json("semantics.json")
    vectors_doc = load_json("vectors.json")
    if semantics["version"] != vectors_doc["contract_version"]:
        raise AssertionError("semantic and vector contract versions differ")
    vector_groups = vectors_doc["vector_groups"]
    ids = [entry["id"] for group in vector_groups.values() for entry in group]
    if len(ids) != len(set(ids)):
        raise AssertionError("vector IDs must be unique")
    numeric = check_numeric(vector_groups["numeric_and_direction"], semantics)
    shapes = check_shapes(vector_groups["shapes"])
    arcs = check_arcs(vector_groups["path_identity"])
    print(f"PASS: {len(ids)} vectors parse; {numeric} numeric/direction, {shapes} shape, "
          f"and {arcs} analytic arc oracle checks pass.")
    print("LIMIT: this validates reference data only; it does not run PikDraw consumers or app UI.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
