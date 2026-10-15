# Contract C2: Document Model and Command Execution

Version: 1.1.0
Status: M0 document invariants remain locked; drawing-authoring extension proposed pending consumer review
Producers: `core/document`
Consumers: `core/rendering`, `modules/desktop`, `modules/vector`, `modules/raster`, `modules/production`, `modules/interop`

---

## 1. Concurrency Model: Single Logical Writer

- **Single Writer**: All state mutations against the active document are serialized through a single logical writer queue hosted on the main UI/Application thread. Background worker threads, external plugins, or asynchronous operations are strictly prohibited from mutating document structures directly.
- **Concurrent Readers**: Any number of reader threads (e.g., Viewport rasterizers, Preflight checkers, Exporters) may concurrently read immutable document snapshots without acquiring reader locks or contending with the writer thread.
- **Copy-On-Write (COW)**: Document mutation commands construct a new root node revision using shared persistent data structures (COW pages/trees). Unmodified subtrees are shared across snapshots, preserving minimal memory retention.

---

## 2. Document Revisions and Lifecycle Epochs

- **`revision: uint64_t`**: Monotonically increasing 64-bit integer. Incremented by exactly 1 upon every successfully committed transaction. Never decremented or reused during the document's open session.
- **`document_epoch: uint64_t`**: Unique 64-bit identifier assigned upon document creation/opening. Changed when a document is closed or reloaded from disk.
- **`job_generation: uint64_t`**: Counter identifying the background computation generation. Incremented whenever an in-flight operation is cancelled or invalidated by user edits.

---

## 3. Stable Object Identifiers

- **Object Identity**: Every document entity (Artboard, Layer, PathObject, TextObject, Group, Component) possesses a `stable_id: uint64_t` (or 128-bit UUID).
- **Invariance**: An entity's `stable_id` remains constant throughout all transformations, styling changes, parent hierarchy re-parenting, and z-order index reordering.
- **Undo/Redo Stability**: Undoing a deletion and subsequent redo recreates the object with its original `stable_id`, ensuring external references, selections, and annotations remain intact.

---

## 4. Transactional Commands and Undo/Redo

- **Atomic Transactions**:
  - Every user action is encapsulated in a `DocumentCommand`.
  - Commands implement:
    - `Execute(Document& doc) -> Result<void, CommandError>`
    - `Undo(Document& doc) -> Result<void, CommandError>`
  - Atomicity guarantee: If `Execute` fails at any intermediate stage, the entire transaction is rolled back to the pre-command snapshot, leaving no partial mutation.
- **History Stack**:
  - Maintained as bounded linear stacks (Undo and Redo stacks) with configurable memory and depth limits (default: 100 steps).
  - Executing a new command while the Redo stack is non-empty prunes the Redo branch cleanly.

---

## 5. Strict Stale Proposal Rejection

Background workers, automated tools, or async filters propose mutations by returning a `DocumentProposal`:
- Required fields in `DocumentProposal`:
  - `document_epoch: uint64_t`
  - `base_revision: uint64_t`
  - `job_generation: uint64_t`
  - `target_object_ids: vector<uint64_t>`
  - `payload: CommandData`
- Rejection Rules:
  1. If `proposal.document_epoch != active_document.epoch`: **REJECT** (Document was closed/reloaded).
  2. If `proposal.base_revision != active_document.revision`: **REJECT** (Document was mutated by user while job was running).
  3. If `proposal.job_generation != active_document.current_job_generation`: **REJECT** (Job was superseded or cancelled).
  4. If any `target_object_id` no longer exists: **REJECT**.
- Under no circumstance may a stale proposal overwrite or silently merge into newer revisions without explicit three-way merge resolution.

## 6. Editable geometry and interaction drafts (extension 1.1)

- A path-bearing document object has exactly one authoritative geometry source: `EditablePath`, `ParametricShape`, or `ConstructionGeometry`. The choice is a tagged source model, not two writable copies. Existing `Path2D` and renderer flattening are derived adapters until every consumer migrates.
- `PathId`, `SubpathId`, `NodeId`, and `SegmentId` are stable opaque nonzero 64-bit typed IDs allocated from the document identity allocator, unique across document objects and path elements. Moving/styling/reordering an element preserves its IDs. Undo/redo restores IDs. Split/join/delete/close/reverse commands return explicit ID mappings for references and selection. IDs are not inferred from coordinates or array indices, and the allocator never wraps or reuses an ID.
- Tool drafts and pointer previews are interaction state, not document state. A session is bound to document ID, lifecycle epoch, base revision, tool generation, and gesture generation. Pointer movement changes only the draft/preview. Finish applies one atomic document command and one revision increment; cancel applies none. Local draft undo/redo is exhausted before document history is changed.
- Space+drag, middle-button pan, and wheel zoom suspend/resume the same draft without changing its world anchors or numeric locks. Camera changes invalidate only camera-dependent hints/previews, not confirmed draft geometry.
- A command prepared from a stale base, a closed/reopened document, cancelled generation, or replaced tool session is rejected. Drafts are never silently rebased over external edits. A changed source invalidates center references and derived proposals.
- If draft recovery is supported, it is a bounded, separate checkpoint with the original base identity; it is not a saved/committed document and is never auto-committed. Recovery against a changed base is rejected or offered as a user-visible reconstruction.
- Parametric shapes retain their source parameters until an explicit Convert to Path command. Construction geometry is document data with stable object identity and undo, but is excluded from normal output unless an explicit export capability says otherwise.
