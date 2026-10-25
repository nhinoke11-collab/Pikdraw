# PikDraw workers

**Trạng thái:** Đã có host worker Windows với AppContainer/Job Object, giới hạn vòng đời, worker protocol v1/v2 và kiểm tra kết quả trước khi nhận. Bộ parser/plug-in sản phẩm và containment acceptance đầy đủ vẫn chưa hoàn tất.

Responsibility: process launch, isolated source snapshots, bounded IPC, resource limits, timeouts and validated proposals. Dependency authority: `registry/modules.json` (interop, plugin-sdk).

`spike_worker.exe` is the production worker: it requires the AppContainer host policy, inherits only an explicit handle list and allowlisted environment, and rejects test canary requests and sandbox bypass flags. `spike_worker_test.exe` exists only when tests are enabled and carries repository-owned failure fixtures and canary behavior; it is never the shipped worker.

Protocol and proposal data are untrusted. Validate sizes, framing, hashes, job identity, epoch/revision and semantics before atomic document application. Timeout or invalid responses terminate the worker scope; partial proposals never commit. Job Objects alone do not prove OS containment; retain live canary evidence and do not claim parser or plug-in acceptance from protocol tests.

Build through `tooling/native/build.ps1`. Keep workers independent of desktop UI. See [worker scope rules](AGENTS.md) and [sandbox evidence policy](../../docs/roadmap/performance-storage-automation.md).

Latest focused local evidence (2026-10-18): `test_pikdraw_workers_live_containment`,
`test_pikdraw_worker_v2_integrity`, and `test_pikdraw_workers_policy` passed 3/3. The live run
denied five OS canaries after positive controls and passed a read-only 20 MiB source-handle
probe. Its local report is `build/win-dev/platform/workers/live-containment-report.json` and
marks `full_spike_accepted=false`; it does not establish format fidelity or release-wide quota
coverage. See [F03 dependency readiness](../../docs/quality/file-interop-dependency-readiness.md)
for scope and third-party engine decisions.
