from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
from itertools import islice
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))

from tooling.performance import canvas_fixture_manifest as corpus


class CanvasFixtureManifestTests(unittest.TestCase):
    def test_checked_in_manifest_is_reproducible_and_hashed(self) -> None:
        path = ROOT / "tests" / "fixtures" / "canvas-corpus-v1.json"
        stored = json.loads(path.read_text(encoding="utf-8"))
        generated = corpus.build_manifest()
        self.assertEqual(stored, generated)

        integrity = dict(stored["integrity"])
        semantic_hash = integrity.pop("semantic_sha256")
        payload = dict(stored)
        payload.pop("integrity")
        self.assertEqual(
            semantic_hash,
            hashlib.sha256(corpus.canonical_bytes(payload)).hexdigest(),
        )
        self.assertEqual(integrity["algorithm"], "sha256")

    def test_seed_reproduces_metadata_and_changes_semantic_hash(self) -> None:
        first = corpus.build_manifest(20261016)
        second = corpus.build_manifest(20261016)
        other_seed = corpus.build_manifest(20261017)
        self.assertEqual(first, second)
        self.assertNotEqual(
            first["integrity"]["semantic_sha256"],
            other_seed["integrity"]["semantic_sha256"],
        )

    def test_s0_through_s8_counts_match_acceptance_corpus(self) -> None:
        workloads = {item["id"]: item for item in corpus.build_manifest()["workloads"]}
        self.assertEqual(workloads["S0-empty"]["object_count"], 0)
        self.assertEqual(workloads["S0-shapes-100"]["object_count"], 100)
        self.assertEqual(workloads["S1-sparse-10pct"]["object_count"], 10_000)
        self.assertEqual(workloads["S1-sparse-10pct"]["path_element_count"], 100_000)
        self.assertEqual(workloads["S2-all-overlap"]["object_count"], 100_000)
        self.assertEqual(workloads["S2-all-overlap"]["path_element_count"], 1_000_000)
        self.assertEqual(workloads["S3-path-1m"]["segment_count"], 1_000_000)
        self.assertEqual(workloads["S4-mixed-images"]["image_count"], 22)
        self.assertEqual(workloads["S4-mixed-images"]["decoded_rgba8_pixels"], 1_280_000_000)
        self.assertEqual(workloads["S5-effects-100"]["effect_node_count"], 100)
        self.assertEqual(workloads["S6-vietnamese-text"]["glyph_count"], 100_000)
        self.assertEqual(workloads["S7-mixed-stress"]["object_count"], 100_000)

        offset_probe = workloads["S8-offset-probe-4GiB-plus-1MiB"]
        self.assertGreater(offset_probe["source_bytes"], 1 << 32)
        self.assertEqual(offset_probe["offset_probe_bytes"], 1 << 32)
        self.assertFalse(offset_probe["materialized_by_default"])
        self.assertEqual(offset_probe["layout"], "dense sequential writes; never sparse-zero substitute")

    def test_samples_are_bounded_and_synthetic(self) -> None:
        workloads = corpus.build_manifest()["workloads"]
        for workload in workloads:
            self.assertLessEqual(len(workload["sample_objects"]), 8)
            self.assertEqual(
                workload["sample_scope"],
                "bounded preview only; replay section hashes the complete scene streams",
            )
        self.assertTrue(all(":" in row["id"] for row in workloads[2]["sample_objects"]))
        self.assertEqual(workloads[0]["sample_objects"], [])

    def test_s0_through_s7_have_full_stream_summaries_and_replayable_camera_traces(self) -> None:
        workloads = corpus.build_manifest()["workloads"]
        for workload in workloads:
            if workload["scenario_id"] == "S8":
                continue
            replay = workload["replay"]
            self.assertEqual(replay["mode"], "replayable-bounded-stream")
            self.assertEqual(replay["object_stream"]["count"], workload["object_count"])
            self.assertEqual(
                replay["path_element_stream"]["count"],
                workload["path_element_count"],
            )
            self.assertEqual(len(replay["object_stream"]["semantic_sha256"]), 64)
            self.assertEqual(len(replay["path_element_stream"]["semantic_sha256"]), 64)

            trace = dict(workload["camera_trace"])
            trace_hash = trace.pop("semantic_sha256")
            self.assertEqual(
                trace_hash, hashlib.sha256(corpus.canonical_bytes(trace)).hexdigest()
            )
            self.assertEqual(trace["workload_id"], workload["id"])
            self.assertEqual(len(trace["steps"]), 4)

        by_id = {workload["id"]: workload for workload in workloads}
        self.assertEqual(
            by_id["S4-mixed-images"]["payload_status"],
            "metadata_only_no_bitmap_bytes_materialized",
        )
        self.assertEqual(
            by_id["S5-effects-100"]["payload_status"],
            "synthetic_effect_graph_metadata_only_no_filtered_pixels_materialized",
        )
        self.assertEqual(
            by_id["S6-vietnamese-text"]["payload_status"],
            "synthetic_text_metadata_only_no_font_or_glyph_atlas_materialized",
        )
        self.assertEqual(by_id["S8-4GiB"]["scenario_id"], "S8")
        self.assertEqual(
            corpus.build_manifest()["s8_materialization"]["native_reader_gate"],
            "not_measured",
        )

    def test_full_workload_iterators_replay_counts_and_s3_giant_coordinates(self) -> None:
        first_objects = list(
            corpus.iter_workload_objects(
                41, "S1-small", 7, 19, object_kind="vector"
            )
        )
        replayed_objects = list(
            corpus.iter_workload_objects(
                41, "S1-small", 7, 19, object_kind="vector"
            )
        )
        self.assertEqual(first_objects, replayed_objects)
        self.assertEqual(len(first_objects), 7)
        self.assertEqual(sum(row["path_elements"] for row in first_objects), 19)

        paths = list(corpus.iter_path_elements(41, "S1-small", 7, 19))
        self.assertEqual(len(paths), 19)
        self.assertEqual(paths[0]["element_index"], 0)
        self.assertEqual(paths[-1]["object_id"], first_objects[-1]["id"])
        summary = corpus.summarize_scene_streams(41, "S1-small", 7, 19)
        self.assertEqual(summary["object_stream"]["count"], 7)
        self.assertEqual(summary["path_element_stream"]["count"], 19)
        self.assertEqual(
            summary,
            corpus.summarize_scene_streams(41, "S1-small", 7, 19),
        )

        stress_path = list(corpus.iter_path_elements(41, "S3-small", 1, 130))
        self.assertEqual(stress_path[1]["x_mm"], 1 << 40)
        self.assertEqual(stress_path[2]["y_mm"], 1 << 40)
        self.assertEqual(stress_path[127]["command"], "close")
        self.assertEqual(stress_path[128]["command"], "move_to")

    def test_large_iterators_yield_incrementally(self) -> None:
        objects = corpus.iter_workload_objects(
            52, "S2-probe", 100_000, 1_000_000, overlap=True
        )
        paths = corpus.iter_path_elements(52, "S2-probe", 100_000, 1_000_000)
        object_prefix = list(islice(objects, 3))
        path_prefix = list(islice(paths, 3))
        self.assertEqual(len(object_prefix), 3)
        self.assertEqual(len(path_prefix), 3)
        self.assertTrue(all(0 <= row["x_mm"] < 32 for row in object_prefix))
        self.assertTrue(all(row["element_index"] == index for index, row in enumerate(path_prefix)))

    def test_materializer_writes_real_deterministic_entropy_and_refuses_overwrite(self) -> None:
        with tempfile.TemporaryDirectory(prefix="pikdraw-s8-test-") as temp:
            path = Path(temp) / "dense-payload.bin"
            summary = corpus.materialize_s8(
                path,
                1024 * 1024 + 123,
                seed=20261016,
                chunk_bytes=64 * 1024,
            )
            data = path.read_bytes()
            self.assertEqual(len(data), 1024 * 1024 + 123)
            self.assertEqual(summary["size_bytes"], len(data))
            self.assertEqual(summary["sha256"], hashlib.sha256(data).hexdigest())
            self.assertTrue(summary["dense_write"])
            self.assertGreater(summary["entropy_bits_per_byte"], 7.8)
            self.assertGreater(len(set(data)), 250)
            self.assertEqual(summary["chunk_count"], 17)
            with self.assertRaises(FileExistsError):
                corpus.materialize_s8(path, 4096, chunk_bytes=4096)
            self.assertEqual(path.stat().st_size, len(data))


if __name__ == "__main__":
    unittest.main()

