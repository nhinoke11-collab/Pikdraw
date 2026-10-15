# ADR 0006 — Editable paths and precision drawing input

Date: 2026-10-15
Status: Proposed for consumer review; implementation has not started
Scope: Geometry, document, rendering, storage, vector interaction, desktop actions

## Context

PikDraw must support mouse-first vector drawing and exact technical input in the same document. The
current source model is `Path2D` with line, quadratic, cubic, endpoint-arc verbs and close; it has no
stable subpath/node/segment identities or parametric shape source. The current PathObject stores that
path directly. Existing selection/style work is on the reviewed code baseline `970273d`, but Pen,
Node, numeric constraints, configurable snap masks and technical geometry are not implemented.

The renderer and manufacturing paths need different approximations and error budgets. A single
screen-space or fixed-mm tolerance cannot define identity, hit testing, curve flattening, snap capture,
and Boolean topology. Existing shell references also contain shortcut conflicts: the UI reference
advertises F10 for Node, Shift+F2 for Zoom to Selection and F8 for Text; the current key handler does
not implement all advertised bindings. The roadmap's earlier F10-Polar proposal cannot be applied
without a binding audit.

## Decision

### One editable source model

Each geometry object has one tagged source: `EditablePath`, `ParametricShape`, or
`ConstructionGeometry`. `Path2D`, renderer tessellation, hit-test polylines, and screen overlays are
derived read-only values. Existing consumers migrate through adapters; the document never stores two
independently editable versions of the same curve.

Editable paths contain ordered subpaths, typed stable IDs, anchors, and line/quadratic/cubic/analytic
elliptic-arc segments. IDs are opaque nonzero 64-bit values allocated without wrap or reuse and
reserved across object and path-element IDs on load. Node modes are Corner, Cusp, Smooth, Symmetric,
and deterministic AutoSmooth v1. Smooth guarantees geometric G1 only; Symmetric adds equal handle length
and does not claim C2. Exact split preserves endpoint IDs and source curve; the old segment ID remains
on the leading portion. Topology changes return explicit ID maps. Separated endpoints require an
explicit Bridge; coincident endpoint merge requires a selected survivor.

Ellipse arcs use `P(t)=C+U*cos(alpha0+t*sweep)+V*sin(alpha0+t*sweep)`. Affine transforms map the
center as a point and both basis vectors as vectors, retaining the parameter interval. Rendering may
flatten for a bounded pixel error, but never overwrites this source. Screen hit radius and snap radius
use DIP; geometry algorithms take explicit model-space tolerances. No global epsilon or renderer float
is used as a manufacturing tolerance.

### Draft, commit, and stale-result behavior

ToolSession keeps an uncommitted draft with document ID/epoch/base revision, tool generation and
gesture generation. Pointer preview does not publish a document snapshot or undo record. Finish
creates one atomic command/revision; cancel creates none. Draft-local undo/redo is consumed before
document history. No draft is silently rebased across an external document edit.

- Enter with a valid numeric candidate places it once and continues the session; Enter otherwise
  finishes an open path.
- Escape in an input field restores that field's prior candidate. Escape during a drag rolls back only
  the current gesture. Escape during a new-segment preview finishes previously confirmed segments as
  an open path and discards the current candidate. Escape before the first point cancels the session.
  Shift+Escape cancels the whole draft.
- Backspace/Ctrl+Z first undo the last draft operation while a draft is active. Once no draft-local
  operation remains, Ctrl+Z reaches document history.
- Focus/capture loss rolls back an uncommitted gesture and clears transient keys. Tab/document switch,
  close, external revision, or changed center source invalidates stale draft references/proposals.

### Numeric input and constraints

Source length is mm/binary64; angles are counter-clockwise degrees from the local construction-frame
+X. The frame is explicit and independent of camera rotation. `@L<theta` is a relative polar point,
`@dx;dy` is relative Cartesian, and `#x;y` is absolute world position. Supported units are mm, cm, in
and pt (`1 in = 25.4 mm`, `1 pt = 25.4/72 mm`). A typed expression parser supports unary signs,
parentheses, addition, subtraction, multiplication and division with dimensional checks; it never
evaluates a script. Locale decimal separators are accepted per numeric token; thousands separators
are rejected; semicolon separates coordinate components. NaN, infinity, overflow, division by zero,
wrong dimensions, and invalid values leave the document unchanged.

Priority is: valid numeric locks, hard tool constraints (including Ortho, modifier angle, circle/square),
compatible snap candidates, soft Polar/grid acquisition, then raw pointer. A candidate incompatible
with a numeric or hard constraint is filtered before ranking. Pen endpoint length `L` means anchor
chord distance, not curve length; handle length is a separate `H` value.

### Snap configuration and stale rejection

F3 toggles only automatic endpoint/object/relation/page/grid/guide/construction candidate acquisition.
It preserves every child mask and does not turn off numeric locks, explicit shape constraints, Ortho,
Polar, or a held modifier. Child masks are independently retained when a group is disabled. “Select all”
and “clear all” are separate actions. Display markers/tracking lines may remain visible without magnetic
acquisition.

The direction mode is one of Free, Ortho or Polar. Enabling a different mode replaces the active mode;
toggling the active mode returns to Free and preserves each mode's settings. Presets are 15°, 30°, 45°
and 90°, with a finite custom angle list normalized to [0°,360°). At an exact tie, retain the current
ray if it is one of the tied candidates; otherwise choose the smallest normalized angle. A held profile
modifier hard-locks the nearest allowed ray for the whole gesture. Polar mode softly acquires a ray
within 6 DIP, retains it through 10 DIP, and does not acquire rays for segments shorter than 16 DIP.
Object snaps default to an 8 DIP acquisition radius and 12 DIP release radius, configurable from 1 to
64 DIP.

Groups cover object points, automatic geometric relations, page/grid/guides/construction, display, and
target policy. The complete option IDs and profile are in `contracts/drawing/semantics.json`.
Configuration precedence is workspace profile, per-tool override, then transient gesture modifier.
Preferences do not dirty the document. Disabled snap kinds are filtered before spatial queries. Candidate
ranking uses per-tool/type priorities, distance in DIP, stable source ID and curve parameter; ties do
not depend on container iteration order.

Each query and result carries document ID/epoch/revision, camera generation, config generation,
capability generation, tool context and query generation. A mismatch discards the result before showing
a hint or committing. Candidate data includes source IDs, kind, analytic parameter, world point and
source revision. Heavy relation queries are bounded by spatial candidates and a time/quota budget.

### Modifier profiles and action dispatch

PikDraw and Illustrator-inspired profiles use Shift for angle/aspect, Alt for rectangle/ellipse from
center, and Shift+Alt for both. Corel-inspired uses Ctrl for angle/aspect, Shift for center, and
Ctrl+Shift for both. Alt breaks opposite Bézier handles in both; adding the constrain modifier also
locks handle angle. Temporary Node in an active Pen draft uses Ctrl for PikDraw/Illustrator-inspired
profiles and held F2 for Corel-inspired. Shift+click adds/removes Nodes. Space+drag and middle-button
drag pan. S bypasses automatic snap only in the canvas and only if no reserved exact command chord
consumes it; it never bypasses numeric or geometric constraints.

Resolution order is focus scope → active tool → pointer phase → exact chord → named action. Text/IME,
AltGr, OS-reserved keys and accessible menu equivalents must be handled. Profile changes during a drag
apply after that gesture. Ctrl+Shift+S remains Save As and wins over S-bypass; the user can toggle the
snap master before the gesture. Function-key defaults remain unresolved until D09 reconciles existing
UI references and actual handlers. Tool, snap, direction, and HUD actions are available by stable IDs
and must remain remappable; no unimplemented action is advertised as active.

### Centered shapes and regular polygons

Rectangle/ellipse support corner or center construction and aspect constraints. A circle or regular
polygon created from an explicit center does not have its center shifted again by a center modifier.
Analytic center references include source ObjectId, document epoch/revision and center value; stale
references are discarded. “Keep center for next shape” is session-only. Coincident centers at creation
do not create persistent constraints; those belong to later solver work.

A regular polygon source stores integer `N` (3..512), center, circumradius `R` or inradius `r` or side
length `a`, rotation and vertex-up/edge-up orientation. `a=2R sin(pi/N)` and `r=R cos(pi/N)`.
Multiple entered drivers must agree within explicit parameter-consistency tolerance; otherwise retain
the last valid preview and report the conflict. The analytic parameters remain authoritative until an
explicit conversion. Star geometry has a separate definition.

### Storage and migration

The authoring path schema is version 1.0. The current development codec v2 cannot persist its IDs or
parametric shape source; C5 2.0 therefore requires a v3 native container. v2 sources migrate only when
conversion is defined and lossless; child IDs are allocated during migration and persisted on v3 save.
The v1 lossy prototype remains rejected. Unknown required tags and v3 files opened by older readers
are rejected with diagnostics. A v3 document is not silently downgraded or flattened. D04 must define
the exact v3 byte layout, compatibility field, quotas and checksum coverage before enabling writes.

## Alternatives considered

- Keep `Path2D` as the only source: rejected because stable node identity, parametric edits and robust
  source-curve splitting cannot be represented without parallel mutable metadata.
- Use screen-pixel tolerance as geometry tolerance: rejected because zoom/DPI would change model
  topology and production results.
- Make F3 disable every constraint: rejected because an object-snap preference must not erase an
  explicitly typed length, Ortho, Polar or shape constraint.
- Copy all shortcuts from CorelDRAW/Illustrator: rejected because global shortcut conflicts, AltGr,
  IME and product-specific action context differ. Only the named profiles above are promised.
- Store centered shapes as polygonal paths: rejected because source parameters and exact centers would
  be lost before an explicit conversion.

## Consequences and rollout

D02 implements curve evaluation, bounds, affine arcs and queries. D03 implements the authoritative
path model, stable IDs and commands. D04 implements the v3 codec/migration. D05–D07 implement session,
numeric parser and basic snap. D09 implements controls/actions after shortcut conflict resolution.
D10–D15 implement Pen, Node, freehand, shapes and advanced snaps. Every task consumes these vectors;
consumer review and integrated tests remain required. This ADR and its vectors define a target, not a
claim that the application already implements it.

## Consumer impact reviewed

The current vector model (`modules/vector`, `core/document/model/object.hpp`) stores `Path2D` directly.
`core/geometry/curves/path.hpp` has endpoint-arc verbs but no stable element IDs; current flattening
falls back to a line for `ArcTo`, and `Path2D::transformed` maps stored points/control points rather
than transforming an ellipse basis. These consumers require D02/D03 adapters before Pen/Node can claim
analytic arcs.

`core/document` already provides immutable snapshots, command/undo, stable object IDs and strict stale
proposal checks; D03 adds path-element identity, and D05 adds the interaction-draft lifecycle.
`core/rendering` already uses camera-relative projection but has no revisioned tool-overlay contract;
D09 must pass the C3 generation stamps. The native v2 codec cannot persist path IDs; D04 owns the v3
migration. SVG adapters consume `Path2D` and need explicit fidelity reporting for unsupported source
features. The current native toolbar/key handler does not yet route Line/Polyline/Pen/Node/Snap actions;
D09 owns action registration and must preserve the advertised but unimplemented F-key references until
they are reconciled. Production measurement and routing remain R5 and are not implied by this ADR.
