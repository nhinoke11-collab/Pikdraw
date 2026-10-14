# PikDraw agent entrypoint

This is the canonical, provider-neutral instruction file. Provider adapters only point here.
On Windows, `./tooling/pikdraw.ps1` can replace `python tooling/agent_harness.py`; it finds an available Python runtime without installation.

## Boot, in order
1. Read `docs/START.md` and `docs/project-state.json`.
2. Use `python tooling/agent_harness.py task list` to find your assigned task. If none is assigned, clarify task scope or create an authorized task; do not pick unrelated work.
3. Read `skills/pikdraw-session/SKILL.md`. Obtain a bounded context packet with `context --task-id TASK`.
4. Read only your module context and the skill needed for the change. Follow scoped module instructions.

## Invariants
- The desktop is a professional vector editor. Production and 3D features share the document model.
- Dependencies follow `registry/modules.json`; no module-to-UI or circular dependencies.
- Document changes go through a single logical writer and atomic commands/transactions. Readers consume immutable snapshots; async proposals carry document epoch/revision and job generation. Reject stale results, including after undo/cancel/close.
- Preserve stable IDs, undo/redo, real units, source curves, editable Unicode/font data, source RGB/CMYK/spot/ICC and overprint metadata. Keep render LOD and manufacturing approximations separate from source data.
- Import fidelity, editability and source-format export are separate capabilities. Report unsupported features; no silent flattening or format claims without fixtures.
- Third-party parsers and plugins require process isolation at runtime; capabilities must be enforced by the host, not trusted declarations.
- Work in the assigned task worktree and owned paths. Changes to shared contracts require a scoped task and dependent-module review.
- Dynamic multi-agent execution: provider labels are session metadata. Parallel work requires disjoint task leases and semantic contract vectors, not only matching schemas. Worktrees and owned-path checks are not OS write sandboxes.
- Hardware resilience follows ADR 0002: choose backends by tested capabilities, not GPU brand/tier. Software 2D has an explicit budget; recoverable device loss and fatal driver crashes have different policies. Runtime choices remain proposed until spikes pass.
- C++20/Qt/Skia is the spike baseline, not a locked runtime stack. Geometry coordinates/transforms propose mm/binary64; render uses local float coordinates. Do not publish renderer types as storage/plugin contracts.
- Initial scaffold assembly may occur on main before the first foundation commit. Later implementation work uses task branches.
- Keep credentials, private customer files, binaries and full chat logs out of Git and context packets.

## Delivery
Run checks relevant to the change; the harness baseline is `doctor` and `python -m unittest discover -s tests/harness -v`.
App gates also require actual dependency/ownership checks, consumer semantic tests and evidence on the integrated revision. Protect shared gates/oracles from feature-task edits. Harness success does not prove graphics, performance, format fidelity or OS containment.
Use the handoff protocol in `docs/agents/PROTOCOL.md`. Record evidence, unresolved limits and the next action.
Update project state at integration; record architectural changes as ADRs. Never label planned features implemented.
Human-facing documentation is Vietnamese; concise English contracts/instructions are acceptable to reduce cross-provider context.
Before runtime implementation, read the bounded references relevant to the task in `docs/roadmap/foundation-spikes.md` and `docs/architecture/document-runtime.md`. These are specifications, not implemented features.
