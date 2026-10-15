# PikDraw drawing contracts

Version: 1.0.0. Status: proposed; consumer review is required before runtime tasks implement these semantics.

`semantics.json` is the machine-readable contract for editable path identity, authoring drafts,
numeric input, snap state, direction constraints, profile chords, centered shapes and file migration.
`vectors.json` contains expected results independent of a renderer or implementation. Run
`python contracts/drawing/verify_vectors.py` to check its independent analytic expectations. This
validator checks reference data only; it does not run the app or consumer implementations. The vectors
are not yet wired into a protected global quality gate; feature tasks must consume them, and the gate
owner must register them separately before claiming automatic enforcement.

## Required invariants

- Source coordinates are mm/binary64 and use world +Y up. Render conversion is camera-relative and
  happens only after subtracting the origin.
- A committed object has one authoritative source model. `Path2D`, flattened paths, screen overlays,
  and preview geometry are derived values.
- Pointer previews do not mutate snapshots. Finish is one atomic command; cancel is no command.
- Snap options are independently maskable. The F3 master preserves every mask and is independent of
  numeric, Ortho, Polar, and shape constraints.
- Every query/result is stamped with document, tool, gesture, camera, config, and capability
  generations; stale responses cannot publish a hint or commit a command.
- UI shortcuts are context-aware and user-remappable. The profile defines gesture semantics, not a
  promise of complete CorelDRAW/Illustrator shortcut compatibility.
- Native v3 is required for stable path-element identity and parametric shape sources. The current
  development codec v2 is not silently extended to write this schema.

## Rollout boundary

D02 implements exact geometry queries/curves; D03 owns authoring model and commands; D04 owns migration
and codec; D05 owns ToolSession; D06 numeric grammar; D07 basic snap; D09 UI/actions; D10–D15 Pen,
Node, shapes and advanced snap. A contract being present does not make any of these features available
in the application.
