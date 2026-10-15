# Contract C6: Isolated Workers and IPC Protocol

Version: 1.0.0
Status: Locked (M0 Foundation)
Producers: `platform/workers`
Consumers: `modules/interop`, `modules/plugin_host`, `modules/desktop`

---

## 1. Process Isolation Boundary

Third-party file format parsers (e.g. CDR, non-PDF AI, legacy EPS, external plugins) are untrusted and must never be loaded into the main editor process. They execute inside an isolated child worker process.

### Windows AppContainer Sandbox
- Every worker process is launched within an isolated AppContainer profile.
- **Capabilities**: **ZERO Network Capabilities** (`internetClient`: NONE, `privateNetworkClientServer`: NONE). Any attempt to bind, listen, or establish outbound socket connections is blocked with `WSAEACCES` (10013) by the operating system kernel.
- **File System Permissions**: Access is restricted strictly to read and execute the worker's own private binary directory. Access to user documents, system directories, registry hives, and network shares is denied (`ERROR_ACCESS_DENIED`, 5).

### Job Object Quotas
- `active_process_limit = 1`: Child process spawning is disabled (`JOB_OBJECT_LIMIT_ACTIVE_PROCESS`, `prevent_child_processes = true`). Attempts to invoke `CreateProcessW` return `ERROR_NOT_ENOUGH_QUOTA` (1816) or `ERROR_ACCESS_DENIED`.
- `job_memory_limit = 512 MB`: Physical commit limit. Workers exceeding quota are terminated cleanly without affecting the host.
- `cpu_rate_control`: Workers run with below-normal scheduling priority to prevent UI starvation.

---

## 2. IPC Wire Protocol (Length-Prefixed Frames)

Host and workers communicate over anonymous unidirectional standard pipes (`stdin`/`stdout`).

### Packet Header Structure
```
+---------------+---------------+---------------+---------------+
| Magic (4B)    | Length (4B)   | MsgType (4B)  | Sequence (8B) |
+---------------+---------------+---------------+---------------+
| Payload (Length - 20 Bytes) ...                               |
+---------------------------------------------------------------+
| CRC32 (4B)                                                    |
+---------------------------------------------------------------+
```

- **Magic**: `0x50 0x4B 0x57 0x50` (`PKWP` - PikDraw Worker Protocol).
- **Length**: Total frame size in bytes including header and checksum. Maximum allowed frame: 64 MB.
- **MsgType**:
  - `0x0001`: `Ping` / `Pong` (Keep-alive and health check)
  - `0x0002`: `ImportRequest` / `ImportResponse`
  - `0x0003`: `ExportRequest` / `ExportResponse`
  - `0x0004`: `CancelJob`
  - `0x0005`: `CanaryProbeRequest` / `CanaryProbeResponse` (Testing only)
- **Sequence**: Request identifier paired between caller and response.
- **CRC32**: Checksum for frame integrity verification.

---

## 3. Bounded Execution Deadlines and Cancellation

- **Transaction Deadline**:
  Host operations set explicit timeout deadlines (e.g., 5 seconds for preview, 30 seconds for full import).
- **Graceful Cancel & Termination**:
  If a worker fails to respond within the deadline or receives a `CancelJob` instruction while stalled, the host invokes `TerminateJobObject` or `TerminateProcess`, drains all pipe handles, and logs an actionable diagnostic.
- **Fault Recovery**:
  Host immediately spawns a fresh worker instance upon abnormal worker exit or crash. The user is informed via a non-fatal banner, and the host document remains completely unaffected.
