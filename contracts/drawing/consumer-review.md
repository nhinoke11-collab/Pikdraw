# D01 consumer impact review

Historical review of the 2026-10-15 candidate baseline. Its "current source" column is not the present runtime state. For the source audit dated 2026-10-24, use the [current C1–C9 index](../README.md) and each contract's source pointers. This file preserves review context and does not override current contracts.

Reviewed against candidate baseline `970273dc35a98cf9fdf4f7e8f3d3f48b5c9fd836` on 2026-10-15.
This is the coordinator's source-level impact review; it is not an independent external review and does
not claim that proposed interfaces are implemented.

| Consumer | Current source behavior | Required change before implementation can claim the contract | Planned owner |
| --- | --- | --- | --- |
| `core/geometry` | `Path2D` stores a flat verb sequence; `ArcTo` uses endpoint radii/flags. `flattening.cpp` substitutes the endpoint for `ArcTo`; `Path2D::transformed` maps stored points/control points only. Boolean and Offset still return bounding-box approximations. | Add analytic arc conversion/evaluation/split/bounds/affine operations and explicit errors/tolerances; keep Boolean/Offset outside this contract's claims. | D02, D16 |
| `core/document` | `PathObject` stores mutable-by-command `Path2D`; object IDs are nonzero `uint64_t`; snapshots, undo/redo and stale proposal rejection already exist on the candidate branch. There are no child path IDs or parametric-shape variants. | Add stable typed path IDs, one authoring-source variant, atomic topology commands and ID maps; preserve strict document epoch/revision/generation checks. | D03 |
| `core/rendering` | Renderer consumes snapshots, subtracts camera origin and has a software path pipeline; it has no contract for tool preview/snap overlays or camera-generation-stamped candidate hints. | Add read-only path adapter and transient overlay/hint inputs; discard stale camera/config/capability generations; retain camera-relative float conversion. | D04 adapter, D09 |
| `core/document/storage` | Repository codec v2 is a development format described in ADR 0005; C5 1.0 still describes v1 and v2 has no stable node/segment IDs or parametric source. v1 lossy input is rejected. | Define the exact v3 container/header/quota/checksum and lossless v2 migration; preserve the v2 reader and reject unsupported required tags. | D04 |
| `modules/vector` | Current tools cover selection, rectangle, ellipse and text; there is no Pen/Node session, numeric parser, snap config or exact-shape authoring source. | Add ToolSession, numeric evaluator, snap query/config, Pen/Node and shape controllers as separate feature groups. | D05–D15 |
| `apps/desktop` | The tool strip advertises V/R/O/T/H/Z. The native shortcut handler handles V/R/E/H/F/1 plus file/edit chords; the design reference advertises F10 Node, Shift+F2 Zoom to Selection and F8 Text without matching key-handler support. | Register named actions before presenting controls; audit existing shortcut ownership and conflicts, preserve text/IME/AltGr/system handling, then add profile-aware modifier routing. | D09 |
| `modules/interop` | SVG adapters consume `Path2D`; native path schema changes are not yet reflected in their input/output adapters. | Adapt through source model; report unsupported parametric/construction features instead of silently flattening or claiming round-trip fidelity. | D04 and scoped interop child task |
| `modules/production` | Measurement/routing modules are not implemented as editor tools. | Add linked measurement and route source schemas only after separate model/storage/render consumers are reviewed. | D20, D21 |

## Contract-by-contract review result

- **C1 1.1:** Existing mm/binary64, axis and transform-order rules remain. Arc basis, node handle semantics and tolerance classes are additive; geometry must implement exact analytic transforms before a consumer uses them.
- **C2 1.1:** Existing writer/snapshot/undo/stale semantics remain. Path IDs and source variants require D03 model/commands and ID allocator review; they do not permit a second mutable `Path2D` copy.
- **C3 1.1:** Existing projection and renderer isolation remain. Overlay data is revision/generation stamped and transient; UI thread owns preview session state, renderer consumes immutable values only.
- **C5 2.0:** This is an explicit breaking extension. No codec change is included in D01. D04 must own exact bytes, version negotiation, quota, migration and fault tests before any feature serializes the new source.
- **C7 1.1:** IDs are a catalog addition. Gesture chords are scoped by focus/tool/pointer phase; old shortcut references are not treated as working bindings until code proves them. Save As and Corel Ctrl+Shift modifier combinations are captured in a conflict vector.

## Unresolved before D09

1. Correct the drawing roadmap's proposed F-key assignments against the current tool strip, reference screenshots and actual native handler. This task leaves function-key defaults unbound except existing named actions.
2. Confirm whether snap preference defaults belong to workspace, profile or both in the actual settings store; all remain outside Document.
3. Run consumer-specific semantic vectors in D02–D07 and D09. The new vectors are not registered with the protected global gate in this task.
