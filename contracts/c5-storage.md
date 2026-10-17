# Contract C5: Native Storage and File Durability

Version: 2.2.0
Status: Native v4 appearance schema 1.0 codec is implemented and covered by byte-level regressions; broader C5 durability and recovery acceptance remain separate
Producers: modules/storage
Consumers: core/document, modules/desktop, modules/interop

Implementation audit (2026-10-17): source writes native container v4 and reads v2, v3, or v4
(core/document/include/pikdraw/document/storage/native_format.hpp and
core/document/src/storage/native_format.cpp). V4 persists Path/Text appearance schema 1.0 and
migrates v2/v3 documents to default appearance in memory; explicit save writes v4. Independent
byte fixtures cover appearance records, node ordering, Shadow schema 1.1, blend IDs, corruption,
and v2/v3 migration in core/document/tests/storage/test_storage_fidelity.cpp. This confirms the
development codec path, not complete C5 durability, recovery, or every production-file gate.

---

## 1. Legacy Native File Format (`.pikdraw`, C5 1.0.0)

The v1 container below is retained as a historical specification only. It is not the layout of the
current development v3 codec.

The native file format is a structured binary/compressed container:
- **Magic Header (8 bytes)**: `0x50 0x4B 0x44 0x52 0x01 0x00 0x00 0x00` (`PKDR\x01\x00\x00\x00`).
- **Container Version**:
  - `format_version: uint32_t` (current: 1).
  - `min_compat_version: uint32_t` (earliest version required to parse the file).
- **Metadata Section**:
  - `document_epoch: uint64_t`
  - `saved_revision: uint64_t`
  - `created_timestamp: int64_t`
  - `modified_timestamp: int64_t`
  - `app_version: string`
- **Data Payload**: Compressed binary or JSON payload containing the persistent document tree.
- **Integrity Checksum (Trailing 4 or 32 bytes)**:
  - CRC32-IEEE or SHA-256 covering all preceding bytes. Files with mismatched checksums are rejected immediately upon open.

---

## 2. Atomic Save Durability Policy

To ensure zero file corruption in the event of unexpected application crash, power outage, or OS kernel freeze:
1. **Isolated Temp File**: Save data is written to a new temporary file located in the target directory:
   `<destination_path>.pikdraw.tmp.<PID>.<Epoch>`
2. **Buffer Flush (`FlushFileBuffers`)**:
   Prior to closing the temporary file handle, the process issues `FlushFileBuffers` on Windows (or `fsync` on POSIX) to force all pending write caches to non-volatile physical storage.
3. **Atomic Replace (`ReplaceFileW`)**:
   The temporary file is atomically swapped into the destination path using Win32 `ReplaceFileW(target, temp, nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)`.
4. **Zero Residual Leftovers**:
   If any error occurs during write or flush, the temporary file is deleted, and the existing target file remains completely untouched.

---

## 3. Crash Recovery and Autosave Index

- **Autosave Cadence**:
  The application saves snapshots of dirty documents at periodic intervals (default: 3 minutes) to an isolated recovery directory:
  `%APPDATA%/PikDraw/Recovery/<document_uuid>/autosave.pikdraw`
- **Recovery Index**:
  An `index.json` maintains the mapping of original file paths to their recovery files, last known revision, and timestamp.
- **Clean Shutdown Cleanup**:
  Upon intentional document close or clean application shutdown, recovery files are cleanly removed.
- **Startup Recovery Prompt**:
  Upon startup, if orphaned recovery files exist, the user is prompted to restore or discard the autosaved sessions.

## 4. Drawing-authoring schema and migration

### 4.1 Current development v4 audit

The implementation audit below records source behavior only; it does not certify conformance to this proposed contract.

| Item | Current source behavior |
| --- | --- |
| Container header | 36 bytes: four-byte PKDR magic, uint32 format version, uint32 feature flags (currently zero), uint64 document epoch, uint64 revision, uint32 payload length, and uint32 CRC32. The checksum covers header plus payload with its own four bytes treated as zero. |
| Accepted/written versions | The writer emits v4. The reader accepts legacy v2, authoring-source v3, and current v4; it rejects v1 and unknown versions. |
| Compatibility/schema values | In v4, minimum-compatible version 4, authoring-path schema 1.0, and appearance schema 1.0 are payload fields after the length-prefixed UTF-8 document ID; they are not header fields. |
| Bounds observed in source | 256 MiB file size, 100,000 objects, 1,000,000 path elements, and 10,000 layers, with additional field-specific string/image limits. |
| v2 path migration | The reader converts legacy path geometry to editable source, assigns stable element IDs, and rejects a path it cannot convert. The v3 writer also assigns stable source IDs when a path lacks authoring source. |
| Appearance | V2/v3 do not store opacity, blend mode, or the effect graph. Their decoded Path/Text objects receive default appearance in memory; v4 stores those values in framed records. |

The native v1 format in section 1 remains historical. V2-to-editable-path migration, v3 authoring-source decoding, and v4 appearance persistence are distinct steps and must be reported separately.

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

There are at most 256 nodes per object and 100,000 nodes per document. An individual appearance record cannot exceed the remaining payload or 256 MiB; an individual node record and parameter payload cannot exceed its containing record or remaining payload. Type IDs are at most 128 UTF-8 bytes, blend IDs at most 32 bytes, and ColorValue strings retain the current 16 MiB cap. Existing file/object/path-element/layer limits continue to apply. All aggregate additions and count-to-byte calculations use checked arithmetic and are validated before allocation.

Appearance schema 1.0 contains no opaque vendor payload and no optional effect node: every stored node is semantically required. Unknown type IDs, unsupported node schema versions, unknown required schema fields, duplicate/zero IDs, invalid UTF-8, invalid ranges, non-finite values, impossible lengths/counts, and unconsumed bytes inside a record reject the whole file before publishing a snapshot. A future non-semantic optional extension may be skipped only if explicitly framed as optional and retained byte-for-byte through load/save; otherwise an unknown extension rejects or opens through a separately implemented read-only/reference workflow and cannot be silently discarded by editable save.

Nodes remain in source order. No render pixels, scratch surfaces, backend handles, cache keys, or transient previews are serialized. Appearance provenance required by the document model is stored separately from render state and covered by the same file checksum and limits.

The logical schema and parser/writer field order above are implemented. Exact offsets depend on existing variable-length strings, paths, and ColorValue payloads. The independent byte fixtures in test_storage_fidelity.cpp verify representative defaults, Blur, ordered Blur/Shadow, Shadow 1.1, six blend IDs, malformed/unsupported records, checksum failure, and v2/v3 migration. This evidence covers the tested cases, not every possible corrupt-input or crash-recovery scenario.
### 4.3 v2/v3 to v4 migration and compatibility

- Opening v3 creates default appearance for each PathObject and TextObject: object/fill/stroke opacity 1, blend Normal, and an empty effect list. Source color alpha remains in ColorValue; the migration does not rewrite source channels or metadata.
- Opening v2 first uses the existing v2-to-editable-path conversion and stable-ID assignment. If every path is representable, it then supplies the same default appearance. If a path cannot be migrated losslessly, reject the document with its object-specific diagnostic and leave the active document unchanged.
- Opening does not rewrite the source file. On an explicit successful save, a document loaded from v2 or v3 is written as v4. The migration preserves document/object/path IDs where they already exist, units, transforms, source colors/profiles, font identity, fill/winding, layer order, visibility, and locks. A newly allocated path-element ID is persisted once in v4.
- A v4 file declares minimum-compatible version 4. A v2/v3 reader rejects v4 before document publication. The v4 reader rejects any unknown required schema or feature; it does not reinterpret an unknown appearance as defaults.
- There is no v4-to-v3 writer. Saving an appearance-bearing document to v2/v3 is forbidden because those formats cannot represent the graph. A future explicit conversion, if ever offered for a document with only default appearance, requires a separate loss report and acceptance gate.
- v1 lossy files remain rejected under ADR 0005. Failed parse, migration, validation, quota admission, or checksum verification publishes no partial snapshot and leaves the prior file/document untouched.

### 4.4 Recovery and bounded resource policy

- A recovery/checkpoint record that contains a v4 document snapshot must carry the document ID, epoch, base revision, native/appearance schema versions, a checksum, and the same object/node/path limits. It must preserve the full appearance graph and reject a stale or unsupported record before publication. An older recovery schema must not accept an appearance-bearing snapshot and then omit its nodes.
- Native parsing validates every count and record length against both its local cap and remaining bytes before allocating. Checked arithmetic is required for aggregate sizes. The v4 node limits above are storage quotas; C3 separately owns per-render memory/work admission. A valid saved sigma or offset that exceeds the current render budget remains source data and produces an explicit incomplete/budget diagnostic rather than a silently omitted effect.
- Save/recovery uses the same required-schema, checksum, migration, and no-partial-publish rules. A corrupt or over-budget input cannot replace the existing file or current document.
