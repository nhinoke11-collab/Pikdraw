from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))

from tooling.performance.canvas_semantic_oracle import (
    composite_rgba8_bitmap,
    contains_path,
    flatten_hierarchy,
    source_over_rgba8,
    validate_unsupported_outcome,
)


class CanvasSemanticOracleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        fixture_path = ROOT / "docs" / "quality" / "reference-fixtures" / "canvas-semantics.json"
        cls.fixture = json.loads(fixture_path.read_text(encoding="utf-8"))

    def test_geometry_vectors_cover_evenodd_and_nonzero(self) -> None:
        for vector in self.fixture["geometry"]["fill_rule_vectors"]:
            for probe in vector["probes"]:
                with self.subTest(vector=vector["id"], point=probe["point"]):
                    self.assertEqual(
                        contains_path(probe["point"], vector["contours"], "evenodd"),
                        probe["evenodd"],
                    )
                    self.assertEqual(
                        contains_path(probe["point"], vector["contours"], "nonzero"),
                        probe["nonzero"],
                    )

    def test_source_over_vectors_match_integer_reference(self) -> None:
        for vector in self.fixture["color"]["vectors"]:
            with self.subTest(vector=vector["id"]):
                self.assertEqual(
                    source_over_rgba8(vector["source_rgba8"], vector["destination_rgba8"]),
                    tuple(vector["expected_rgba8"]),
                )

    def test_synthetic_rgba8_bitmap_matches_independent_pixel_reference(self) -> None:
        bitmap = self.fixture["bitmap_rgba8"]
        self.assertEqual(bitmap["pixel_format"], "RGBA8_STRAIGHT")
        self.assertEqual(bitmap["runtime_capability"]["software_renderer_status"], "unsupported")
        self.assertEqual(bitmap["runtime_capability"]["runtime_validation"], "not_measured")

        for vector in bitmap["vectors"]:
            with self.subTest(vector=vector["id"]):
                self.assertEqual(len(vector["source_pixels_rgba8"]), vector["height"])
                self.assertEqual(len(vector["source_pixels_rgba8"][0]), vector["width"])
                actual = composite_rgba8_bitmap(
                    vector["source_pixels_rgba8"],
                    vector["background_pixels_rgba8"],
                )
                self.assertEqual(
                    [[list(pixel) for pixel in row] for row in actual],
                    vector["expected_pixels_rgba8"],
                )

    def test_bitmap_oracle_rejects_mismatched_dimensions(self) -> None:
        with self.assertRaisesRegex(ValueError, "heights"):
            composite_rgba8_bitmap([[[0, 0, 0, 0]]], [])
        with self.assertRaisesRegex(ValueError, "width"):
            composite_rgba8_bitmap([[[0, 0, 0, 0], [0, 0, 0, 0]]], [[[0, 0, 0, 0]]])

    def test_unsupported_feature_requires_error_and_preserves_source(self) -> None:
        scenario = self.fixture["unsupported_feature_scenarios"][0]
        self.assertEqual(scenario["current_implementation"]["status"], "unsupported")
        self.assertEqual(scenario["current_implementation"]["runtime_validation"], "not_measured")
        expected = scenario["expected_outcome"]
        diagnostic = expected["diagnostic"]
        observation = {
            "render_success": False,
            "accepted_as_complete_frame": False,
            "source_preserved": True,
            "flattened": False,
            "diagnostics": [
                {
                    "object_id": diagnostic["object_id"],
                    "error": True,
                    "message": "Object cannot be rendered: invalid/unsupported data",
                }
            ],
        }
        validate_unsupported_outcome(observation, expected)

    def test_unsupported_feature_rejects_silent_or_flattened_result(self) -> None:
        expected = self.fixture["unsupported_feature_scenarios"][0]["expected_outcome"]
        base = {
            "render_success": False,
            "accepted_as_complete_frame": False,
            "source_preserved": True,
            "flattened": False,
            "diagnostics": [],
        }
        with self.assertRaisesRegex(ValueError, "diagnostic"):
            validate_unsupported_outcome(base, expected)

        flattened = dict(base)
        flattened["flattened"] = True
        with self.assertRaisesRegex(ValueError, "flattened"):
            validate_unsupported_outcome(flattened, expected)

    def test_hierarchy_preserves_order_visibility_and_ancestor_opacity(self) -> None:
        hierarchy = self.fixture["hierarchy"]
        actual = flatten_hierarchy(
            hierarchy["roots"],
            max_group_depth=hierarchy["max_group_depth"],
        )
        self.assertEqual(actual, hierarchy["expected_leaves"])
        self.assertNotIn("hidden-shape", [item["id"] for item in actual])

    def test_rejects_invalid_fill_rule_and_malformed_geometry(self) -> None:
        with self.assertRaises(ValueError):
            contains_path((1, 1), [[[0, 0], [1, 0], [0, 1]]], "winding")
        with self.assertRaises(ValueError):
            contains_path((1, 1), [[[0, 0], [1, 0]]], "evenodd")

    def test_rejects_cycles_duplicate_ids_invalid_opacity_and_excess_depth(self) -> None:
        cycle = {"id": "cycle", "type": "group", "children": []}
        cycle["children"].append(cycle)
        with self.assertRaisesRegex(ValueError, "cycle"):
            flatten_hierarchy([cycle])

        with self.assertRaisesRegex(ValueError, "duplicate"):
            flatten_hierarchy(
                [
                    {"id": "same", "type": "shape"},
                    {"id": "same", "type": "shape"},
                ]
            )

        with self.assertRaisesRegex(ValueError, "opacity"):
            flatten_hierarchy([{"id": "bad", "type": "shape", "opacity": 1.5}])

        node = {"id": "leaf", "type": "shape"}
        for index in reversed(range(9)):
            node = {"id": f"group-{index}", "type": "group", "children": [node]}
        with self.assertRaisesRegex(ValueError, "depth"):
            flatten_hierarchy([node], max_group_depth=8)

    def test_source_over_rejects_out_of_range_channels(self) -> None:
        with self.assertRaises(ValueError):
            source_over_rgba8((0, 0, 0, 256), (0, 0, 0, 255))
        with self.assertRaises(ValueError):
            source_over_rgba8((0, 0, 0, 12.5), (0, 0, 0, 255))


if __name__ == "__main__":
    unittest.main()

