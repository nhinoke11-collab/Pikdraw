# Contract C5: Native Storage and File Durability

Version: 2.0.0
Status: Proposed breaking drawing-authoring extension; exact codec implementation awaits D04
Producers: `modules/storage`
Consumers: `core/document`, `modules/desktop`, `modules/interop`

Implementation audit (2026-10-15): the original lossy v1 prototype did not implement C5 1.0.0.
The repository currently has a development codec v2 documented in
`docs/decisions/0005-native-storage-repair.md`; it repairs several source-fidelity and parser issues,
but is **not accepted C5 compliance**. This C5 2.0.0 extension defines required source and migration
behavior for drawing authoring. It does not claim the target codec is implemented.

---

## 1. Legacy Native File Format (`.pikdraw`, C5 1.0.0)

The v1 container below is retained as a historical specification only. The current repository codec
is development v2. Neither is sufficient to persist the drawing-authoring source model in this C5
2.0.0 contract.

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

## 4. Drawing authoring schema and migration (C5 2.0.0)

- The payload has a separately versioned `authoring_path_schema`; the initial value is 1.0. It stores stable IDs and exact line/quadratic/cubic/analytic-arc source. Parametric shape parameters and construction-object state are tagged, versioned source values.
- The drawing-authoring model requires native container `format_version=3`. Development codec v2 cannot encode path-element IDs or parametric shape sources. D04 must audit the codec and define the exact v3 byte layout, compatibility field, limits, and checksum coverage before enabling a v3 writer.
- A v3 reader validates container version, required feature/schema versions, finite numeric values, ID uniqueness, lengths/counts/depth, and checksum before publishing any document state. Unknown required path/shape tags reject the entire document.
- A v2 file may be migrated only when every represented source feature has a defined conversion. Because v2 has no path-element IDs, migration allocates fresh stable IDs once; the next v3 save persists them. Unsupported or ambiguous source features produce a fidelity report and block a lossless-save claim. A v3 document is never silently written back as v2.
- A v2 reader or unknown required schema rejects a v3 file with an unsupported-version/feature diagnostic. v1 lossy files remain rejected per ADR 0005. Recovery/checkpoint follows the same version and source-fidelity rules as normal save.
- Migration preserves document/object IDs, units, local transforms, source colors, font identity, fill/winding, visibility/locks, and layer/order metadata. Malformed, unsupported, or over-budget data never leaves a partially migrated document or replaces the prior file. Diagnostics identify the source object and unsupported feature; no silent flattening is allowed.
- An in-progress ToolSession draft, if checkpointed, uses a separate bounded record containing document identity, epoch, base revision, schema version, checksum, and quota. It cannot be opened as a committed `.pikdraw` document and is never auto-applied; a mismatched base requires explicit user reconstruction or discard.
