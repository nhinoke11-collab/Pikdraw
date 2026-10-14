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
- Document changes go through commands/transactions. Preserve stable IDs, undo/redo, units, color metadata and editable source data.
- Import fidelity, editability and source-format export are separate capabilities. Report unsupported features; no silent flattening or format claims without fixtures.
- Third-party parsers and plugins require process isolation at runtime; capabilities must be enforced by the host, not trusted declarations.
- Work in the assigned task worktree and owned paths. Changes to shared contracts require a scoped task and dependent-module review.
- Initial scaffold assembly may occur on main before the first foundation commit. Later implementation work uses task branches.
- Keep credentials, private customer files, binaries and full chat logs out of Git and context packets.

## Delivery
Run checks relevant to the change; the harness baseline is `doctor` and `python -m unittest discover -s tests/harness -v`.
Use the handoff protocol in `docs/agents/PROTOCOL.md`. Record evidence, unresolved limits and the next action.
Update project state at integration; record architectural changes as ADRs. Never label planned features implemented.
Human-facing documentation is Vietnamese; concise English contracts/instructions are acceptable to reduce cross-provider context.
