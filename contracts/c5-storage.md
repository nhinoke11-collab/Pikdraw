# Contract C5: Native Storage and File Durability

Version: 1.0.0
Status: Locked (M0 Foundation)
Producers: `modules/storage`
Consumers: `core/document`, `modules/desktop`, `modules/interop`

Implementation audit (2026-10-15): the original lossy v1 prototype did not implement this
locked container specification. Development codec v2 is documented in
`docs/decisions/0005-native-storage-repair.md`; it repairs source fidelity and bounded parsing,
but is **not accepted C5 compliance**. Metadata/assets, migrations, recovery index/UI and
power-loss evidence remain required. No producer may advertise C5/M1 acceptance from v2 unit tests alone.

---

## 1. Native File Format (`.pikdraw`)

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
