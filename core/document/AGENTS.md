# document scope

All writes use commands/transactions. Keep original editable objects, stable IDs, units and versioned migrations. Derived production/3D data references source IDs.
One logical writer publishes immutable root/index snapshots with monotonic revisions, including undo/redo. Async proposals carry document epoch/base revision/job generation; MVP rejects any stale base rather than guessing a rebase. Cancel/close/source edits cannot half-commit.
Evaluate chunk COW and immutable shared assets; measure prepare/validation/commit/invalidation and retention/cleanup. Save/export uses one snapshot and preserves asset lifetime; save r must not clear dirty edits r+1. Recovery needs explicit durability and fault injection. See ../../docs/architecture/document-runtime.md.

Read root AGENTS.md and this module README. Load architecture/quality references only when the task needs them.
Tests must verify observable correctness or boundaries, not merely repeat implementation details.
