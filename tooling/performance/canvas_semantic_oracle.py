"""Independent analytic geometry, source-over color, and hierarchy oracles."""
from __future__ import annotations

import math
from typing import Any, Dict, Iterable, List, Sequence, Tuple

Point = Tuple[float, float]
RGBA8 = Tuple[int, int, int, int]


def _validated_contour(contour: Sequence[Sequence[float]]) -> List[Point]:
    points: List[Point] = []
    for point in contour:
        if len(point) != 2:
            raise ValueError("each point must have two coordinates")
        x, y = float(point[0]), float(point[1])
        if not math.isfinite(x) or not math.isfinite(y):
            raise ValueError("coordinates must be finite")
        points.append((x, y))
    if len(points) < 3:
        raise ValueError("a contour needs at least three points")
    return points


def _winding_number(point: Point, contour: Sequence[Point]) -> int:
    px, py = point
    winding = 0
    for index, (x1, y1) in enumerate(contour):
        x2, y2 = contour[(index + 1) % len(contour)]
        cross = (x2 - x1) * (py - y1) - (px - x1) * (y2 - y1)
        if y1 <= py < y2 and cross > 0:
            winding += 1
        elif y2 <= py < y1 and cross < 0:
            winding -= 1
    return winding


def _ray_crossings(point: Point, contour: Sequence[Point]) -> int:
    px, py = point
    crossings = 0
    for index, (x1, y1) in enumerate(contour):
        x2, y2 = contour[(index + 1) % len(contour)]
        if (y1 > py) != (y2 > py):
            x_at_y = x1 + (py - y1) * (x2 - x1) / (y2 - y1)
            if x_at_y > px:
                crossings += 1
    return crossings


def contains_path(
    point: Sequence[float],
    contours: Iterable[Sequence[Sequence[float]]],
    fill_rule: str,
) -> bool:
    """Return analytic point containment for even-odd or nonzero winding fill.

    Test points on an edge are intentionally outside this oracle's contract;
    callers should use a separate edge-distance predicate for boundary cases.
    """
    if len(point) != 2:
        raise ValueError("point must have two coordinates")
    p = (float(point[0]), float(point[1]))
    if not math.isfinite(p[0]) or not math.isfinite(p[1]):
        raise ValueError("point coordinates must be finite")
    if fill_rule not in ("evenodd", "nonzero"):
        raise ValueError("fill_rule must be 'evenodd' or 'nonzero'")
    parsed = [_validated_contour(contour) for contour in contours]
    if fill_rule == "evenodd":
        return sum(_ray_crossings(p, contour) for contour in parsed) % 2 == 1
    return sum(_winding_number(p, contour) for contour in parsed) != 0


def _rgba8(value: Sequence[int]) -> RGBA8:
    if len(value) != 4:
        raise ValueError("RGBA value must have four channels")
    if any(isinstance(channel, bool) or not isinstance(channel, int) for channel in value):
        raise ValueError("RGBA8 channels must be integers")
    channels = tuple(value)
    if any(channel < 0 or channel > 255 for channel in channels):
        raise ValueError("RGBA8 channels must be in [0, 255]")
    return channels  # type: ignore[return-value]


def source_over_rgba8(source: Sequence[int], destination: Sequence[int]) -> RGBA8:
    """Composite straight-alpha RGBA8 source over destination with half-up rounding."""
    sr, sg, sb, sa = _rgba8(source)
    dr, dg, db, da = _rgba8(destination)
    alpha_numerator = sa * 255 + da * (255 - sa)
    if alpha_numerator == 0:
        return (0, 0, 0, 0)

    out_alpha = (alpha_numerator + 127) // 255

    def channel(src: int, dst: int) -> int:
        numerator = src * sa * 255 + dst * da * (255 - sa)
        return (numerator + alpha_numerator // 2) // alpha_numerator

    return (
        channel(sr, dr),
        channel(sg, dg),
        channel(sb, db),
        out_alpha,
    )


def composite_rgba8_bitmap(
    source_pixels: Sequence[Sequence[Sequence[int]]],
    destination_pixels: Sequence[Sequence[Sequence[int]]],
) -> List[List[RGBA8]]:
    """Composite an identity-mapped straight-alpha RGBA8 bitmap per pixel.

    This is a tiny semantic oracle, not an image decoder or a renderer. It
    deliberately supports only equal-sized source and destination grids with
    one source pixel mapping to one output pixel.
    """
    height = len(source_pixels)
    if height == 0 or len(destination_pixels) != height:
        raise ValueError("bitmap source and destination heights must match and be non-empty")

    width = len(source_pixels[0])
    if width == 0:
        raise ValueError("bitmap rows must be non-empty")

    output: List[List[RGBA8]] = []
    for source_row, destination_row in zip(source_pixels, destination_pixels):
        if len(source_row) != width or len(destination_row) != width:
            raise ValueError("bitmap rows must have the same non-zero width")
        output.append(
            [
                source_over_rgba8(source_pixel, destination_pixel)
                for source_pixel, destination_pixel in zip(source_row, destination_row)
            ]
        )
    return output


def validate_unsupported_outcome(
    observation: Dict[str, Any], expected: Dict[str, Any]
) -> None:
    """Reject silent success, flattening, lost source, or a missing diagnostic.

    ``observation`` is the runtime adapter's report. A passing oracle contract
    test does not itself prove that a desktop build produced this observation.
    """
    if not isinstance(observation, dict) or not isinstance(expected, dict):
        raise ValueError("unsupported outcome and expectation must be objects")

    for key in (
        "render_success",
        "accepted_as_complete_frame",
        "source_preserved",
        "flattened",
    ):
        required = expected.get(key)
        if observation.get(key) is not required:
            raise ValueError(f"unsupported outcome violates {key} contract")

    diagnostic_expectation = expected.get("diagnostic")
    if not isinstance(diagnostic_expectation, dict):
        raise ValueError("unsupported expectation needs a diagnostic contract")
    expected_id = diagnostic_expectation.get("object_id")
    expected_fragment = diagnostic_expectation.get("message_contains")
    diagnostics = observation.get("diagnostics")
    if not isinstance(diagnostics, list):
        raise ValueError("unsupported outcome must include diagnostics")

    for diagnostic in diagnostics:
        if not isinstance(diagnostic, dict):
            continue
        message = diagnostic.get("message")
        if (
            diagnostic.get("object_id") == expected_id
            and diagnostic.get("error") is diagnostic_expectation.get("error")
            and isinstance(message, str)
            and isinstance(expected_fragment, str)
            and expected_fragment in message
        ):
            return
    raise ValueError("required object error diagnostic is missing")


def _opacity(node: Dict[str, Any]) -> float:
    opacity = float(node.get("opacity", 1.0))
    if not math.isfinite(opacity) or opacity < 0.0 or opacity > 1.0:
        raise ValueError("node opacity must be finite and within [0, 1]")
    return opacity


def flatten_hierarchy(
    roots: Sequence[Dict[str, Any]],
    *,
    max_group_depth: int = 8,
) -> List[Dict[str, Any]]:
    """Flatten ordered groups to visible leaves while applying ancestor opacity.

    This deliberately models only stable traversal order, visibility, depth and
    opacity. Transform/clip/composite isolation need separate semantic vectors.
    """
    if max_group_depth < 0:
        raise ValueError("max_group_depth must be non-negative")
    output: List[Dict[str, Any]] = []
    seen_ids = set()
    active_objects = set()

    def visit(
        node: Dict[str, Any],
        group_depth: int,
        effective_opacity: float,
        visible_ancestors: bool,
        ancestors: Tuple[str, ...],
    ) -> None:
        identity = id(node)
        if identity in active_objects:
            raise ValueError("hierarchy contains a cycle")
        if not isinstance(node, dict):
            raise ValueError("each hierarchy node must be an object")
        node_id = node.get("id")
        if not isinstance(node_id, str) or not node_id:
            raise ValueError("each node needs a non-empty string id")
        if node_id in seen_ids:
            raise ValueError(f"duplicate node id: {node_id}")
        seen_ids.add(node_id)

        kind = node.get("type")
        if kind not in ("group", "shape"):
            raise ValueError(f"unsupported node type: {kind}")
        opacity = _opacity(node)
        visible = bool(node.get("visible", True))
        path_visible = visible_ancestors and visible
        path_opacity = effective_opacity * opacity

        if kind == "group":
            next_depth = group_depth + 1
            if next_depth > max_group_depth:
                raise ValueError(f"group nesting exceeds maximum depth {max_group_depth}")
            children = node.get("children", [])
            if not isinstance(children, list):
                raise ValueError("group children must be a list")
            active_objects.add(identity)
            try:
                next_ancestors = ancestors + (node_id,)
                for child in children:
                    visit(child, next_depth, path_opacity, path_visible, next_ancestors)
            finally:
                active_objects.remove(identity)
            return

        if path_visible:
            output.append(
                {
                    "id": node_id,
                    "order": len(output),
                    "effective_opacity": path_opacity,
                    "ancestors": list(ancestors),
                }
            )

    for root in roots:
        visit(root, 0, 1.0, True, ())
    return output

