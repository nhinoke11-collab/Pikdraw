# workers scope

Treat worker output as untrusted data. Validate before atomic document application. Crashes/timeouts terminate task scope; do not apply partial results.
IPC version/count/bytes/depth/finite-value validation, document epoch/revision and job generation are host-enforced. Cancelled/stale/closed-document responses cannot commit. Bound queue, memory, CPU/time and child-process lifecycle; account for viewport responsiveness.
Process isolation or Job Objects alone do not prove filesystem/network containment. Test denied canary access, ungranted network, handles and child processes under the selected OS sandbox; untrusted plugins remain disabled until containment evidence. See ../../docs/roadmap/foundation-spikes.md.

Read root AGENTS.md and this module README. Load architecture/quality references only when the task needs them.
Tests must verify observable correctness or boundaries, not merely repeat implementation details.
