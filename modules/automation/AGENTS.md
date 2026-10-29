# automation scope

Semantic document mutations and queries are typed and UI-independent. Operations must never depend on desktop GUI widgets or Win32 message loop primitives.
All document writes go through transactional document commands and verify strict epoch/revision matching. Read operations provide bounded projections.
Write operations must be guarded against active interactive gestures (draft dragging, text editing, modal dialogs) via busy checks.
All coordinates and units are normalized to canonical millimeters before reaching the document writer.

Read root AGENTS.md and this module README. Load Contract C10 when working on automation protocols.
Tests must verify observable correctness, schema validation, and concurrency/busy guards.
