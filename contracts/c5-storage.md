# Contract C5: Native storage and file durability

Version: 2.7.0 (chunk-streamed atomic save and v25 raster sample deduplication).
Audited: 2026-10-25 against the working source.
Status: PKDR v23 remains the compact schema; v24 adds deduplicated encoded-source and
RGBA8/RGBA16 sample tables, and v24/v25 chunked containers are read for compatibility/current raster saves.
Producer: core/document/storage. Consumers: desktop, automation, raster and interop.
Current design suffix: .pr only. Filename extension does not authenticate content.

## 1. Current container and schemas

Source of truth: core/document/include/pikdraw/document/storage/native_format.hpp and
core/document/src/storage/{native_format.cpp,native_chunk_encode.cpp,native_chunk_decode.cpp,
byte_codec.hpp,file_io.cpp,object_codec.cpp}.
Payloads without encoded raster source bytes use schema v23: under 1 MiB they use the legacy
v23 container, and larger payloads use chunked container v24. When embedded raster source bytes
exist, schema v24 writes encoded sources and equal RGBA8/RGBA16 samples once in shared tables;
image records reference them by 1-based indices (0 means no optional source/U16 sample). Container
v25 carries that schema. Reader accepts v2–v25 and checks magic/version/checksums,
schema and semantics before publication. V2–v23 headers are 36 bytes: PKDR magic, u32 version,
u32 flags=0, u64 epoch, u64 revision, u32 payload length, u32 CRC32. V24/v25 retain that prefix,
use flags=1 and the u32 payload-length sentinel 0xffffffff, then add uncompressed-payload-size:u64
and chunk-count:u64.
Each 1 MiB chunk has a 16-byte record (raw size:u32, stored size:u32, codec:u8, reserved[3],
raw CRC32:u32) followed by STORE or zlib-wrapped DEFLATE bytes. DEFLATE uses the pinned zlib-ng
compatibility library at Z_BEST_SPEED and is selected only when smaller than STORE.

There is no fixed 256 MiB whole-container cap. Reader admission is bounded by half of currently
available physical memory; v24/v25 use 64-bit aggregate payload/chunk sizes while legacy field counts
and individual blob lengths retain their u32 wire-width bounds. Object and element caps are now
u32 wire-width rather than 100,000/1,000,000; string cap remains 16 MiB and field-specific quotas
remain in their codecs. Atomic save uses `serialize_to` and stages at most one 1 MiB raw chunk before
writing chunk records to its temporary file. The reader still keeps compressed input and decoded
payload in memory; its admission check is a resource quota, not streaming file IO.

| Version milestones | Persisted feature |
| --- | --- |
| v3–v5 | Authoring paths, appearance, gradient paints |
| v6–v10 | Pages, text layout, raster assets, page color/DPI, PowerClip |
| v11–v18 | PDF source/preview, display units, page settings, ICC resources and paint bindings, path feature specs |
| v19–v23 | Raster source descriptors/edit graph, import fidelity, text properties, layer properties, artboards |
| v24 payload / v25 container | Embedded raster source and equal RGBA8/RGBA16 decoded sample deduplication |

RGBA pixel records can coexist with encoded original source bytes. Atomic save streams payload
encoding into an exclusive temporary file and patches payload length, chunk count and CRC after the
last record; `NativeFormat::serialize()` remains the vector-returning API for callers that request a
complete buffer. Loading still reads the complete file and decodes chunks into a contiguous payload
before document parsing. A disk-backed descriptor is not by itself a portable embedded resource.
Raster source and equal decoded sample deduplication are implemented in the v24 payload tables; dedup
for other asset types, a seekable index, streaming load and lazy asset access remain follow-up work; see
[PR format](../docs/architecture/native-pr-format.md).

## 2. Save durability and snapshot identity

SaveRecoveryService owns atomic_save and low-level load. Desktop/MCP accept .pr design filenames;
recovery uses internal .pkdr checkpoints. A save takes one immutable snapshot R, writes a temporary
file in the destination directory, flushes and replaces the destination only on success.
On failure preserve the old file and remove owned temporary output. Save R must not mark edits R+1
clean. Do not promise immunity to power/device failure from the API name: crash/flush/disk-full
fault injection and reopen are required evidence for each supported filesystem/environment.

## 3. Recovery and resource lifecycle

Desktop recovery directory is %LOCALAPPDATA%/PikDraw/Recovery/<session-guid>.
Checkpoint names/manifests are defined by save_recovery.cpp, not user design suffixes.
Keep the previous valid checkpoint until the replacement is durable. Normal close and explicit
discard follow the recovery service policy. Autosave checkpoint serialization runs through bounded
background jobs over immutable snapshots. Startup scan, selected recovery and UI .pr Open run in
background jobs but remain full-buffer. Open cancellation is checked during 1 MiB reads and at most
64 KiB CRC/parse/inflate windows; selected recovery validates its captured identity before tab
creation. Cancellation latency and snapshot construction are not measured/cooperative. Save is not
cancelled after the durability request. A startup recovery selection modal exists in the native shell.
See [implementation plan](../docs/roadmap/performance-storage-automation.md).

## 4. Historical schema details: native v4/v5

These sections document the older wire layouts for migration fixtures only. They do not select
the current writer version or override the current source/limits above.

### 4.1 Historical v4 audit

The table below records the historical v4 implementation for migration fixtures. Current writes and reader acceptance are defined in sections 1–3 above.

| Item | Historical v4 behavior |
| --- | --- |
| Container header | 36 bytes: four-byte PKDR magic, uint32 format version, uint32 feature flags (currently zero), uint64 document epoch, uint64 revision, uint32 payload length, and uint32 CRC32. The checksum covers header plus payload with its own four bytes treated as zero. |
| Accepted/written versions | At that milestone the writer emitted v4 and reader accepted v2/v3/v4. Current writes use schema v23 or v24, containers v23–v25, and reader accepts v2–v25. |
| Compatibility/schema values | In v4, minimum-compatible version 4, authoring-path schema 1.0, and appearance schema 1.0 are payload fields after the length-prefixed UTF-8 document ID; they are not header fields. |
| Bounds observed in source | 256 MiB file size, 100,000 objects, 1,000,000 path elements, and 10,000 layers, with additional field-specific string/image limits. |
| v2 path migration | The reader converts legacy path geometry to editable source, assigns stable element IDs, and rejects a path it cannot convert. The v3 writer also assigns stable source IDs when a path lacks authoring source. |
| Appearance | V2/v3 do not store opacity, blend mode, or the effect graph. Their decoded Path/Text objects receive default appearance in memory; v4 stores those values in framed records. |

Native v1 is rejected; this appendix does not select the current writer version. V2-to-editable-path migration, v3 authoring-source decoding, and v4 appearance persistence are distinct steps and must be reported separately.

### 4.2 Implemented native v4 appearance schema 1.0

Native v4 is the first container revision that persists the C9 basic appearance profile. The writer and reader are implemented in core/document/src/storage; the storage regression suite includes independently constructed byte fixtures and legacy migration cases. This implementation covers Path/Text only and does not claim generic forward-compatible object framing or source-format interoperability.

The v4 container keeps the existing 36-byte header field order and little-endian encoding: magic[4], format_version:u32=4, feature_flags:u32=0, document_epoch:u64, revision:u64, payload_bytes:u32, crc32:u32. CRC32 covers the complete header and payload with the CRC field bytes treated as zero. Payload lengths and the full container remain bounded by 256 MiB.

All scalar integers are unsigned little-endian. Binary64 values use the existing IEEE-754 little-endian encoding and must be finite. A string is u32 byte_length followed by that many UTF-8 bytes; lengths are checked against remaining payload before reading, UTF-8 is validated, and the existing 16 MiB string limit applies unless a narrower cap is stated. A boolean is one byte and only 0 or 1 is valid. Counts and record lengths are u32. These rules match the current Writer/Reader primitive conventions and v4 validation.

The v4 payload prefix is in this exact order:
1. document_id as a length-prefixed UTF-8 string;
2. minimum_compatible_version:u32 = 4;
3. authoring_path_schema_major:u32 = 1;
4. authoring_path_schema_minor:u32 = 0;
5. appearance_schema_major:u32 = 1;
6. appearance_schema_minor:u32 = 0;
7. the existing page, layer, and object sequence.

The v4 page/layer/object source encodings remain the v3 encodings. Immediately after the complete v3 source record for each PathObject and TextObject, write one appearance_record_length:u32, then exactly that many appearance bytes. Every Path/Text record is present in v4, including default appearance. GroupObject and ImageObject have no appearance record in schema 1.0. Unknown object tags reject because schema 1.0 has no forward-compatible object body framing.

The appearance record body is encoded in this exact order:
1. object_opacity:f64;
2. fill_opacity:f64;
3. stroke_opacity:f64;
4. blend_id as a length-prefixed UTF-8 string, capped at 32 bytes;
5. effect_count:u32;
6. effect_count ordered node records.

Each node record is node_record_length:u32 followed by exactly that many node-body bytes. A node body is encoded in this order:
1. instance_id:u64, nonzero and unique in the document ID namespace;
2. type_id as a length-prefixed UTF-8 string, capped at 128 bytes;
3. schema_major:u32;
4. schema_minor:u32;
5. enabled:u8, restricted to 0 or 1;
6. parameter_bytes:u32;
7. exactly parameter_bytes type-specific payload bytes.

Known node payloads are:
- pikdraw.effect.gaussian-blur: sigma_x_mm:f64, sigma_y_mm:f64, exactly 16 parameter bytes;
- pikdraw.effect.drop-shadow schema 1.0: dx_mm:f64, dy_mm:f64, sigma_x_mm:f64, sigma_y_mm:f64, the ColorValue encoding defined below, then shadow_opacity:f64. Its payload length is derived from the ColorValue record and checked against the node and appearance record lengths.
- pikdraw.effect.drop-shadow schema 1.1: the schema 1.0 fields followed by shadow_blend_id as a length-prefixed UTF-8 string, capped at 32 bytes. The six stable IDs are `normal`, `multiply`, `screen`, `overlay`, `darken`, and `lighten`. For RGB ColorValue, schema 1.0 is 115 bytes and schema 1.1 adds 4 bytes plus the UTF-8 ID length.

The embedded ColorValue uses the current native color tags and field order: color_tag:u8 (0 RGB, 1 CMYK, 2 Spot, 3 Gray), overprint_fill:u8, overprint_stroke:u8; then RGB r/g/b/a:f64, CMYK c/m/y/k/a:f64, Spot spot_name:string plus tint:f64 plus alternate RGB r/g/b/a:f64 plus spot_overprint:u8, or Gray gray/a:f64. The string uses the length/UTF-8 rules above. The shadow payload length is therefore 40 bytes plus the ColorValue bytes: 75 for RGB, 83 for CMYK, 59 for Gray, or 88 plus spot-name byte length for Spot.

Opacity and shadow opacity are finite and in [0,1]. Sigma values are finite and nonnegative. Offsets are finite. Blend and effect IDs are case-sensitive stable identifiers, never localized labels or numeric enum ordinals. The six schema 1.0 object blend IDs are normal, multiply, screen, overlay, darken, and lighten. Drop Shadow schema 1.0 implies Normal shadow compositing and zero spread; loading it supplies `normal` without changing or rewriting the source record. Schema 1.1 stores its own shadow blend ID. Gaussian Blur supports schema 1.0 only.

There are at most 256 nodes per object and 100,000 nodes per document. An individual appearance record cannot exceed the remaining payload; an individual node record and parameter payload cannot exceed its containing record or remaining payload. Type IDs are at most 128 UTF-8 bytes, blend IDs at most 32 bytes, and ColorValue strings retain the 16 MiB cap. Container reads also obey the adaptive memory quota; format counts retain their u32 representation bounds. All aggregate additions and count-to-byte calculations use checked arithmetic and are validated before allocation.

Appearance schema 1.0 contains no opaque vendor payload and no optional effect node: every stored node is semantically required. Unknown type IDs, unsupported node schema versions, unknown required schema fields, duplicate/zero IDs, invalid UTF-8, invalid ranges, non-finite values, impossible lengths/counts, and unconsumed bytes inside a record reject the whole file before publishing a snapshot. A future non-semantic optional extension may be skipped only if explicitly framed as optional and retained byte-for-byte through load/save; otherwise an unknown extension rejects or opens through a separately implemented read-only/reference workflow and cannot be silently discarded by editable save.

Nodes remain in source order. No render pixels, scratch surfaces, backend handles, cache keys, or transient previews are serialized. Appearance provenance required by the document model is stored separately from render state and covered by the same file checksum and limits.

The logical schema and parser/writer field order above are implemented. Exact offsets depend on existing variable-length strings, paths, and ColorValue payloads. The independent byte fixtures in test_storage_fidelity.cpp verify representative defaults, Blur, ordered Blur/Shadow, Shadow 1.1, six blend IDs, malformed/unsupported records, checksum failure, and v2/v3 migration. This evidence covers the tested cases, not every possible corrupt-input or crash-recovery scenario.
### 4.3 v2/v3 to v4 migration and compatibility

- Opening v3 creates default appearance for each PathObject and TextObject: object/fill/stroke opacity 1, blend Normal, and an empty effect list. Source color alpha remains in ColorValue; the migration does not rewrite source channels or metadata.
- Opening v2 first uses the existing v2-to-editable-path conversion and stable-ID assignment. If every path is representable, it then supplies the same default appearance. If a path cannot be migrated losslessly, reject the document with its object-specific diagnostic and leave the active document unchanged.
- Opening does not rewrite the source file. The historical v4 writer saved migrated v2/v3 documents as v4; the current writer saves them as schema v23, or v24 when an embedded raster source table is present. The migration preserves document/object/path IDs where they already exist, units, transforms, source colors/profiles, font identity, fill/winding, layer order, visibility, and locks. A newly allocated path-element ID is persisted once in v4.
- A v4 file declares minimum-compatible version 4. A v2/v3 reader rejects v4 before document publication. The v4 reader rejects any unknown required schema or feature; it does not reinterpret an unknown appearance as defaults.
- There is no v4-to-v3 writer. Saving an appearance-bearing document to v2/v3 is forbidden because those formats cannot represent the graph. A future explicit conversion, if ever offered for a document with only default appearance, requires a separate loss report and acceptance gate.
- v1 lossy files remain rejected under ADR 0005. Failed parse, migration, validation, quota admission, or checksum verification publishes no partial snapshot and leaves the prior file/document untouched.

### 4.4 Recovery and bounded resource policy

- A recovery/checkpoint record that contains a v4 document snapshot must carry the document ID, epoch, base revision, native/appearance schema versions, a checksum, and the same object/node/path limits. It must preserve the full appearance graph and reject a stale or unsupported record before publication. An older recovery schema must not accept an appearance-bearing snapshot and then omit its nodes.
- Native parsing validates every count and record length against both its local cap and remaining bytes before allocating. Checked arithmetic is required for aggregate sizes. The v4 node limits above are storage quotas; C3 separately owns per-render memory/work admission. A valid saved sigma or offset that exceeds the current render budget remains source data and produces an explicit incomplete/budget diagnostic rather than a silently omitted effect.
- Save/recovery uses the same required-schema, checksum, migration, and no-partial-publish rules. A corrupt or over-budget input cannot replace the existing file or current document.

### 4.5 Native v5 gradient paint schema 1.0

Native v5 is the container version that persists the Gradient paint schema 1.0 for PathObject.
The v5 container header maintains the 36-byte format with `format_version: uint32_t = 5`.
The payload prefix adds `gradient_schema_major: uint32_t = 1` and `gradient_schema_minor: uint32_t = 0`.
`minimum_compatible_version: uint32_t` is set to 5.

Immediately following the appearance record bytes for each `PathObject`, write `gradient_record_length: uint32_t`:
- If `gradient_record_length == 0`: no gradient paint is attached (`fill_gradient == nullptr`).
- If `gradient_record_length > 0`: exactly `gradient_record_length` bytes containing the gradient record body.

The gradient record body is encoded in this exact order:
1. `schema_major: uint32_t` (1)
2. `schema_minor: uint32_t` (0)
3. `paint_id: uint64_t` (nonzero, unique)
4. `kind: uint8_t` (0 = Linear, 1 = Radial)
5. `spread: uint8_t` (0 = Pad, 1 = Repeat, 2 = Reflect)
6. `interpolation: uint8_t` (0 = EncodedSRGBStraightAlpha)
7. `gradient_to_object: 6 x f64` (a, b, c, d, tx, ty)
8. `stop_count: uint32_t` (2 to 256)
9. `stop_count` ordered stop records.

Each stop record is encoded in this order:
1. `stop_id: uint64_t` (nonzero, unique within paint)
2. `offset: f64` ([0.0, 1.0], monotonic non-decreasing)
3. `color: ColorValue` (using standard native color codec)
4. `opacity: f64` ([0.0, 1.0], stop-level opacity multiplier)
5. `midpoint_to_next: f64` ([0.01, 0.99]; terminal stop always 0.5)

Validation and bounds:
- 2 to 256 stops per paint; max 1,000,000 stops per document.
- Offsets must be finite, in [0, 1], and monotonic non-decreasing. Hard edges (equal offsets) preserve vector order.
- Invertibility guardrail: relative inverse threshold `|det(A/n)| > 1e-12` with `n = max(|a|,|b|,|c|,|d|) > 0`.
- All coordinates and lengths must be finite and within host limits (`|coord| <= 1e9`).
- Corruption, truncated records, duplicate IDs, out-of-range values, or unconsumed bytes reject the file atomically before publishing.

### 4.6 v2/v3/v4 to v5 migration and compatibility

- Opening v2, v3, or v4 documents loads all paths with `fill_gradient = nullptr`. Their solid `fill` and `has_fill` are preserved.
- The historical v5 writer emitted v5. Current writes use schema v23 or v24; schema v23 uses the v23 container below 1 MiB or v24 above it, while schema v24 uses chunk container v25.
- Legacy readers (v4 or older) reject v5 files with an unsupported version diagnostic rather than attempting lossy fallback.

## 5. Current source and acceptance boundary

- [Container versions/header](../core/document/include/pikdraw/document/storage/native_format.hpp), [codec quotas](../core/document/src/storage/byte_codec.hpp).
- [Native format implementation](../core/document/src/storage/native_format.cpp), [save/recovery](../core/document/src/storage/save_recovery.cpp).
- Regression locations: [storage](../core/document/tests/storage/test_storage.cpp), [fidelity](../core/document/tests/storage/test_storage_fidelity.cpp), [appearance migration](../core/document/tests/storage/test_storage_appearance_migration.cpp).

PKDR v24/v25 compression, chunked atomic-save serialization, and the >256 MiB atomic save/open regression are implemented. Raster
source and equal RGBA8/RGBA16 sample deduplication are covered by separately allocated source
round-trip and shared-buffer assertions.
Atomic save encodes through a bounded 1 MiB payload buffer and writes compressed chunks directly to
the temporary file; `NativeFormat::serialize()` remains a vector-returning API. Load still reads the full
container and materializes the decoded payload before parsing. Cancellation polling through reads, CRC,
parse, DEFLATE and chunk CRC has deterministic storage coverage; actual cancel latency, save/open peak
memory, progress, large-design UI latency, lazy asset loading, and raster dedup performance still need
measurement. Import memory admission and native storage admission remain separate quotas. Changing a
filename suffix alone does not reduce file size or allocations.

