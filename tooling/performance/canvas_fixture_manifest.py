"""Deterministic synthetic canvas workload manifest and bounded S8 payload writer.

The default command writes metadata only. S8 payloads are materialized only when
--materialize-s8 and --size-bytes are both explicitly supplied.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import shutil
import struct
import sys
import time
from collections import Counter
from functools import lru_cache
from itertools import islice
from pathlib import Path
from typing import Any, Dict, Iterator, List, Optional

DEFAULT_SEED = 20261016
MIB = 1024 * 1024
GIB = 1024 * MIB
DEFAULT_CHUNK_BYTES = 4 * MIB
MAX_CHUNK_BYTES = 64 * MIB
DISK_RESERVE_BYTES = 64 * MIB
FORMAT_VERSION = 1
STREAM_RECORDS_PER_CHUNK = 4096
OBJECT_HASH_RECORD = struct.Struct("<IqqII4BI")
PATH_HASH_RECORD = struct.Struct("<IIBqq")
PATH_COMMANDS = ("move_to", "line_to", "quadratic_to", "cubic_to", "close")
PATH_COMMAND_CODES = {command: index for index, command in enumerate(PATH_COMMANDS)}


def canonical_bytes(value: Any) -> bytes:
    """Stable UTF-8 representation used by semantic hashes."""
    return json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")


def deterministic_bytes(seed: int, domain: str, counter: int, length: int) -> bytes:
    """A reproducible SHAKE256 stream segment, independent of platform byte order."""
    if length < 0:
        raise ValueError("length must be non-negative")
    if counter < 0:
        raise ValueError("counter must be non-negative")
    prefix = (
        b"PikDraw/canvas-corpus/v1\0"
        + str(int(seed)).encode("ascii")
        + b"\0"
        + domain.encode("utf-8")
        + b"\0"
        + str(int(counter)).encode("ascii")
    )
    return hashlib.shake_256(prefix).digest(length)


def _iter_record_bytes(
    seed: int, domain: str, count: int, record_bytes: int
) -> Iterator[bytes]:
    """Yield fixed-size records while retaining at most one bounded byte chunk."""
    if count < 0:
        raise ValueError("record count must be non-negative")
    if record_bytes <= 0:
        raise ValueError("record_bytes must be positive")
    for chunk_index, start in enumerate(range(0, count, STREAM_RECORDS_PER_CHUNK)):
        chunk_items = min(STREAM_RECORDS_PER_CHUNK, count - start)
        payload = deterministic_bytes(
            seed, domain, chunk_index, chunk_items * record_bytes
        )
        for offset in range(0, len(payload), record_bytes):
            yield payload[offset : offset + record_bytes]


def _path_elements_for_object(
    path_element_count: int, object_count: int, object_index: int
) -> int:
    if object_count <= 0:
        return 0
    base, remainder = divmod(path_element_count, object_count)
    return base + (1 if object_index < remainder else 0)


def iter_workload_objects(
    seed: int,
    workload_id: str,
    object_count: int,
    path_element_count: int = 0,
    *,
    overlap: bool = False,
    object_kind: str = "vector",
) -> Iterator[Dict[str, Any]]:
    """Replay every deterministic object record without building a scene list."""
    if object_count < 0 or path_element_count < 0:
        raise ValueError("workload counts must be non-negative")
    if path_element_count and not object_count:
        raise ValueError("path elements require at least one object")
    world = 2_000 if overlap else 2_000_000
    for index, raw in enumerate(
        _iter_record_bytes(
            seed, "scene-objects-v1/" + workload_id, object_count, 32
        )
    ):
        values = [
            int.from_bytes(raw[offset : offset + 4], "little")
            for offset in range(0, 32, 4)
        ]
        if overlap:
            # The bounded 32 mm cluster stays inside the declared 512 mm overlap query.
            x_mm = values[0] % 32
            y_mm = values[1] % 32
        else:
            x_mm = values[0] % world
            y_mm = values[1] % world
        yield {
            "id": f"{workload_id}:object:{index:04d}",
            "kind": object_kind,
            "x_mm": x_mm,
            "y_mm": y_mm,
            "width_mm": 10 + values[3] % 500,
            "height_mm": 10 + values[4] % 500,
            "fill_rgba8": [component for component in raw[20:24]],
            "path_elements": _path_elements_for_object(
                path_element_count, object_count, index
            ),
        }


def _path_owner(
    global_index: int, path_element_count: int, object_count: int
) -> tuple[int, int]:
    base, remainder = divmod(path_element_count, object_count)
    long_object_span = (base + 1) * remainder
    if global_index < long_object_span:
        object_index, local_index = divmod(global_index, base + 1)
    else:
        object_index = remainder + (global_index - long_object_span) // base
        local_index = (global_index - long_object_span) % base
    return object_index, local_index


def iter_path_elements(
    seed: int,
    workload_id: str,
    object_count: int,
    path_element_count: int,
) -> Iterator[Dict[str, Any]]:
    """Replay every path element; S3 streams contain closed, repeating stress contours."""
    if object_count < 0 or path_element_count < 0:
        raise ValueError("workload counts must be non-negative")
    if path_element_count and not object_count:
        raise ValueError("path elements require at least one object")
    domain = "scene-path-elements-v1/" + workload_id
    for global_index, raw in enumerate(
        _iter_record_bytes(seed, domain, path_element_count, 16)
    ):
        object_index, local_index = _path_owner(
            global_index, path_element_count, object_count
        )
        if local_index % 64 == 0:
            command_code = 0
        elif local_index % 64 == 63:
            command_code = 4
        else:
            command_code = 1 + (raw[0] % 3)

        x_mm = int.from_bytes(raw[:8], "little") % 4_000_001 - 2_000_000
        y_mm = int.from_bytes(raw[8:], "little") % 4_000_001 - 2_000_000
        if workload_id.startswith("S3-"):
            contour_index, contour_offset = divmod(local_index, 128)
            orientation = -1 if contour_index % 2 else 1
            x_mm = orientation * ((contour_offset % 16) - 8) * 1_000
            y_mm = ((contour_offset // 16) - 4) * 1_000
            if contour_offset == 0:
                command_code = 0
            elif contour_offset == 127:
                command_code = 4
            if local_index == 1:
                x_mm, y_mm = 1 << 40, -(1 << 40)
            elif local_index == 2:
                x_mm, y_mm = -(1 << 40), 1 << 40

        yield {
            "object_id": f"{workload_id}:object:{object_index:04d}",
            "object_index": object_index,
            "element_index": local_index,
            "command": PATH_COMMANDS[command_code],
            "x_mm": x_mm,
            "y_mm": y_mm,
        }


def _summarize_object_stream(
    seed: int,
    workload_id: str,
    object_count: int,
    path_element_count: int,
    overlap: bool,
    object_kind: str,
) -> Dict[str, Any]:
    digest = hashlib.sha256()
    digest.update(b"PikDraw/canvas-object-stream/v1\0")
    digest.update(
        canonical_bytes(
            {
                "seed": seed,
                "workload_id": workload_id,
                "object_count": object_count,
                "path_element_count": path_element_count,
                "overlap": overlap,
                "object_kind": object_kind,
                "record_encoding": "little-endian object index, mm bounds, rgba8, path count",
            }
        )
        + b"\n"
    )
    count = 0
    for index, record in enumerate(
        iter_workload_objects(
            seed,
            workload_id,
            object_count,
            path_element_count,
            overlap=overlap,
            object_kind=object_kind,
        )
    ):
        digest.update(
            OBJECT_HASH_RECORD.pack(
                index,
                record["x_mm"],
                record["y_mm"],
                record["width_mm"],
                record["height_mm"],
                *record["fill_rgba8"],
                record["path_elements"],
            )
        )
        count += 1
    if count != object_count:
        raise ValueError(f"object stream count mismatch: expected {object_count}, got {count}")
    return {"count": count, "semantic_sha256": digest.hexdigest()}


def _summarize_path_stream(
    seed: int, workload_id: str, object_count: int, path_element_count: int
) -> Dict[str, Any]:
    digest = hashlib.sha256()
    digest.update(b"PikDraw/canvas-path-element-stream/v1\0")
    digest.update(
        canonical_bytes(
            {
                "seed": seed,
                "workload_id": workload_id,
                "object_count": object_count,
                "path_element_count": path_element_count,
                "commands": PATH_COMMANDS,
                "record_encoding": "little-endian object index, local element index, command, x_mm, y_mm",
            }
        )
        + b"\n"
    )
    count = 0
    for record in iter_path_elements(
        seed, workload_id, object_count, path_element_count
    ):
        digest.update(
            PATH_HASH_RECORD.pack(
                record["object_index"],
                record["element_index"],
                PATH_COMMAND_CODES[record["command"]],
                record["x_mm"],
                record["y_mm"],
            )
        )
        count += 1
    if count != path_element_count:
        raise ValueError(
            f"path stream count mismatch: expected {path_element_count}, got {count}"
        )
    return {"count": count, "semantic_sha256": digest.hexdigest()}


@lru_cache(maxsize=128)
def _cached_stream_summary(
    seed: int,
    workload_id: str,
    object_count: int,
    path_element_count: int,
    overlap: bool,
    object_kind: str,
) -> tuple[tuple[int, str], tuple[int, str]]:
    objects = _summarize_object_stream(
        seed,
        workload_id,
        object_count,
        path_element_count,
        overlap,
        object_kind,
    )
    paths = _summarize_path_stream(
        seed, workload_id, object_count, path_element_count
    )
    return (
        (objects["count"], objects["semantic_sha256"]),
        (paths["count"], paths["semantic_sha256"]),
    )


def summarize_scene_streams(
    seed: int,
    workload_id: str,
    object_count: int,
    path_element_count: int,
    *,
    overlap: bool = False,
    object_kind: str = "vector",
) -> Dict[str, Any]:
    """Hash both complete streams; memory is bounded by one generator chunk."""
    object_summary, path_summary = _cached_stream_summary(
        seed,
        workload_id,
        object_count,
        path_element_count,
        overlap,
        object_kind,
    )
    return {
        "seed": seed,
        "generator_version": 1,
        "mode": "replayable-bounded-stream",
        "chunk_records": STREAM_RECORDS_PER_CHUNK,
        "object_stream": {
            "count": object_summary[0],
            "semantic_sha256": object_summary[1],
            "record_encoding": "little-endian object index, mm bounds, rgba8, path count; v1",
        },
        "path_element_stream": {
            "count": path_summary[0],
            "semantic_sha256": path_summary[1],
            "record_encoding": "little-endian object index, local element index, command, x_mm, y_mm; v1",
        },
        "object_path_allocation": "balanced, remainder assigned to lowest stable object indices",
    }


def _camera_trace(
    seed: int, workload_id: str, page_mm: List[int], overlap: bool
) -> Dict[str, Any]:
    raw = deterministic_bytes(seed, "camera-trace-v1/" + workload_id, 0, 24)
    values = [int.from_bytes(raw[offset : offset + 4], "little") for offset in range(0, 24, 4)]
    width_mm = max(1, int(page_mm[0]))
    height_mm = max(1, int(page_mm[1]))
    if overlap:
        center_mm = [16, 16]
        pan_span_x = pan_span_y = 16
    else:
        center_mm = [values[0] % width_mm, values[1] % height_mm]
        pan_span_x = max(1, min(width_mm // 8, 1_000_000))
        pan_span_y = max(1, min(height_mm // 8, 1_000_000))
    pan_mm = [
        values[2] % (2 * pan_span_x + 1) - pan_span_x,
        values[3] % (2 * pan_span_y + 1) - pan_span_y,
    ]
    trace: Dict[str, Any] = {
        "trace_id": f"camera-v1:{workload_id}:{seed}",
        "seed": seed,
        "workload_id": workload_id,
        "source": "synthetic deterministic replay recipe; not a runtime measurement",
        "camera_space": "millimeters",
        "viewport_px": [1920, 1080],
        "page_mm": [width_mm, height_mm],
        "duration_ms": 31_000,
        "steps": [
            {"at_ms": 0, "operation": "set_camera", "center_mm": center_mm, "zoom_milli": 1000},
            {"at_ms": 1000, "operation": "pan", "delta_mm": pan_mm, "duration_ms": 10_000},
            {"at_ms": 11_000, "operation": "zoom", "factor_milli": 8000, "anchor_px": [960, 540], "duration_ms": 10_000},
            {"at_ms": 21_000, "operation": "zoom", "factor_milli": 125, "anchor_px": [960, 540], "duration_ms": 10_000},
        ],
    }
    if overlap:
        trace["query_region_mm"] = [0, 0, 512, 512]
    trace["semantic_sha256"] = hashlib.sha256(canonical_bytes(trace)).hexdigest()
    return trace


def _workload(
    seed: int,
    workload_id: str,
    scenario_id: str,
    description: str,
    *,
    object_count: int = 0,
    path_element_count: int = 0,
    sample_count: int = 4,
    overlap: bool = False,
    object_kind: str = "vector",
    metrics: Optional[Dict[str, Any]] = None,
) -> Dict[str, Any]:
    entry: Dict[str, Any] = {
        "id": workload_id,
        "scenario_id": scenario_id,
        "description": description,
        "object_count": object_count,
        "path_element_count": path_element_count,
        "sample_scope": "bounded preview only; replay section hashes the complete scene streams",
        "sample_objects": list(
            islice(
                iter_workload_objects(
                    seed,
                    workload_id,
                    object_count,
                    path_element_count,
                    overlap=overlap,
                    object_kind=object_kind,
                ),
                min(max(sample_count, 0), 8, object_count),
            )
        ),
    }
    if metrics:
        entry.update(metrics)
    if overlap:
        entry["overlap_query_mm"] = [0, 0, 512, 512]
    if scenario_id != "S8":
        entry["replay"] = summarize_scene_streams(
            seed,
            workload_id,
            object_count,
            path_element_count,
            overlap=overlap,
            object_kind=object_kind,
        )
        page_mm = entry.get("page_mm", [2_000_000, 2_000_000])
        entry["camera_trace"] = _camera_trace(
            seed, workload_id, page_mm, overlap
        )
    return entry


def _build_workloads(seed: int) -> List[Dict[str, Any]]:
    workloads = [
        _workload(seed, "S0-empty", "S0", "Empty document for cold/warm open, DPI resize and tab switch."),
        _workload(seed, "S0-shapes-100", "S0", "100 simple synthetic shapes.", object_count=100, path_element_count=400, sample_count=8),
        _workload(seed, "S1-sparse-10pct", "S1", "Sparse 10% visible view.", object_count=10_000, path_element_count=100_000, metrics={"visible_object_count": 1_000, "page_mm": [20_000, 10_000], "layers": 1, "stroke_cases": ["miter", "round"]}),
        _workload(seed, "S1-sparse-100pct", "S1", "Sparse scene with all objects visible.", object_count=10_000, path_element_count=100_000, metrics={"visible_object_count": 10_000, "page_mm": [20_000, 10_000], "layers": 1, "stroke_cases": ["miter", "round"]}),
        _workload(seed, "S1-all-overlap", "S1", "All objects overlap the query region.", object_count=10_000, path_element_count=100_000, overlap=True, metrics={"visible_object_count": 10_000, "page_mm": [20_000, 10_000], "layers": 1, "stroke_cases": ["miter", "round"]}),
        _workload(seed, "S2-sparse", "S2", "100 layers with sparse visibility and many tiny objects.", object_count=100_000, path_element_count=1_000_000, metrics={"visible_object_count": 10_000, "layers": 100, "max_group_depth": 8, "shape_mix": ["tiny", "large", "path"]}),
        _workload(seed, "S2-all-overlap", "S2", "Worst-case all-overlap query.", object_count=100_000, path_element_count=1_000_000, overlap=True, metrics={"visible_object_count": 100_000, "layers": 100, "max_group_depth": 8, "shape_mix": ["tiny", "large", "path"]}),
        _workload(seed, "S3-path-100k", "S3", "One path with 100,000 segments.", object_count=1, path_element_count=100_001, sample_count=1, metrics={"path_count": 1, "segment_count": 100_000, "cases": ["holes", "self_intersection", "giant_coordinates", "reflection", "nonuniform_scale"]}),
        _workload(seed, "S3-path-1m", "S3", "Stress path with 1,000,000 segments.", object_count=1, path_element_count=1_000_001, sample_count=1, metrics={"path_count": 1, "segment_count": 1_000_000, "cases": ["holes", "self_intersection", "giant_coordinates", "reflection", "nonuniform_scale"]}),
        _workload(seed, "S4-mixed-images", "S4", "Synthetic image metadata; no licensed image/font payload is bundled.", object_count=22, sample_count=0, object_kind="bitmap-placeholder", metrics={"image_count": 22, "images_6000x4000": 20, "images_20000x20000": 2, "decoded_rgba8_pixels": 1_280_000_000, "decoded_rgba8_bytes": 5_120_000_000, "reuse_same_asset_count": 100, "variants": ["sparse-visible", "many-visible", "crop", "icc", "orientation"], "payload_status": "metadata_only_no_bitmap_bytes_materialized", "payload_policy": "synthetic dimensions and asset references only; decoded pixels are not present"}),
        _workload(seed, "S5-effects-100", "S5", "S1 plus a deterministic 100-node effect stack.", object_count=10_000, path_element_count=100_000, metrics={"effect_node_count": 100, "blur_count": 50, "shadow_count": 50, "max_effect_stack_depth": 4, "sigma_reference_px": [2, 16, 64, 256], "cases": ["tile_boundary", "alpha", "group_clip", "transparent_overlap"], "payload_status": "synthetic_effect_graph_metadata_only_no_filtered_pixels_materialized"}),
        _workload(seed, "S6-vietnamese-text", "S6", "Unicode Vietnamese layout and glyph-cache pressure; fonts are not bundled.", object_count=10_000, path_element_count=0, object_kind="text-placeholder", metrics={"glyph_count": 100_000, "text_samples": ["Tiếng Việt: dấu hỏi, dấu ngã, ă â ê ô ơ ư đ.", "Hoà bình và mỹ thuật.", "NFC/NFD: á / á."], "font_policy": "system-font provenance recorded by runner; synthetic text only", "cases": ["ligature", "combining_mark", "font_substitution", "icc_switch"], "payload_status": "synthetic_text_metadata_only_no_font_or_glyph_atlas_materialized"}),
        _workload(seed, "S7-mixed-stress", "S7", "100k objects combined with S4 image and S5 effect metadata.", object_count=100_000, path_element_count=1_000_000, overlap=True, metrics={"image_count": 22, "decoded_rgba8_pixels": 1_280_000_000, "effect_node_count": 100, "cache_pressure": True, "tab_switch": True, "software_forced": True, "payload_status": "synthetic_scene_records_only_image_and_effect_pixel_payloads_absent"}),
    ]

    s8_sizes = [
        ("256MiB", 256 * MIB),
        ("1GiB", GIB),
        ("4GiB", 4 * GIB),
        ("offset-probe-4GiB-plus-1MiB", 4 * GIB + MIB),
    ]
    for label, source_bytes in s8_sizes:
        workloads.append(
            _workload(
                seed,
                "S8-" + label,
                "S8",
                "Synthetic dense chunk payload plan; not a PikDraw native-format file.",
                sample_count=0,
                metrics={
                    "source_bytes": source_bytes,
                    "chunk_bytes": DEFAULT_CHUNK_BYTES,
                    "chunk_count": math.ceil(source_bytes / DEFAULT_CHUNK_BYTES),
                    "payload_algorithm": "SHAKE256 domain-separated deterministic chunks v1",
                    "layout": "dense sequential writes; never sparse-zero substitute",
                    "offset_probe_bytes": (1 << 32) if source_bytes > (1 << 32) else None,
                    "materialized_by_default": False,
                },
            )
        )
    return workloads


def build_manifest(seed: int = DEFAULT_SEED) -> Dict[str, Any]:
    if seed < 0:
        raise ValueError("seed must be non-negative")
    body: Dict[str, Any] = {
        "schema_version": FORMAT_VERSION,
        "corpus_id": "pikdraw-canvas-corpus-v1",
        "generator": {
            "name": "canvas_fixture_manifest.py",
            "version": FORMAT_VERSION,
            "seed": seed,
            "seed_algorithm": "SHAKE256 domain-separated streams; deterministic across platforms",
            "record_streaming": "complete S0-S7 scenes are replayed in bounded record chunks and hashed",
            "stream_record_chunk_records": STREAM_RECORDS_PER_CHUNK,
            "default_mode": "metadata-only",
            "source_rights": "synthetic geometry/text metadata; no customer assets or bundled fonts",
        },
        "workloads": _build_workloads(seed),
        "s8_materialization": {
            "default": "disabled",
            "chunk_bytes": DEFAULT_CHUNK_BYTES,
            "max_chunk_bytes": MAX_CHUNK_BYTES,
            "offset_probe_min_bytes": (1 << 32) + 1,
            "format_status": "payload stress fixture only; native container/open/save remains a separate gate",
            "native_reader_gate": "not_measured",
            "full_disk_gate": "not_measured until a real native reader/writer consumes these bytes",
        },
    }
    body["integrity"] = {
        "algorithm": "sha256",
        "canonicalization": "UTF-8 JSON, sorted keys, compact separators, ensure_ascii=false",
        "semantic_sha256": hashlib.sha256(canonical_bytes(body)).hexdigest(),
    }
    return body


def default_manifest_path() -> Path:
    return Path(__file__).resolve().parents[2] / "tests" / "fixtures" / "canvas-corpus-v1.json"


def write_manifest(path: Path, seed: int = DEFAULT_SEED) -> None:
    payload = json.dumps(build_manifest(seed), ensure_ascii=False, sort_keys=True, indent=2) + "\n"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(payload, encoding="utf-8", newline="\n")


def _entropy_bits_per_byte(data: bytes) -> float:
    if not data:
        return 0.0
    counts = Counter(data)
    total = len(data)
    return -sum((count / total) * math.log2(count / total) for count in counts.values())


def materialize_s8(
    path: Path,
    size_bytes: int,
    *,
    seed: int = DEFAULT_SEED,
    chunk_bytes: int = DEFAULT_CHUNK_BYTES,
) -> Dict[str, Any]:
    """Write dense synthetic chunks to a new file and return verifiable evidence.

    This function never creates a sparse file and refuses to overwrite an existing
    path. It keeps at most one chunk in memory and removes its partial file on error.
    """
    if size_bytes <= 0:
        raise ValueError("size_bytes must be positive")
    if seed < 0:
        raise ValueError("seed must be non-negative")
    if chunk_bytes < 4096 or chunk_bytes > MAX_CHUNK_BYTES:
        raise ValueError(f"chunk_bytes must be between 4096 and {MAX_CHUNK_BYTES}")
    path = Path(path)
    if not path.is_absolute():
        raise ValueError("materialization path must be absolute")
    if not path.parent.is_dir():
        raise ValueError("parent directory must already exist")
    if path.exists():
        raise FileExistsError(path)
    free_bytes = shutil.disk_usage(path.parent).free
    if free_bytes < size_bytes + DISK_RESERVE_BYTES:
        raise OSError(
            f"insufficient free space: need {size_bytes + DISK_RESERVE_BYTES} bytes, "
            f"have {free_bytes}"
        )

    started = time.perf_counter()
    digest = hashlib.sha256()
    created = False
    try:
        with path.open("xb") as stream:
            created = True
            remaining = size_bytes
            chunk_index = 0
            while remaining:
                count = min(chunk_bytes, remaining)
                chunk = deterministic_bytes(seed, "S8-payload", chunk_index, count)
                stream.write(chunk)
                digest.update(chunk)
                remaining -= count
                chunk_index += 1
            stream.flush()
        elapsed = time.perf_counter() - started

        stat_size = path.stat().st_size
        if stat_size != size_bytes:
            raise OSError(f"written size mismatch: expected {size_bytes}, got {stat_size}")

        sample_size = min(64 * 1024, size_bytes)
        offsets = {0, max(0, size_bytes - sample_size)}
        if size_bytes > (1 << 32):
            offsets.add((1 << 32) - sample_size)
            offsets.add(1 << 32)
        sampled = bytearray()
        with path.open("rb") as stream:
            for offset in sorted(offsets):
                stream.seek(offset)
                sampled.extend(stream.read(min(sample_size, size_bytes - offset)))
        return {
            "path": str(path),
            "size_bytes": stat_size,
            "sha256": digest.hexdigest(),
            "chunk_bytes": chunk_bytes,
            "chunk_count": math.ceil(size_bytes / chunk_bytes),
            "dense_write": True,
            "elapsed_seconds": round(elapsed, 3),
            "sampled_offsets": sorted(offsets),
            "entropy_sample_bytes": len(sampled),
            "entropy_bits_per_byte": round(_entropy_bits_per_byte(bytes(sampled)), 6),
            "offset_above_u32_verified": size_bytes > (1 << 32) and (1 << 32) in offsets,
        }
    except BaseException:
        if created:
            try:
                path.unlink()
            except OSError:
                pass
        raise


def _parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    parser.add_argument("--output", type=Path, default=default_manifest_path())
    parser.add_argument("--check", action="store_true", help="compare generated manifest with --output")
    parser.add_argument("--materialize-s8", type=Path, help="explicitly write a dense S8 payload file")
    parser.add_argument("--size-bytes", type=int, help="required with --materialize-s8; no large file is created by default")
    parser.add_argument("--chunk-bytes", type=int, default=DEFAULT_CHUNK_BYTES)
    args = parser.parse_args(argv)
    if args.check and args.materialize_s8:
        parser.error("--check and --materialize-s8 are mutually exclusive")
    if args.materialize_s8 and args.size_bytes is None:
        parser.error("--size-bytes is required with --materialize-s8")
    if args.size_bytes is not None and not args.materialize_s8:
        parser.error("--size-bytes requires --materialize-s8")
    return args


def main(argv: Optional[List[str]] = None) -> int:
    args = _parse_args(argv)
    if args.materialize_s8:
        result = materialize_s8(
            args.materialize_s8,
            args.size_bytes,
            seed=args.seed,
            chunk_bytes=args.chunk_bytes,
        )
        print(json.dumps(result, sort_keys=True))
        return 0

    expected = json.dumps(build_manifest(args.seed), ensure_ascii=False, sort_keys=True, indent=2) + "\n"
    if args.check:
        if not args.output.is_file():
            print(f"manifest missing: {args.output}", file=sys.stderr)
            return 1
        actual = args.output.read_text(encoding="utf-8")
        if actual != expected:
            print(f"manifest differs from deterministic output: {args.output}", file=sys.stderr)
            return 1
        print(f"manifest verified: {args.output}")
        return 0

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(expected, encoding="utf-8", newline="\n")
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

